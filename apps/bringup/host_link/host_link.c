#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "app_clock.h"
#include "board_watchdog.h"
#include "host_link.h"
#include "host_protocol.h"
#include "precharge.h"
#include "rs485_tx.h"
#include "serial_rx.h"

/** 包含 520 字节组帧缓存及原有调用余量的线程栈预算，单位为字节。 */
#define HOST_LINK_STACK_SIZE 1536u
/** 通信线程优先级，低于预充线程。 */
#define HOST_LINK_PRIORITY 5
/** 联调期间发送主动上报前的接收空闲时间，单位为毫秒。 */
#define HOST_LINK_RX_IDLE_MS 5u
/** 心跳请求周期，单位为毫秒。 */
#define HOST_LINK_HEARTBEAT_MS 3000u
/** 心跳应答占用总线的联调窗口，单位为毫秒。 */
#define HOST_LINK_HEARTBEAT_GUARD_MS 100u
/** RS485 线程进度上报的超时，单位为毫秒。 */
#define HOST_LINK_WATCHDOG_TIMEOUT_MS 3000u
typedef enum
{
    HOST_LINK_NO_EVENT = 0,       /**< 当前没有待发送上报。 */
    HOST_LINK_PRECHARGE_DONE = 1, /**< 电压检查通过。 */
    HOST_LINK_FAULT = 2,          /**< 预充或运行故障。 */
} host_link_event_e;

typedef enum
{
    HOST_LINK_HEARTBEAT = 2, /**< 预充板发起的心跳。 */
    HOST_LINK_CONTROL = 3,   /**< 启动或停止。 */
    HOST_LINK_REPORT = 4,    /**< 预充板主动上报。 */
} host_link_type_e;

/** 已实现的预充完成和板级故障上报载荷，按实际长度发送。 */
typedef struct
{
    uint8_t sequence_le[2]; /**< 上报序号，小端。 */
    uint8_t event;          /**< 预充完成或故障。 */
    uint8_t cause;          /**< 故障原因，仅故障事件携带。 */
    uint8_t channel_mask;   /**< 故障通道位图，是否携带由原因决定。 */
    uint8_t currents[2u * PRECHARGE_CHANNEL_COUNT]; /**< 超限通道电流，小端。 */
} host_link_report_data_t;

/** 预充板发送缓冲区，Data 容量按已实现的最大载荷配置。 */
typedef struct
{
    host_link_header_t header;                     /**< 固定帧头。 */
    uint8_t data[sizeof(host_link_report_data_t)]; /**< 实际发送长度由功能码决定。 */
} host_link_tx_frame_t;

/** 由 485 线程独占的接收与上报状态。 */
typedef struct
{
    host_protocol_parser_t parser; /**< 当前串口字节流的解析状态。 */
    int64_t heartbeat_at_ms; /**< 下次心跳发送时刻。 */
    int64_t heartbeat_guard_until_ms; /**< 主动上报避让截止时刻。 */
    int64_t heartbeat_sent_ms; /**< 待应答心跳的发送完成时刻，-1 表示无待应答心跳。 */
    uint32_t precharge_id; /**< 完成事件的内部编号，不在线协议中传输。 */
    uint16_t handover_sequence; /**< 已发完成上报的序号，用于匹配接管应答。 */
    bool handover_pending; /**< 已发送完成上报，等待接管应答。 */
    host_link_event_e event;        /**< 当前待发送的上报类型。 */
    uint16_t sequence;              /**< 最近分配的上报序号。 */
    host_link_report_data_t report; /**< 待发送的上报 Data。 */
    size_t report_size;             /**< 待重发 Data 长度。 */
} host_link_session_t;

LOG_MODULE_REGISTER(host_link, LOG_LEVEL_INF);

static K_THREAD_STACK_DEFINE(host_link_stack, HOST_LINK_STACK_SIZE);
static struct k_thread host_link_thread;

/**
 ******************************************************************************
 * @brief      封装并发送预充板报文。
 * @param[in]  type:帧类型。
 * @param[in]  command:命令编号。
 * @param[in]  data:Data 字节，长度为零时允许为空。
 * @param[in]  length:Data 字节数，最多为发送帧的 Data 容量。
 * @retval     0:发送成功。
 * @retval     负值:参数不合法或串口发送失败。
 ******************************************************************************
 */
static int send_frame(host_link_type_e type, uint8_t command, const void *data, size_t length)
{
    host_link_tx_frame_t frame =
    {
        .header =
        {
            .direction = 0xaau,
            .type = (uint8_t)type,
            .command = command,
        },
    };
    int size = host_protocol_encode(&frame, sizeof(frame), &frame.header, data, length);
    int err;

    if (size < 0)
    {
        return size;
    }
    err = board_rs485_send((const uint8_t *)&frame, (size_t)size);
    if (err)
    {
        LOG_ERR("RS485 TX failed: %d", err);
    }
    else
    {
        LOG_HEXDUMP_INF(&frame, (size_t)size, "RS485 TX");
    }
    return err;
}

/**
 ******************************************************************************
 * @brief      将预充接口的返回值转换成协议受理结果。
 * @param[in]  error:预充接口返回值。
 * @retval     协议结果码。
 ******************************************************************************
 */
static uint8_t control_result(int error)
{
    switch (error)
    {
        case 0:
            return 0x00u;
        case -EBUSY:
            return 0x01u;
        case -EINVAL:
            return 0x02u;
        default:
            return 0x04u;
    }
}

/**
 ******************************************************************************
 * @brief      处理启动预充请求并回复受理结果。
 * @param[in,out] session:通信线程状态，当前未使用。
 * @param[in]  data:空载荷，当前未使用。
 * @retval     None
 ******************************************************************************
 */
static void handle_start(host_link_session_t *session, const uint8_t *data)
{
    uint8_t result = control_result(precharge_request_start());

    ARG_UNUSED(session);
    ARG_UNUSED(data);
    send_frame(HOST_LINK_CONTROL, 1u, &result, sizeof(result));
}

/**
 ******************************************************************************
 * @brief      处理停止请求并回复受理结果。
 * @param[in,out] session:通信线程持有的最新状态。
 * @param[in]  data:空载荷，当前未使用。
 * @retval     None
 ******************************************************************************
 */
static void handle_stop(host_link_session_t *session, const uint8_t *data)
{
    uint8_t result = 0u;

    ARG_UNUSED(session);
    ARG_UNUSED(data);
    precharge_request_stop();
    send_frame(HOST_LINK_CONTROL, 2u, &result, sizeof(result));
}

/**
 ******************************************************************************
 * @brief      受理主控清除故障指令，取消尚未发出的旧故障上报。
 * @param[in,out] session:通信线程持有的状态。
 * @param[in]  data:空载荷，未使用。
 * @retval     None
 ******************************************************************************
 */
static void handle_clear_faults(host_link_session_t *session, const uint8_t *data)
{
    const uint8_t result = 0;

    ARG_UNUSED(data);
    precharge_request_clear_faults();
    if (session->event == HOST_LINK_FAULT)
    {
        session->event = HOST_LINK_NO_EVENT;
    }
    send_frame(HOST_LINK_CONTROL, 3u, &result, sizeof(result));
}

/**
 ******************************************************************************
 * @brief      使用本次心跳应答中的 UTC 秒数校准本地 RTC。
 * @param[in,out] session:通信线程的心跳状态。
 * @param[in]  data:四字节小端 UTC Unix 秒数。
 * @retval     None
 ******************************************************************************
 */
static void handle_heartbeat(host_link_session_t *session, const uint8_t *data)
{
    int64_t now_ms = k_uptime_get();
    uint32_t unix_seconds = sys_get_le32(data);
    long long elapsed_ms = now_ms - session->heartbeat_sent_ms;
    int result;

    if (session->heartbeat_sent_ms < 0)
    {
        LOG_WRN("Heartbeat reply ignored: no pending request");
        return;
    }
    session->heartbeat_sent_ms = -1;
    session->heartbeat_guard_until_ms = 0;

    result = app_clock_sync(unix_seconds);

    if (result < 0)
    {
        LOG_WRN("Heartbeat reply %lld ms: RTC sync failed: %d (UTC=%u)", elapsed_ms, result,
                unix_seconds);
    }
    else if (result == 0)
    {
        LOG_INF("Heartbeat reply %lld ms: RTC difference under 5 s", elapsed_ms);
    }
    else
    {
        LOG_INF("Heartbeat reply %lld ms: RTC synchronized (UTC=%u)", elapsed_ms, unix_seconds);
    }
}

/**
 ******************************************************************************
 * @brief      匹配上报应答并提交接管确认。
 * @param[in,out] session:待确认的上报状态。
 * @param[in]  data:两字节小端上报序号。
 * @retval     None
 ******************************************************************************
 */
static void handle_report_ack(host_link_session_t *session, const uint8_t *data)
{
    int err;

    if (!session->handover_pending || sys_get_le16(data) != session->handover_sequence)
    {
        return;
    }
    session->handover_pending = false;
    err = precharge_confirm_handover(session->precharge_id);
    if (err)
    {
        LOG_WRN("Precharge report ACK rejected: %d", err);
    }
}

/** 完整帧业务处理函数，载荷长度已由分发入口检查。 */
typedef void (*host_link_handler_t)(host_link_session_t *session, const uint8_t *data);

/** 入站命令的分发规则。 */
typedef struct
{
    uint8_t type;                /**< 报文类型。 */
    uint8_t command;             /**< 功能码。 */
    size_t data_size;            /**< 固定载荷长度。 */
    host_link_handler_t handler; /**< 对应业务处理函数。 */
} host_link_command_t;

static const host_link_command_t commands[] = {
    {HOST_LINK_CONTROL, 1u, 0u, handle_start},
    {HOST_LINK_CONTROL, 2u, 0u, handle_stop},
    {HOST_LINK_CONTROL, 3u, 0u, handle_clear_faults},
    {HOST_LINK_HEARTBEAT, 1u, sizeof(uint32_t), handle_heartbeat},
    {HOST_LINK_REPORT, 1u, sizeof(uint16_t), handle_report_ack},
};

/**
 ******************************************************************************
 * @brief      查找命令，校验一次载荷长度后分发完整帧。
 * @param[in,out] session:通信线程持有的状态。
 * @retval     None
 ******************************************************************************
 */
static void handle_frame(host_link_session_t *session)
{
    const host_protocol_frame_t *frame = &session->parser.frame;
    const host_link_header_t *header = &frame->header;
    size_t length = sys_get_be16(header->length_be) - sizeof(*header);

    if (header->direction != 0x55u)
    {
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(commands); i++)
    {
        const host_link_command_t *entry = &commands[i];

        if (entry->type != header->type || entry->command != header->command)
        {
            continue;
        }
        if (length == entry->data_size)
        {
            entry->handler(session, frame->data);
        }
        return;
    }
}

/**
 ******************************************************************************
 * @brief      根据预充状态变化生成需确认的主动上报。
 * @param[in,out] session:通信线程持有的状态。
 * @retval     None
 ******************************************************************************
 */
static void update_report(host_link_session_t *session)
{
    precharge_event_t report;

    if (session->event == HOST_LINK_FAULT || precharge_take_event(&report) != 1)
    {
        return;
    }
    if (report.type == PRECHARGE_EVENT_CANCEL)
    {
        if (session->precharge_id == report.id)
        {
            session->handover_pending = false;
            if (session->event == HOST_LINK_PRECHARGE_DONE)
            {
                session->event = HOST_LINK_NO_EVENT;
            }
        }
        return;
    }
    if (report.type == PRECHARGE_EVENT_DONE)
    {
        session->precharge_id = report.id;
        session->handover_pending = false;
        session->event = HOST_LINK_PRECHARGE_DONE;
        session->report_size = offsetof(host_link_report_data_t, cause);
    }
    else if (report.type == PRECHARGE_EVENT_MP10_FAULT)
    {
        session->event = HOST_LINK_FAULT;
        session->report.cause = 0x03u;
        session->report.channel_mask = report.channels;
        session->report_size = offsetof(host_link_report_data_t, currents);
    }
    else if (report.type == PRECHARGE_EVENT_LEAKAGE)
    {
        size_t length = 0;

        session->event = HOST_LINK_FAULT;
        session->report.cause = 0x04u;
        session->report.channel_mask = report.channels;
        for (unsigned int i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
        {
            if (report.channels & BIT(i))
            {
                int32_t current = CLAMP(report.current[i], INT16_MIN, INT16_MAX);

                sys_put_le16((uint16_t)current, &session->report.currents[length]);
                length += sizeof(uint16_t);
            }
        }
        session->report_size = offsetof(host_link_report_data_t, currents) + length;
    }
    else
    {
        session->event = HOST_LINK_FAULT;
        session->report.cause = (uint8_t)report.cause;
        switch (report.cause)
        {
            case PRECHARGE_CAUSE_INPUT:
            case PRECHARGE_CAUSE_VOLTAGE:
                session->report.channel_mask = report.channels;
                session->report_size = offsetof(host_link_report_data_t, currents);
                break;
            default:
                session->report.cause = 0x05u;
                session->report_size = offsetof(host_link_report_data_t, channel_mask);
                break;
        }
    }

    session->sequence++;
    sys_put_le16(session->sequence, session->report.sequence_le);
    session->report.event = (uint8_t)session->event;
}

/**
 ******************************************************************************
 * @brief      在总线空闲时发送一次上报，完成上报发送成功后等待接管应答。
 * @param[in,out] session:通信线程持有的状态。
 * @retval     None
 ******************************************************************************
 */
static void send_report(host_link_session_t *session)
{
    int err;

    if (session->event == HOST_LINK_NO_EVENT)
    {
        return;
    }

    err = send_frame(HOST_LINK_REPORT, 1u, &session->report, session->report_size);
    if (!err && session->event == HOST_LINK_PRECHARGE_DONE)
    {
        session->handover_sequence = session->sequence;
        session->handover_pending = true;
    }
    session->event = HOST_LINK_NO_EVENT;
}

/**
 ******************************************************************************
 * @brief      接收主控报文并在总线空闲时主动上报。
 * @param[in]  arg1:Zephyr 保留参数，未使用。
 * @param[in]  arg2:Zephyr 保留参数，未使用。
 * @param[in]  arg3:Zephyr 保留参数，未使用。
 * @retval     None
 ******************************************************************************
 */
static void host_link_entry(void *arg1, void *arg2, void *arg3)
{
    host_link_session_t session = {.heartbeat_sent_ms = -1};
    uint8_t bytes[32];
    int channel;

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    channel = board_watchdog_register(HOST_LINK_WATCHDOG_TIMEOUT_MS);
    if (channel < 0)
    {
        LOG_ERR("RS485 watchdog registration failed: %d", channel);
        return;
    }

    for (;;)
    {
        ssize_t received = app_serial_rx_read(APP_SERIAL_RS485, bytes, sizeof(bytes),
                                              K_MSEC(HOST_LINK_RX_IDLE_MS));

        if (received < 0)
        {
            LOG_ERR("RS485 receive failed: %d", (int)received);
            return;
        }

        update_report(&session);
        for (ssize_t i = 0; i < received; i++)
        {
            if (host_protocol_receive(&session.parser, bytes[i], k_uptime_get()))
            {
                do
                {
                    LOG_HEXDUMP_INF(&session.parser.frame,
                                    sys_get_be16(session.parser.frame.header.length_be),
                                    "RS485 RX");
                    handle_frame(&session);
                } while (host_protocol_next(&session.parser));
            }
        }
        if (!received)
        {
            host_protocol_expire(&session.parser, k_uptime_get());
            if (!session.parser.used || session.parser.ready)
            {
                int64_t now_ms = k_uptime_get();

                if (now_ms >= session.heartbeat_guard_until_ms)
                {
                    if (now_ms >= session.heartbeat_at_ms)
                    {
                        session.heartbeat_at_ms = now_ms + HOST_LINK_HEARTBEAT_MS;
                        if (!send_frame(HOST_LINK_HEARTBEAT, 1u, NULL, 0))
                        {
                            session.heartbeat_sent_ms = k_uptime_get();
                            session.heartbeat_guard_until_ms =
                                session.heartbeat_sent_ms + HOST_LINK_HEARTBEAT_GUARD_MS;
                        }
                    }
                    else
                    {
                        send_report(&session);
                    }
                }
            }
        }

        if (board_watchdog_feed(channel))
        {
            return;
        }
    }
}

/**
 ******************************************************************************
 * @brief      启动 RS485 接收与应答线程。
 * @retval     None
 * @details    在串口和应用参数就绪后调用一次。
 ******************************************************************************
 */
void host_link_start(void)
{
    k_thread_create(&host_link_thread, host_link_stack, K_THREAD_STACK_SIZEOF(host_link_stack),
                    host_link_entry, NULL, NULL, NULL, HOST_LINK_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&host_link_thread, "host_link");
}

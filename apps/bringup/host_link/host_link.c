#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

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
/** RS485 线程进度上报的超时，单位为毫秒。 */
#define HOST_LINK_WATCHDOG_TIMEOUT_MS 3000u
/** 上报重发间隔，单位为毫秒，联调值。 */
#define HOST_LINK_RETRY_MS 250u
/** 普通故障上报的最大发送尝试数，联调值。 */
#define HOST_LINK_FAULT_ATTEMPTS 3u
typedef enum
{
    HOST_LINK_NO_EVENT = 0,       /**< 当前没有待确认上报。 */
    HOST_LINK_PRECHARGE_DONE = 1, /**< 电压检查通过。 */
    HOST_LINK_FAULT = 2,          /**< 预充或运行故障。 */
} host_link_event_e;

typedef enum
{
    HOST_LINK_HEARTBEAT = 2, /**< 主控心跳。 */
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
    uint32_t precharge_id; /**< 已处理快照的内部事务编号，不在线协议中传输。 */
    precharge_status_e status;      /**< 已处理的预充状态。 */
    precharge_cause_e cause;        /**< 已处理的故障原因。 */
    host_link_event_e event;        /**< 当前等待确认的上报类型。 */
    uint16_t sequence;              /**< 上报序号，重发时保持一致。 */
    host_link_report_data_t report; /**< 待重发的上报 Data。 */
    size_t report_size;             /**< 待重发 Data 长度。 */
    int64_t retry_at_ms;            /**< 最早可以重发的时间。 */
    unsigned int attempts;          /**< 当前上报的发送尝试数。 */
    bool sent;                      /**< 当前上报已成功发出至少一次。 */
} host_link_session_t;

LOG_MODULE_REGISTER(host_link, LOG_LEVEL_WRN);

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
    uint8_t result = control_result(precharge_begin());

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
    precharge_stop();
    send_frame(HOST_LINK_CONTROL, 2u, &result, sizeof(result));
}

/**
 ******************************************************************************
 * @brief      接收主控心跳并返回空载荷应答。
 * @param[in,out] session:通信线程状态，当前未使用。
 * @param[in]  data:主控时间戳，目前仅受理，不执行对时。
 * @retval     None
 ******************************************************************************
 */
static void handle_heartbeat(host_link_session_t *session, const uint8_t *data)
{
    ARG_UNUSED(data);
    ARG_UNUSED(session);
    send_frame(HOST_LINK_HEARTBEAT, 1u, NULL, 0);
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
    if (!session->sent || session->event == HOST_LINK_NO_EVENT
        || sys_get_le16(data) != session->sequence)
    {
        return;
    }
    if (session->event == HOST_LINK_PRECHARGE_DONE)
    {
        int err = precharge_confirm();

        if (err)
        {
            LOG_WRN("Precharge report ACK rejected: %d", err);
        }
    }
    session->event = HOST_LINK_NO_EVENT;
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
    precharge_report_t report;

    if (precharge_get_report(&report))
    {
        return;
    }
    if (report.id == session->precharge_id && report.status == session->status
        && report.cause == session->cause)
    {
        return;
    }

    session->precharge_id = report.id;
    session->status = report.status;
    session->cause = report.cause;
    session->event = HOST_LINK_NO_EVENT;
    if (report.status == PRECHARGE_WAIT_HANDOVER)
    {
        session->event = HOST_LINK_PRECHARGE_DONE;
        session->report_size = offsetof(host_link_report_data_t, cause);
    }
    else if (report.status == PRECHARGE_FAILED)
    {
        session->event = HOST_LINK_FAULT;
        session->report.cause = (uint8_t)report.cause;
        switch (report.cause)
        {
            case PRECHARGE_CAUSE_INPUT:
            case PRECHARGE_CAUSE_VOLTAGE:
                session->report.channel_mask = report.failed_channels;
                session->report_size = sizeof(session->report);
                break;
            case PRECHARGE_CAUSE_ADC:
            case PRECHARGE_CAUSE_GPIO:
            case PRECHARGE_CAUSE_RELEASE:
                session->report.channel_mask = 0u;
                session->report_size = sizeof(session->report);
                break;
            default:
                session->report_size = offsetof(host_link_report_data_t, channel_mask);
                break;
        }
    }
    else
    {
        return;
    }

    session->sequence++;
    sys_put_le16(session->sequence, session->report.sequence_le);
    session->report.event = (uint8_t)session->event;
    session->retry_at_ms = 0;
    session->attempts = 0;
    session->sent = false;
}

/**
 ******************************************************************************
 * @brief      在总线空闲时发送或重发待确认的主动上报。
 * @param[in,out] session:通信线程持有的状态。
 * @retval     None
 ******************************************************************************
 */
static void send_report(host_link_session_t *session)
{
    int64_t now_ms = k_uptime_get();

    if (session->event == HOST_LINK_NO_EVENT || now_ms < session->retry_at_ms)
    {
        return;
    }

    if (session->event == HOST_LINK_FAULT && session->attempts >= HOST_LINK_FAULT_ATTEMPTS)
    {
        LOG_WRN("RS485 fault report unacknowledged: %u", session->sequence);
        session->event = HOST_LINK_NO_EVENT;
        return;
    }

    if (!send_frame(HOST_LINK_REPORT, 1u, &session->report, session->report_size))
    {
        session->sent = true;
    }
    session->attempts++;
    session->retry_at_ms = k_uptime_get() + HOST_LINK_RETRY_MS;
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
    host_link_session_t session = {0};
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
                    handle_frame(&session);
                } while (host_protocol_next(&session.parser));
            }
        }
        if (!received)
        {
            host_protocol_expire(&session.parser, k_uptime_get());
            if (!session.parser.used || session.parser.ready)
            {
                send_report(&session);
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
}

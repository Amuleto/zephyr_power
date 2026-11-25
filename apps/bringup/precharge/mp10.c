#include <errno.h>
#include <inttypes.h>

#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "mp10.h"

/** 经典 CAN 测量帧的 DLC，对应八字节。 */
#define MP10_MEASUREMENT_DLC 8u
/** 测量帧判为过期的年龄阈值，单位为毫秒。 */
#define MP10_TIMEOUT_MS 100u
/** 剩余电流为零时的协议编码值。 */
#define MP10_ZERO_OFFSET INT64_C(0x80000000)
/** 每轮最多处理的 CAN 收发回调事件数，限制任务单轮开销。 */
#define MP10_EVENT_BUDGET 8u
/** CAN 回调事件队列容量，满时丢弃新事件并累计告警。 */
#define MP10_EVENT_QUEUE_DEPTH 32u
/** 周期测量报文的日志间隔，单位为毫秒。 */
#define MP10_MEASUREMENT_LOG_INTERVAL_MS 1000u
/** 自检命令字，同时出现在设备的应答中。 */
#define MP10_SELFTEST_COMMAND 0x44u
/** 上电收到首条有效测量帧后执行两项自检。 */
#define MP10_SELFTEST_AUTO_TYPE 3u
/** 等待 CAN 发送回调的上限，单位为毫秒。 */
#define MP10_SELFTEST_TX_TIMEOUT_MS 2000u
/** 自检应答的联调时限，单位为毫秒。实板约 200 ms 才收到应答。 */
#define MP10_SELFTEST_REPLY_TIMEOUT_MS 500u
/** 自检后屏蔽测量的联调时间，单位为毫秒，待实板核验。 */
#define MP10_SELFTEST_RECOVERY_MS 100u

LOG_MODULE_REGISTER(mp10, LOG_LEVEL_INF);

/** CAN 回调交付给预充线程的记录，不借用任务栈或驱动缓冲区。 */
typedef struct
{
    int64_t received_ms; /**< 回调时间，单位为毫秒。 */
    unsigned int channel; /**< 零表示接收帧；1～4 表示对应通道的发送完成事件。 */
    union
    {
        struct can_frame frame; /**< channel 为零时有效，保存接收帧副本。 */
        int tx_result; /**< channel 非零时有效，保存发送结果。 */
    };
} mp10_event_t;

/* Zephyr 宏同时生成静态存储和内核对象，队列只在本文件使用，不对外声明。 */
K_MSGQ_DEFINE(event_queue, sizeof(mp10_event_t), MP10_EVENT_QUEUE_DEPTH, 8);
static atomic_t events_dropped;

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

/** 四台设备各自的测量、请求和应答 ID。 */
typedef struct
{
    uint32_t measurement_id;
    uint32_t request_id;
    uint32_t response_id;
} mp10_channel_ids_t;

static const mp10_channel_ids_t channel_ids[MP10_CHANNEL_COUNT] = {
    {0x023du, 0x046au, 0x046bu},
    {0x023eu, 0x047au, 0x047bu},
    {0x023fu, 0x048au, 0x048bu},
    {0x0240u, 0x049au, 0x049bu},
};

/**
 ******************************************************************************
 * @brief      从 CAN 中断交付带时间戳的接收帧。
 * @param[in]  dev:接收设备，未使用。
 * @param[in]  frame:驱动提供的报文，在回调内复制。
 * @param[in]  user_data:回调参数，未使用。
 * @retval     None
 ******************************************************************************
 */
static void receive_frame(const struct device *dev, struct can_frame *frame, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    if (frame->flags != CAN_FRAME_IDE)
    {
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(channel_ids); i++)
    {
        const mp10_channel_ids_t *ids = &channel_ids[i];

        if ((frame->dlc == MP10_MEASUREMENT_DLC && frame->id == ids->measurement_id)
            || (frame->dlc == 2u && frame->id == ids->response_id))
        {
            const mp10_event_t event = {.frame = *frame, .received_ms = k_uptime_get()};

            if (k_msgq_put(&event_queue, &event, K_NO_WAIT))
            {
                atomic_inc(&events_dropped);
            }
            return;
        }
    }
}

/**
 ******************************************************************************
 * @brief      安装 MP10 的 CAN 接收过滤器。
 * @retval     0:过滤器安装成功。
 * @retval     负值:CAN 设备或过滤器配置失败。
 ******************************************************************************
 */
int mp10_init(void)
{
    const struct can_filter extended = {.flags = CAN_FILTER_IDE};
    int filter;

    if (!device_is_ready(can_dev))
    {
        return -ENODEV;
    }

    /* 八种报文共用一个扩展帧过滤器，回调按通道 ID 筛选。 */
    filter = can_add_rx_filter(can_dev, receive_frame, NULL, &extended);
    return MIN(filter, 0);
}

/**
 ******************************************************************************
 * @brief      将自检发送结果交给预充线程。
 * @param[in]  dev:发送设备，未使用。
 * @param[in]  error:驱动返回的发送结果。
 * @param[in]  user_data:编码为指针值的通道号 1～4，不指向任务内存。
 * @retval     None
 * @details    每路每次上电仅发送一次，通道号可唯一关联迟到的发送结果。
 ******************************************************************************
 */
static void selftest_sent(const struct device *dev, int error, void *user_data)
{
    const mp10_event_t event =
    {
        .received_ms = k_uptime_get(),
        .channel = (unsigned int)(uintptr_t)user_data,
        .tx_result = error,
    };

    ARG_UNUSED(dev);

    if (k_msgq_put(&event_queue, &event, K_NO_WAIT))
    {
        atomic_inc(&events_dropped);
    }
}

/**
 ******************************************************************************
 * @brief      根据测量 ID 解码电流和故障位，并记录接收时间。
 * @param[in,out] task:预充线程持有的 MP10 状态。
 * @param[in]  event:已校验格式和长度的测量报文。
 * @retval     None
 ******************************************************************************
 */
static void parse_measurement(mp10_context_t *task, const mp10_event_t *event)
{
    const struct can_frame *frame = &event->frame;

    for (size_t i = 0; i < ARRAY_SIZE(channel_ids); i++)
    {
        if (frame->id != channel_ids[i].measurement_id)
        {
            continue;
        }

        mp10_channel_t *channel = &task->channels[i];
        mp10_sample_t *sample = &channel->sample;
        uint16_t errors = sys_get_be16(&frame->data[4]);

        sample->received_ms = event->received_ms;
        sample->current_deci_ma = (int64_t)sys_get_be32(frame->data) - MP10_ZERO_OFFSET;
        sample->error_code = errors & 0x7fffu;
        sample->fault = (errors >> 15) != 0u;
        channel->online = true;
        sample->valid = channel->selftest_result == MP10_SELFTEST_PASS && !sample->fault
                        && event->received_ms >= channel->measurement_from_ms;
        return;
    }
}

/**
 ******************************************************************************
 * @brief      保存当前自检设备在有效时间内发出的首条有效应答。
 * @param[in,out] task:预充线程持有的 MP10 状态。
 * @param[in]  event:已校验格式和长度的应答报文。
 * @retval     None
 ******************************************************************************
 */
static void parse_selftest_reply(mp10_context_t *task, const mp10_event_t *event)
{
    const struct can_frame *frame = &event->frame;

    if (task->state != MP10_TEST_WAIT_TX && task->state != MP10_TEST_WAIT_REPLY)
    {
        return;
    }

    if (frame->id != channel_ids[task->active - 1u].response_id || task->reply
        || event->received_ms < task->request_ms)
    {
        return;
    }

    if (frame->data[1] == MP10_SELFTEST_COMMAND
        && (frame->data[0] == 0x51u || frame->data[0] == 0x7fu))
    {
        task->reply = frame->data[0];
        task->reply_ms = event->received_ms;
    }
}

/**
 ******************************************************************************
 * @brief      按扩展数据帧的长度分派测量报文和自检应答。
 * @param[in,out] task:预充线程持有的 MP10 状态。
 * @param[in]  event:接收报文及其时间戳。
 * @retval     None
 ******************************************************************************
 */
static void parse_frame(mp10_context_t *task, const mp10_event_t *event)
{
    /* 队列入口已限定扩展数据帧，并校验了 ID 和长度。 */
    if (event->frame.dlc == MP10_MEASUREMENT_DLC)
    {
        parse_measurement(task, event);
    }
    else
    {
        parse_selftest_reply(task, event);
    }
}

/**
 ******************************************************************************
 * @brief      选出首次上线且尚未自检的设备，并提交一次自检请求。
 * @param[in,out] task:预充线程持有的 MP10 状态。
 * @param[in]  now_ms:当前开机时间，单位为毫秒。
 * @retval     None
 ******************************************************************************
 */
static void start_next_selftest(mp10_context_t *task, int64_t now_ms)
{
    for (size_t i = 0; i < ARRAY_SIZE(channel_ids); i++)
    {
        mp10_channel_t *channel = &task->channels[i];
        const struct can_frame frame = {
            .id = channel_ids[i].request_id,
            .dlc = 2,
            .flags = CAN_FRAME_IDE,
            .data = {MP10_SELFTEST_COMMAND, MP10_SELFTEST_AUTO_TYPE},
        };
        unsigned int number = (unsigned int)i + 1u;
        int err;

        if (!channel->online || channel->selftest_result != MP10_SELFTEST_NOT_RUN)
        {
            continue;
        }

        LOG_INF("%x 4403", frame.id);
        err = can_send(can_dev, &frame, K_NO_WAIT, selftest_sent, (void *)(uintptr_t)number);
        if (err)
        {
            channel->selftest_result = MP10_SELFTEST_TX_FAILED;
            LOG_ERR("MP10 CH%u self-test enqueue failed: %d", number, err);
            return;
        }

        channel->selftest_result = MP10_SELFTEST_PENDING;
        task->active = number;
        task->reply = 0;
        task->request_ms = now_ms;
        task->deadline_ms = now_ms + MP10_SELFTEST_TX_TIMEOUT_MS;
        channel->sample.valid = false;
        task->state = MP10_TEST_WAIT_TX;
        return;
    }
}

/**
 ******************************************************************************
 * @brief      推进一个在途自检事务。
 * @param[in,out] task:预充线程持有的 MP10 状态。
 * @param[in]  now_ms:当前开机时间，单位为毫秒。
 * @retval     None
 *
 * @details    首次上线后向该路请求 ID 发 44 03，执行 3.3 mA 和 1.65 mA 自检。
 *             该路应答 ID 上的 51 44 表示通过，7F 44 表示失败。
 *             每次上电仅尝试一次，自检结束后等待注入电流消退。
 *             发送超时后继续下一路，迟到回调按通道号丢弃；计时日志从提交请求起算。
 ******************************************************************************
 */
static void advance_selftest(mp10_context_t *task, int64_t now_ms)
{
    mp10_selftest_result_e result;
    mp10_channel_t *channel;

    switch (task->state)
    {
        case MP10_TEST_IDLE:
            start_next_selftest(task, now_ms);
            return;

        case MP10_TEST_WAIT_TX:
            if (now_ms >= task->deadline_ms)
            {
                task->channels[task->active - 1u].selftest_result = MP10_SELFTEST_TIMEOUT;
                LOG_ERR("MP10 CH%u self-test timed out waiting for TX", task->active);
                task->state = MP10_TEST_IDLE;
            }
            return;

        case MP10_TEST_WAIT_REPLY:
            break;

        case MP10_TEST_RECOVER:
            if (now_ms >= task->channels[task->active - 1u].measurement_from_ms)
            {
                task->state = MP10_TEST_IDLE;
            }
            return;

        default:
            return;
    }

    if (task->reply && task->reply_ms <= task->deadline_ms)
    {
        result = MP10_SELFTEST_FAIL;
        if (task->reply == 0x51u)
        {
            result = MP10_SELFTEST_PASS;
        }
        LOG_INF("MP10 CH%u self-test %s (%" PRId64 " ms)", task->active,
                result == MP10_SELFTEST_PASS ? "PASS" : "FAIL", task->reply_ms - task->request_ms);
    }
    else if (now_ms >= task->deadline_ms)
    {
        result = MP10_SELFTEST_TIMEOUT;
        LOG_ERR("MP10 CH%u self-test timed out waiting for reply", task->active);
    }
    else
    {
        return;
    }

    channel = &task->channels[task->active - 1u];
    channel->selftest_result = result;
    channel->measurement_from_ms = now_ms + MP10_SELFTEST_RECOVERY_MS;
    task->state = MP10_TEST_RECOVER;
}

/**
 ******************************************************************************
 * @brief      将当前通道的发送结果交给自检状态机。
 * @param[in,out] task:预充线程持有的 MP10 状态。
 * @param[in]  event:包含通道号和回调时间的发送完成记录。
 * @retval     None
 * @details    超时后的迟到回调只关联原通道，不改变下一路事务。
 ******************************************************************************
 */
static void complete_selftest_tx(mp10_context_t *task, const mp10_event_t *event)
{
    if (task->state != MP10_TEST_WAIT_TX || event->channel != task->active
        || event->received_ms > task->deadline_ms)
    {
        return;
    }

    LOG_INF("MP10 CH%u self-test TX result=%d", task->active, event->tx_result);
    if (event->tx_result)
    {
        task->channels[task->active - 1u].selftest_result = MP10_SELFTEST_TX_FAILED;
        task->state = MP10_TEST_IDLE;
        return;
    }

    task->deadline_ms = event->received_ms + MP10_SELFTEST_REPLY_TIMEOUT_MS;
    task->state = MP10_TEST_WAIT_REPLY;
}

/**
 ******************************************************************************
 * @brief      打印接收到的 CAN ID 和原始数据。
 * @param[in,out] task:持有四路共用日志限流时间的线程状态。
 * @param[in]  frame:待打印的 CAN 报文。
 * @retval     None
 ******************************************************************************
 */
static void log_rx_frame(mp10_context_t *task, const struct can_frame *frame)
{
    char data[2u * sizeof(frame->data) + 1u] = "";

    /* 四路测量共用每秒一条的限流，自检应答不受此限制。 */
    if (frame->dlc == MP10_MEASUREMENT_DLC)
    {
        int64_t now_ms = k_uptime_get();

        if (now_ms < task->next_measurement_log_ms)
        {
            return;
        }
        task->next_measurement_log_ms = now_ms + MP10_MEASUREMENT_LOG_INTERVAL_MS;
    }

    bin2hex(frame->data, frame->dlc, data, sizeof(data));

    LOG_INF("%x %s", frame->id, data);
}

/**
 ******************************************************************************
 * @brief      有界地处理 CAN 回调事件，解析接收帧并推进发送状态。
 * @param[in,out] task:预充线程持有的 MP10 状态。
 * @retval     None
 ******************************************************************************
 */
static void process_events(mp10_context_t *task)
{
    mp10_event_t event;
    atomic_val_t dropped;

    for (unsigned int i = 0; i < MP10_EVENT_BUDGET; i++)
    {
        if (k_msgq_get(&event_queue, &event, K_NO_WAIT))
        {
            break;
        }

        if (event.channel)
        {
            complete_selftest_tx(task, &event);
        }
        else
        {
            parse_frame(task, &event);
            log_rx_frame(task, &event.frame);
        }
    }

    dropped = atomic_set(&events_dropped, 0);
    if (dropped)
    {
        LOG_WRN("CAN events dropped %ld", (long)dropped);
    }
}

/**
 ******************************************************************************
 * @brief      将超时未上报的通道标为离线并撤销测量有效性。
 * @param[in,out] task:预充线程持有的 MP10 状态。
 * @param[in]  now_ms:当前开机时间，单位为毫秒。
 * @retval     None
 ******************************************************************************
 */
static void update_online(mp10_context_t *task, int64_t now_ms)
{
    for (size_t i = 0; i < ARRAY_SIZE(task->channels); i++)
    {
        mp10_channel_t *channel = &task->channels[i];

        if (channel->online && now_ms - channel->sample.received_ms >= MP10_TIMEOUT_MS)
        {
            channel->online = false;
            channel->sample.valid = false;
        }
    }
}

/**
 ******************************************************************************
 * @brief      处理接收、更新在线状态并推进上电自检。
 * @param[in,out] task:初始化为零的线程私有状态。
 * @retval     None
 ******************************************************************************
 */
void mp10_task(mp10_context_t *task)
{
    int64_t now_ms;

    process_events(task);
    now_ms = k_uptime_get();
    update_online(task, now_ms);
    advance_selftest(task, now_ms);
}

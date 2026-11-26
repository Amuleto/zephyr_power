#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "board_gpio.h"
#include "board_watchdog.h"
#include "mp10.h"
#include "precharge.h"
#include "precharge_io.h"

/** 预充回读的联调间隔，单位为毫秒，量产值依据实际检测时限确定。 */
#define PRECHARGE_TASK_INTERVAL_MS 20u
/** 心跳灯相邻两次翻转的间隔，单位为毫秒。 */
#define PRECHARGE_LED_INTERVAL_MS 500u
/** 预充线程沿用此前主线程的栈预算，单位为字节。 */
#define PRECHARGE_TASK_STACK_SIZE 1024u
/** 预充线程优先级，高于 RS485 通信线程。 */
#define PRECHARGE_TASK_PRIORITY 0

/** Precharge 线程进度上报的超时，单位为毫秒。 */
#define PRECHARGE_WATCHDOG_TIMEOUT_MS 3000u

LOG_MODULE_REGISTER(precharge, LOG_LEVEL_WRN);

static K_THREAD_STACK_DEFINE(precharge_stack, PRECHARGE_TASK_STACK_SIZE);
static struct k_thread precharge_thread;

/** U46 闭合后 ADC 稳定时间，联调值，单位为毫秒。 */
#define PRECHARGE_ADC_SETTLE_MS 20u
/** U46 释放后 MP10 稳定时间，联调值，单位为毫秒。 */
#define PRECHARGE_MP10_SETTLE_MS 100u
/** MP10 持续未收到测量帧后上报离线故障的联调门限，单位为毫秒。 */
#define PRECHARGE_MP10_OFFLINE_REPORT_MS 300u
/** 吸合前的输入电压观察窗口，单位为毫秒。 */
#define PRECHARGE_INPUT_WINDOW_MS 500u
/** 2T 检查时刻，单位为毫秒，T 暂按 700 ms。 */
#define PRECHARGE_CHECK_MS 1400u
/** 预充完成后等待接管确认的上限，单位为毫秒。 */
#define PRECHARGE_HANDOVER_MS 3000u
/** 输入电压下限，单位为高压侧 mV。 */
#define PRECHARGE_MIN_INPUT_MV 200000
/** 允许的前后绝对压差上限，单位为高压侧 mV。 */
#define PRECHARGE_MAX_DELTA_MV 30000
/** 前置输入电压波动上限，以窗口最大值为基准。 */
#define PRECHARGE_INPUT_SPREAD_PERCENT 10

/** 仅用于断开高压回路的台架联调。置 1 后忽略输入波动与 2T 电压判定失败。 */
#define PRECHARGE_BENCH_BYPASS_VOLTAGE_CHECK 1

typedef enum
{
    PRECHARGE_PHASE_IDLE,   /**< 无待执行的电压检查。 */
    PRECHARGE_PHASE_SETTLE, /**< 等待 ADC 参考稳定。 */
    PRECHARGE_PHASE_INPUT,  /**< 采集吸合前输入。 */
    PRECHARGE_PHASE_CHARGE, /**< 等待 2T 检查。 */
    PRECHARGE_PHASE_HANDOVER, /**< 等待主板确认。 */
    PRECHARGE_PHASE_MONITOR, /**< 旁路运行监测。 */
} precharge_phase_e;

/** 由预充线程独占的本次运行状态。 */
typedef struct
{
    uint32_t id; /**< 本轮本地标识。 */
    precharge_cause_e cause; /**< 当前故障原因。 */
    uint8_t failed_channels; /**< 故障通道位图。 */
    int release_error; /**< 首个输出撤销错误。 */
    precharge_phase_e phase;   /**< 电压检查阶段。 */
    int64_t leakage_from_ms; /**< PB13 释放后的有效采样起点。 */
    int64_t over_since[PRECHARGE_CHANNEL_COUNT]; /**< 各路超限起点。 */
    int64_t last_sample[PRECHARGE_CHANNEL_COUNT]; /**< 各路上次超限采样时刻。 */
    uint8_t over; /**< 累计超限的通道。 */
    uint8_t reported; /**< 本次超限已上报的通道。 */
    uint8_t mp10_faults; /**< 最近一轮确认的 MP10 故障通道。 */
    int64_t deadline_ms;       /**< 当前阶段的截止时间。 */
    int32_t min_mv[PRECHARGE_CHANNEL_COUNT]; /**< 前置采样窗口最小值。 */
    int32_t max_mv[PRECHARGE_CHANNEL_COUNT]; /**< 前置采样窗口最大值。 */
} precharge_run_t;

/** 预充和通信线程在持锁时交换的指令和快照。 */
typedef struct
{
    precharge_event_t events[PRECHARGE_EVENT_COUNT]; /**< 分槽保存完成、取消和故障。 */
    uint8_t pending; /**< 已占用事件槽位图。 */
    precharge_phase_e phase; /**< 供接口受理判断的当前阶段。 */
    bool release_failed; /**< 最近一次撤销失败。 */
    uint32_t completion_id; /**< 当前可确认轮次。 */
    int64_t handover_by_ms;    /**< 接管确认最晚受理时刻。 */
    bool ready;                /**< 预充线程已就绪。 */
    bool start;                /**< 新预充请求待处理。 */
    bool confirm;              /**< 已收到有效接管确认。 */
    bool clear_faults; /**< 主控清除故障请求。 */
    bool stop;                 /**< 停止请求待处理。 */
} precharge_exchange_t;





static K_MUTEX_DEFINE(precharge_lock);
static precharge_exchange_t exchange;

/**
 ******************************************************************************
 * @brief      撤销所有预充输出并发布本次运行的结束原因。
 * @param[in,out] run:预充线程持有的事务状态。
 * @param[in]  phase:输出释放成功时记录的状态。
 * @param[in]  error:失败原因，正常结束时为零。
 * @retval     None
 ******************************************************************************
 */
static void finish_run(precharge_run_t *run, precharge_phase_e phase, int error)
{
    int relay_error = precharge_set_relays(false);
    int pe_error = app_board_gpio_write(BOARD_GPIO_OUT_PE, false);

    run->release_error = relay_error;
    if (!relay_error)
    {
        run->release_error = pe_error;
    }
    if (run->release_error)
    {
        run->cause = PRECHARGE_CAUSE_RELEASE;
        phase = PRECHARGE_PHASE_IDLE;
    }
    run->phase = phase;
    if (error || run->release_error)
    {
        LOG_ERR("Precharge %u ended: error=%d, release=%d, channels=0x%x", run->id, error,
                run->release_error, run->failed_channels);
    }
}

/**
 ******************************************************************************
 * @brief      初始化预充事务并接通 ADC 采样参考。
 * @param[in,out] run:预充线程持有的事务状态。
 * @retval     None
 ******************************************************************************
 */
static void begin_run(precharge_run_t *run)
{
    uint32_t id = run->id + 1u;
    int err;

    if (id == 0u)
    {
        id = 1u;
    }
    uint8_t mp10_faults = run->mp10_faults;

    *run = (precharge_run_t){0};
    run->mp10_faults = mp10_faults;
    run->id = id;
    err = app_board_gpio_write(BOARD_GPIO_OUT_PE, true);
    if (err)
    {
        run->cause = PRECHARGE_CAUSE_GPIO;
        finish_run(run, PRECHARGE_PHASE_IDLE, err);
        return;
    }

    for (size_t i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        run->min_mv[i] = INT32_MAX;
        run->max_mv[i] = INT32_MIN;
    }
    run->deadline_ms = k_uptime_get() + PRECHARGE_ADC_SETTLE_MS;
    run->phase = PRECHARGE_PHASE_SETTLE;
}

/**
 ******************************************************************************
 * @brief      读取四对电压并记录无效通道。
 * @param[in,out] run:故障原因和通道输出。
 * @param[out] adc:完整电压快照。
 * @retval     0:四路有效。
 * @retval     负值:采样失败或至少一路无效。
 ******************************************************************************
 */
static int read_valid_snapshot(precharge_run_t *run, precharge_adc_snapshot_t *adc)
{
    const uint8_t all = BIT(PRECHARGE_CHANNEL_COUNT) - 1u;
    int err = precharge_read_snapshot(adc);

    if (err)
    {
        run->cause = PRECHARGE_CAUSE_ADC;
        return err;
    }
    run->failed_channels = all & ~adc->valid_channels;
    if (run->failed_channels)
    {
        run->cause = PRECHARGE_CAUSE_ADC;
        return -ERANGE;
    }
    return 0;
}

/**
 ******************************************************************************
 * @brief      采样输入电压，并在窗口结束后决定能否吸合四路。
 * @param[in,out] run:预充线程持有的事务状态。
 * @param[in]  now_ms:当前开机时间，单位为毫秒。
 * @retval     None
 ******************************************************************************
 */
static void check_input(precharge_run_t *run)
{
    precharge_adc_snapshot_t adc;
    int err = read_valid_snapshot(run, &adc);

    if (err)
    {
        finish_run(run, PRECHARGE_PHASE_IDLE, err);
        return;
    }

    for (size_t i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        int32_t voltage = adc.input_mv[i];

        run->min_mv[i] = MIN(run->min_mv[i], voltage);
        run->max_mv[i] = MAX(run->max_mv[i], voltage);
    }

    if (adc.read_ms < run->deadline_ms)
    {
        return;
    }

    for (size_t i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        int32_t spread = run->max_mv[i] - run->min_mv[i];

        int32_t baseline = run->max_mv[i];

        /* 用窗口内绝对值较大的端点计算波动，零电压恒定时波动为零。 */
        baseline = MAX(baseline, -run->min_mv[i]);
        if (spread && spread * 100 >= baseline * PRECHARGE_INPUT_SPREAD_PERCENT)
        {
            run->failed_channels |= (uint8_t)BIT(i);
        }
    }
    if (run->failed_channels)
    {
        if (!PRECHARGE_BENCH_BYPASS_VOLTAGE_CHECK)
        {
            run->cause = PRECHARGE_CAUSE_INPUT;
            finish_run(run, PRECHARGE_PHASE_IDLE, -EDOM);
            return;
        }
        LOG_WRN("Bench bypass: unstable inputs 0x%x", run->failed_channels);
        run->failed_channels = 0;
    }

    err = precharge_set_relays(true);
    if (err)
    {
        run->cause = PRECHARGE_CAUSE_GPIO;
        finish_run(run, PRECHARGE_PHASE_IDLE, err);
        return;
    }

    run->deadline_ms = k_uptime_get() + PRECHARGE_CHECK_MS;
    run->phase = PRECHARGE_PHASE_CHARGE;
}

/**
 ******************************************************************************
 * @brief      到 2T 时检查一次电压，释放 U46，并准备通知主板。
 * @param[in,out] run:预充线程持有的事务状态。
 * @retval     None
 ******************************************************************************
 */
static void check_charge(precharge_run_t *run)
{
    precharge_adc_snapshot_t adc;
    int err = read_valid_snapshot(run, &adc);

    if (err)
    {
        finish_run(run, PRECHARGE_PHASE_IDLE, err);
        return;
    }

    for (size_t i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        int32_t input = adc.input_mv[i];
        int32_t delta = input - adc.load_mv[i];

        if (input <= PRECHARGE_MIN_INPUT_MV || delta <= -PRECHARGE_MAX_DELTA_MV
            || delta >= PRECHARGE_MAX_DELTA_MV)
        {
            run->failed_channels |= (uint8_t)BIT(i);
        }
    }

    if (run->failed_channels)
    {
        if (!PRECHARGE_BENCH_BYPASS_VOLTAGE_CHECK)
        {
            run->cause = PRECHARGE_CAUSE_VOLTAGE;
            finish_run(run, PRECHARGE_PHASE_IDLE, -ERANGE);
            return;
        }
        LOG_WRN("Bench bypass: 2T voltage check 0x%x", run->failed_channels);
        run->failed_channels = 0;
    }

    err = app_board_gpio_write(BOARD_GPIO_OUT_PE, false);
    if (err)
    {
        run->cause = PRECHARGE_CAUSE_GPIO;
        finish_run(run, PRECHARGE_PHASE_IDLE, err);
        return;
    }

    int64_t now_ms = k_uptime_get();

    run->leakage_from_ms = now_ms + PRECHARGE_MP10_SETTLE_MS;
    run->deadline_ms = now_ms + PRECHARGE_HANDOVER_MS;
    run->phase = PRECHARGE_PHASE_HANDOVER;
}

/**
 ******************************************************************************
 * @brief      优先处理停止指令，并推进当前预充阶段。
 * @param[in,out] run:预充线程持有的事务状态。
 * @retval     None
 ******************************************************************************
 */
static void advance_precharge(precharge_run_t *run)
{
    int64_t now_ms = k_uptime_get();
    bool clear_faults;
    bool started;
    bool confirmed;
    bool stopped;

    k_mutex_lock(&precharge_lock, K_FOREVER);
    clear_faults = exchange.clear_faults;
    exchange.clear_faults = false;
    started = exchange.start;
    stopped = exchange.stop;
    if (started || stopped)
    {
        /* 处理输出期间仍拒绝新启动，直到本轮公布处理结果。 */
        exchange.phase = PRECHARGE_PHASE_SETTLE;
    }
    confirmed = exchange.confirm;
    exchange.start = false;
    exchange.confirm = false;
    exchange.stop = false;
    k_mutex_unlock(&precharge_lock);

    if (clear_faults)
    {
        run->cause = PRECHARGE_CAUSE_NONE;
        run->failed_channels = 0;
        run->mp10_faults = 0;
        run->over = 0;
        run->reported = 0;
        if (run->release_error && !stopped)
        {
            finish_run(run, PRECHARGE_PHASE_IDLE, 0);
        }
    }

    if (stopped)
    {
        run->cause = PRECHARGE_CAUSE_NONE;
        finish_run(run, PRECHARGE_PHASE_IDLE, 0);
        return;
    }
    if (started)
    {
        begin_run(run);
        return;
    }
    if (run->phase == PRECHARGE_PHASE_HANDOVER)
    {
        if (confirmed)
        {
            run->cause = PRECHARGE_CAUSE_NONE;
            finish_run(run, PRECHARGE_PHASE_MONITOR, 0);
        }
        else if (now_ms >= run->deadline_ms)
        {
            run->cause = PRECHARGE_CAUSE_HANDOVER;
            finish_run(run, PRECHARGE_PHASE_IDLE, -ETIMEDOUT);
        }
        return;
    }

    switch (run->phase)
    {
        case PRECHARGE_PHASE_SETTLE:
            if (now_ms < run->deadline_ms)
            {
                break;
            }
            run->phase = PRECHARGE_PHASE_INPUT;
            run->deadline_ms = now_ms + PRECHARGE_INPUT_WINDOW_MS;
            check_input(run);
            break;
        case PRECHARGE_PHASE_INPUT:
            check_input(run);
            break;
        case PRECHARGE_PHASE_CHARGE:
            if (now_ms >= run->deadline_ms)
            {
                check_charge(run);
            }
            break;
        default:
            break;
    }
}

/**
 ******************************************************************************
 * @brief      发布阶段变化及对应事件。
 * @param[in]  run:预充线程运行状态。
 * @param[in]  previous:本轮处理前的阶段。
 * @retval     None
 ******************************************************************************
 */
static void publish_events(const precharge_run_t *run, precharge_phase_e previous)
{
    k_mutex_lock(&precharge_lock, K_FOREVER);
    exchange.phase = run->phase;
    exchange.release_failed = run->release_error != 0;
    if (run->phase == PRECHARGE_PHASE_HANDOVER && previous != run->phase)
    {
        exchange.events[PRECHARGE_EVENT_DONE] = (precharge_event_t){
            .type = PRECHARGE_EVENT_DONE, .id = run->id,
        };
        exchange.pending |= BIT(PRECHARGE_EVENT_DONE);
        exchange.completion_id = run->id;
        exchange.handover_by_ms = run->deadline_ms;
    }
    else if (previous == PRECHARGE_PHASE_HANDOVER && run->phase != previous)
    {
        exchange.pending &= ~BIT(PRECHARGE_EVENT_DONE);
        if (run->phase != PRECHARGE_PHASE_MONITOR)
        {
            exchange.events[PRECHARGE_EVENT_CANCEL] = (precharge_event_t){
                .type = PRECHARGE_EVENT_CANCEL, .id = run->id,
            };
            exchange.pending |= BIT(PRECHARGE_EVENT_CANCEL);
        }
        exchange.completion_id = 0;
    }
    if ((run->cause == PRECHARGE_CAUSE_INPUT || run->cause == PRECHARGE_CAUSE_VOLTAGE
         || run->cause == PRECHARGE_CAUSE_INTERNAL)
        && !(exchange.pending & BIT(PRECHARGE_EVENT_FAULT)))
    {
        exchange.events[PRECHARGE_EVENT_FAULT] = (precharge_event_t){
            .type = PRECHARGE_EVENT_FAULT, .cause = run->cause, .channels = run->failed_channels,
        };
        exchange.pending |= BIT(PRECHARGE_EVENT_FAULT);
    }
    k_mutex_unlock(&precharge_lock);
}

/**
 ******************************************************************************
 * @brief      提交一次四路预充请求。
 * @retval     0:请求已接收。
 * @retval     -EBUSY:已有活动事务或上次输出释放失败。
 * @retval     -EAGAIN:预充线程尚未就绪。
 ******************************************************************************
 */
int precharge_request_start(void)
{
    int err = 0;

    k_mutex_lock(&precharge_lock, K_FOREVER);
    if (!exchange.ready)
    {
        err = -EAGAIN;
    }
    else if (exchange.start || exchange.stop || exchange.clear_faults || exchange.release_failed
             || exchange.phase != PRECHARGE_PHASE_IDLE
             || (exchange.pending & BIT(PRECHARGE_EVENT_FAULT)))
    {
        err = -EBUSY;
    }
    else
    {
        exchange.start = true;
    }
    k_mutex_unlock(&precharge_lock);
    return err;
}

/**
 ******************************************************************************
 * @brief      请求停止，允许重复调用，由预充线程撤销输出。
 * @retval     None
 * @details    在线程上下文调用，返回表示请求已提交，执行结果通过快照获取。
 ******************************************************************************
 */
void precharge_request_stop(void)
{
    k_mutex_lock(&precharge_lock, K_FOREVER);
    exchange.stop = true;
    exchange.confirm = false;
    k_mutex_unlock(&precharge_lock);
}
/**
 ******************************************************************************
 * @brief      请求预充线程清除故障和待上报故障事件。
 * @retval     None
 * @details    在线程上下文调用。已清除的故障可由检测流程重新确认。
 ******************************************************************************
 */
void precharge_request_clear_faults(void)
{
    k_mutex_lock(&precharge_lock, K_FOREVER);
    exchange.clear_faults = true;
    exchange.pending &= ~(BIT(PRECHARGE_EVENT_FAULT) | BIT(PRECHARGE_EVENT_MP10_FAULT)
                          | BIT(PRECHARGE_EVENT_LEAKAGE));
    exchange.events[PRECHARGE_EVENT_MP10_FAULT].channels = 0;
    exchange.events[PRECHARGE_EVENT_LEAKAGE].channels = 0;
    k_mutex_unlock(&precharge_lock);
}

/**
 ******************************************************************************
 * @brief      通知预充模块主控旁路已确认吸合。
 * @param[in]  completion_id:完成事件的本地标识。
 * @retval     0:确认已受理，由预充线程释放输出。
 * @retval     -EINVAL:当前未等待接管，或已有停止请求。
 * @retval     -EAGAIN:预充线程尚未就绪。
 * @retval     -ETIMEDOUT:接管确认超过时限。
 ******************************************************************************
 */
int precharge_confirm_handover(uint32_t completion_id)
{
    int err = 0;

    k_mutex_lock(&precharge_lock, K_FOREVER);
    if (!exchange.ready)
    {
        err = -EAGAIN;
    }
    else if (exchange.phase != PRECHARGE_PHASE_HANDOVER || exchange.stop
             || completion_id != exchange.completion_id)
    {
        err = -EINVAL;
    }
    else if (k_uptime_get() >= exchange.handover_by_ms)
    {
        err = -ETIMEDOUT;
    }
    else
    {
        exchange.confirm = true;
    }
    k_mutex_unlock(&precharge_lock);
    return err;
}

/**
 ******************************************************************************
 * @brief      领取一个待上报事件。
 * @param[out] event:事件副本。
 * @retval     1:已领取。
 * @retval     0:无待处理事件。
 * @retval     -EINVAL:输出地址无效。
 ******************************************************************************
 */
int precharge_take_event(precharge_event_t *event)
{
    if (!event)
    {
        return -EINVAL;
    }

    k_mutex_lock(&precharge_lock, K_FOREVER);
    for (unsigned int i = 0; i < ARRAY_SIZE(exchange.events); i++)
    {
        if (exchange.pending & BIT(i))
        {
            *event = exchange.events[i];
            exchange.pending &= ~BIT(i);
            exchange.events[i].channels = 0;
            k_mutex_unlock(&precharge_lock);
            return 1;
        }
    }
    k_mutex_unlock(&precharge_lock);
    return 0;
}

/**
 ******************************************************************************
 * @brief      发布启动结果并允许通信线程提交预充请求。
 * @param[in,out] run:初始化为零的预充任务状态。
 * @param[in]  initial_error:启动错误，为零时正常就绪。
 * @retval     None
 * @details    启动错误通过现有内部故障上报，不改变原有电压检测请求受理策略。
 ******************************************************************************
 */
static void precharge_ctrl_init(precharge_run_t *run, int initial_error)
{
    if (initial_error)
    {
        run->cause = PRECHARGE_CAUSE_INTERNAL;
        publish_events(run, PRECHARGE_PHASE_IDLE);
        run->cause = PRECHARGE_CAUSE_NONE;
    }

    k_mutex_lock(&precharge_lock, K_FOREVER);
    exchange.ready = true;
    k_mutex_unlock(&precharge_lock);
}

/**
 ******************************************************************************
 * @brief      处理指令、推进预充状态并发布结果。
 * @param[in,out] run:预充线程持有的事务状态。
 * @retval     None
 ******************************************************************************
 */
static void precharge_ctrl_step(precharge_run_t *run)
{
    precharge_phase_e previous = run->phase;

    advance_precharge(run);
    publish_events(run, previous);
    run->cause = PRECHARGE_CAUSE_NONE;
}

/**
 ******************************************************************************
 * @brief      停止接收预充请求，撤销输出并发布故障结果。
 * @param[in,out] run:预充线程持有的事务状态。
 * @param[in]  error:预充线程退出原因。
 * @retval     None
 ******************************************************************************
 */
static void precharge_ctrl_shutdown(precharge_run_t *run, int error)
{
    k_mutex_lock(&precharge_lock, K_FOREVER);
    exchange.ready = false;
    exchange.start = false;
    k_mutex_unlock(&precharge_lock);

    precharge_phase_e previous = run->phase;

    run->cause = PRECHARGE_CAUSE_INTERNAL;
    finish_run(run, PRECHARGE_PHASE_IDLE, error);
    publish_events(run, previous);
}

/**
 ******************************************************************************
 * @brief      更新 MP10 可用性和漏电结果。
 * @param[in,out] run:预充线程状态。
 * @param[in]  mp10:四路最新测量。
 * @retval     None
 * @details    阈值和时长均配置后才判定漏电。暂采用绝对值严格超限，
 *             连续采样间隔使用 MP10 的有效时限，恢复后允许再次触发。
 ******************************************************************************
 */
static void precharge_ctrl_monitor(precharge_run_t *run, const mp10_context_t *mp10)
{
    precharge_event_t leakage = {.type = PRECHARGE_EVENT_LEAKAGE};
    int64_t now_ms = k_uptime_get();
    uint8_t mp10_faults = 0;

    for (unsigned int i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        const mp10_channel_t *channel = &mp10->channels[i];
        const mp10_sample_t *sample = &channel->sample;
        uint8_t bit = BIT(i);
        int64_t current = sample->current_deci_ma;
        int64_t last_rx_age_ms = now_ms - sample->received_ms;

        bool selftest_failed = channel->selftest_result == MP10_SELFTEST_FAIL
                               || channel->selftest_result == MP10_SELFTEST_TX_FAILED
                               || channel->selftest_result == MP10_SELFTEST_TIMEOUT;
        bool rx_timeout = channel->selftest_result == MP10_SELFTEST_PASS && !channel->online
                          && last_rx_age_ms >= PRECHARGE_MP10_OFFLINE_REPORT_MS;

        if (sample->fault || selftest_failed || rx_timeout)
        {
            mp10_faults |= bit;
            if (!(run->mp10_faults & bit))
            {
                LOG_WRN("MP10 CH%u fault: device_fault=%u code=0x%04x rx_timeout=%u",
                        i + 1u, (unsigned int)sample->fault, (unsigned int)sample->error_code,
                        (unsigned int)rx_timeout);
                LOG_WRN("MP10 CH%u detail: last_rx_age=%lld ms limit=%u ms selftest_failed=%u result=%u",
                        i + 1u, (long long)last_rx_age_ms,
                        PRECHARGE_MP10_OFFLINE_REPORT_MS, (unsigned int)selftest_failed,
                        (unsigned int)channel->selftest_result);
            }
        }
        else if (run->mp10_faults & bit)
        {
            LOG_WRN("MP10 CH%u fault cleared", i + 1u);
        }
        if (!CONFIG_APP_LEAKAGE_LIMIT_DECI_MA || !CONFIG_APP_LEAKAGE_DURATION_MS
            || !sample->valid || (run->phase != PRECHARGE_PHASE_HANDOVER
                                  && run->phase != PRECHARGE_PHASE_MONITOR)
            || sample->received_ms < run->leakage_from_ms
            || (current <= CONFIG_APP_LEAKAGE_LIMIT_DECI_MA
                && current >= -CONFIG_APP_LEAKAGE_LIMIT_DECI_MA))
        {
            run->over &= ~bit;
            run->reported &= ~bit;
            continue;
        }
        if (!(run->over & bit) || sample->received_ms - run->last_sample[i] >= MP10_TIMEOUT_MS)
        {
            run->over |= bit;
            run->reported &= ~bit;
            run->over_since[i] = sample->received_ms;
        }
        run->last_sample[i] = sample->received_ms;
        if (!(run->reported & bit)
            && sample->received_ms - run->over_since[i] >= CONFIG_APP_LEAKAGE_DURATION_MS)
        {
            run->reported |= bit;
            leakage.channels |= bit;
            leakage.current[i] = (int32_t)current;
        }
    }

    uint8_t new_faults = mp10_faults & ~run->mp10_faults;

    run->mp10_faults = mp10_faults;
    if (!new_faults && !leakage.channels)
    {
        return;
    }
    k_mutex_lock(&precharge_lock, K_FOREVER);
    if (new_faults)
    {
        exchange.events[PRECHARGE_EVENT_MP10_FAULT].type = PRECHARGE_EVENT_MP10_FAULT;
        exchange.events[PRECHARGE_EVENT_MP10_FAULT].channels |= new_faults;
        exchange.pending |= BIT(PRECHARGE_EVENT_MP10_FAULT);
    }
    if (leakage.channels)
    {
        precharge_event_t *pending = &exchange.events[PRECHARGE_EVENT_LEAKAGE];

        pending->type = PRECHARGE_EVENT_LEAKAGE;
        pending->channels |= leakage.channels;
        for (unsigned int i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
        {
            if (leakage.channels & BIT(i))
            {
                pending->current[i] = leakage.current[i];
            }
        }
        exchange.pending |= BIT(PRECHARGE_EVENT_LEAKAGE);
    }
    k_mutex_unlock(&precharge_lock);
}

/**
 ******************************************************************************
 * @brief      到达设定时间时翻转运行灯。
 * @param[in,out] next_led_update_ms:下次翻转时间，单位为毫秒。
 * @param[in,out] led_active:上次成功设置的灯状态。
 * @retval     None
 ******************************************************************************
 */
static void led_flash(int64_t *next_led_update_ms, bool *led_active)
{
    int64_t now_ms = k_uptime_get();
    int err;

    if (now_ms < *next_led_update_ms)
    {
        return;
    }

    err = app_board_gpio_write(BOARD_GPIO_OUT_LED, !*led_active);
    if (!err)
    {
        *led_active = !*led_active;
    }
    else
    {
        LOG_ERR("Heartbeat LED update failed: %d", err);
    }
    *next_led_update_ms = now_ms + PRECHARGE_LED_INTERVAL_MS;
}

/**
 ******************************************************************************
 * @brief      周期更新预充回读和心跳，并报告任务进度。
 * @param[in]  arg1:Zephyr 保留参数，未使用。
 * @param[in]  arg2:Zephyr 保留参数，未使用。
 * @param[in]  arg3:Zephyr 保留参数，未使用。
 * @retval     None
 *
 * @details    看门狗操作失败时退出线程。
 ******************************************************************************
 */
static void precharge_entry(void *arg1, void *arg2, void *arg3)
{
    mp10_context_t mp10 = {0};
    precharge_run_t run = {0};
    int64_t next_led_update_ms = 0;
    bool led_active = false;
    int channel;
    int err;

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    channel = board_watchdog_register(PRECHARGE_WATCHDOG_TIMEOUT_MS);
    if (channel < 0)
    {
        LOG_ERR("Precharge watchdog registration failed: %d", channel);
        return;
    }

    err = mp10_init();
    if (err)
    {
        LOG_ERR("MP10 receive filter setup failed: %d", err);
    }

    precharge_ctrl_init(&run, err);

    for (;;)
    {
        precharge_ctrl_step(&run);
        mp10_task(&mp10);
        precharge_ctrl_monitor(&run, &mp10);

        led_flash(&next_led_update_ms, &led_active);

        err = board_watchdog_feed(channel);
        if (err)
        {
            precharge_ctrl_shutdown(&run, err);
            return;
        }

        k_msleep(PRECHARGE_TASK_INTERVAL_MS);
    }
}

/**
 ******************************************************************************
 * @brief      启动预充线程。
 * @retval     0:已创建线程。
 * @details    在板级初始化和参数加载完成后调用一次。
 ******************************************************************************
 */
int precharge_start_task(void)
{
    k_thread_create(&precharge_thread, precharge_stack, K_THREAD_STACK_SIZEOF(precharge_stack),
                    precharge_entry, NULL, NULL, NULL, PRECHARGE_TASK_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&precharge_thread, "precharge");
    return 0;
}

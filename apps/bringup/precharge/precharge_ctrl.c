#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "board_gpio.h"
#include "precharge_ctrl.h"
#include "precharge_io.h"

/** U46 闭合后 ADC 稳定时间，联调值，单位为毫秒。 */
#define PRECHARGE_ADC_SETTLE_MS 20u
/** U46 释放后 MP10 稳定时间，联调值，单位为毫秒。 */
#define PRECHARGE_MP10_SETTLE_MS 100u
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

/** 预充和通信线程在持锁时交换的指令和快照。 */
typedef struct
{
    precharge_report_t report; /**< 已发布的事务快照。 */
    int64_t handover_by_ms;    /**< 接管确认最晚受理时刻。 */
    bool ready;                /**< 预充线程已就绪。 */
    bool can_begin;            /**< 状态机允许受理新预充请求。 */
    bool start;                /**< 新预充请求待处理。 */
    bool confirm;              /**< 已收到有效接管确认。 */
    bool stop;                 /**< 停止请求待处理。 */
} precharge_exchange_t;

BUILD_ASSERT(PRECHARGE_CHANNEL_COUNT == MP10_CHANNEL_COUNT);

LOG_MODULE_REGISTER(precharge_ctrl, LOG_LEVEL_ERR);

static K_MUTEX_DEFINE(precharge_lock);
static precharge_exchange_t exchange;

/**
 ******************************************************************************
 * @brief      撤销所有预充输出并发布本次运行的结束原因。
 * @param[in,out] run:预充线程持有的事务状态。
 * @param[in]  status:输出释放成功时记录的状态。
 * @param[in]  error:失败原因，正常结束时为零。
 * @retval     None
 ******************************************************************************
 */
static void finish_run(precharge_run_t *run, precharge_status_e status, int error)
{
    int relay_error = precharge_set_relays(false);
    int pe_error = app_board_gpio_write(BOARD_GPIO_OUT_PE, false);

    run->report.release_error = relay_error;
    if (!relay_error)
    {
        run->report.release_error = pe_error;
    }
    run->report.status = status;
    if (run->report.release_error)
    {
        run->report.cause = PRECHARGE_CAUSE_RELEASE;
        run->report.status = PRECHARGE_FAILED;
    }
    run->report.error = error;
    run->phase = PRECHARGE_PHASE_IDLE;
    if (error || run->report.release_error)
    {
        LOG_ERR("Precharge %u ended: error=%d, release=%d, channels=0x%x", run->report.id, error,
                run->report.release_error, run->report.failed_channels);
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
    uint32_t id = run->report.id + 1u;
    int err;

    if (id == 0u)
    {
        id = 1u;
    }
    *run = (precharge_run_t){0};
    run->report.id = id;
    run->report.status = PRECHARGE_CHECKING;
    err = app_board_gpio_write(BOARD_GPIO_OUT_PE, true);
    if (err)
    {
        run->report.cause = PRECHARGE_CAUSE_GPIO;
        finish_run(run, PRECHARGE_FAILED, err);
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
 * @brief      采样输入电压，并在窗口结束后决定能否吸合四路。
 * @param[in,out] run:预充线程持有的事务状态。
 * @param[in]  now_ms:当前开机时间，单位为毫秒。
 * @retval     None
 ******************************************************************************
 */
static void check_input(precharge_run_t *run, int64_t now_ms)
{
    int err = precharge_read_voltages(run->report.input_mv, run->report.load_mv);

    if (err)
    {
        run->report.cause = PRECHARGE_CAUSE_ADC;
        finish_run(run, PRECHARGE_FAILED, err);
        return;
    }

    for (size_t i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        int32_t voltage = run->report.input_mv[i];

        run->min_mv[i] = MIN(run->min_mv[i], voltage);
        run->max_mv[i] = MAX(run->max_mv[i], voltage);
    }

    if (now_ms < run->deadline_ms)
    {
        return;
    }

    for (size_t i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        int32_t spread = run->max_mv[i] - run->min_mv[i];

        if (run->min_mv[i] <= PRECHARGE_MIN_INPUT_MV
            || spread * 100 >= run->max_mv[i] * PRECHARGE_INPUT_SPREAD_PERCENT)
        {
            run->report.failed_channels |= (uint8_t)BIT(i);
        }
    }
    if (run->report.failed_channels)
    {
        run->report.cause = PRECHARGE_CAUSE_INPUT;
        finish_run(run, PRECHARGE_FAILED, -EDOM);
        return;
    }

    err = precharge_set_relays(true);
    if (err)
    {
        run->report.cause = PRECHARGE_CAUSE_GPIO;
        finish_run(run, PRECHARGE_FAILED, err);
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
    int err = precharge_read_voltages(run->report.input_mv, run->report.load_mv);

    if (err)
    {
        run->report.cause = PRECHARGE_CAUSE_ADC;
        finish_run(run, PRECHARGE_FAILED, err);
        return;
    }

    for (size_t i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        int32_t input = run->report.input_mv[i];
        int32_t delta = input - run->report.load_mv[i];

        if (input <= PRECHARGE_MIN_INPUT_MV || delta <= -PRECHARGE_MAX_DELTA_MV
            || delta >= PRECHARGE_MAX_DELTA_MV)
        {
            run->report.failed_channels |= (uint8_t)BIT(i);
        }
    }

    if (run->report.failed_channels)
    {
        run->report.cause = PRECHARGE_CAUSE_VOLTAGE;
        finish_run(run, PRECHARGE_FAILED, -ERANGE);
        return;
    }

    err = app_board_gpio_write(BOARD_GPIO_OUT_PE, false);
    if (err)
    {
        run->report.cause = PRECHARGE_CAUSE_GPIO;
        finish_run(run, PRECHARGE_FAILED, err);
        return;
    }

    run->leakage_from_ms = k_uptime_get() + PRECHARGE_MP10_SETTLE_MS;
    run->deadline_ms = k_uptime_get() + PRECHARGE_HANDOVER_MS;
    run->phase = PRECHARGE_PHASE_IDLE;
    run->report.status = PRECHARGE_WAIT_HANDOVER;
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
    bool started;
    bool confirmed;
    bool stopped;

    k_mutex_lock(&precharge_lock, K_FOREVER);
    started = exchange.start;
    if (started)
    {
        exchange.can_begin = false;
    }
    confirmed = exchange.confirm;
    stopped = exchange.stop;
    exchange.start = false;
    exchange.confirm = false;
    exchange.stop = false;
    k_mutex_unlock(&precharge_lock);

    if (stopped)
    {
        run->report.cause = PRECHARGE_CAUSE_NONE;
        finish_run(run, PRECHARGE_STOPPED, 0);
        return;
    }
    if (started)
    {
        begin_run(run);
        return;
    }
    if (run->report.status == PRECHARGE_WAIT_HANDOVER)
    {
        if (confirmed)
        {
            run->report.cause = PRECHARGE_CAUSE_NONE;
            finish_run(run, PRECHARGE_COMPLETE, 0);
        }
        else if (now_ms >= run->deadline_ms)
        {
            run->report.cause = PRECHARGE_CAUSE_HANDOVER;
            finish_run(run, PRECHARGE_FAILED, -ETIMEDOUT);
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
            check_input(run, now_ms);
            break;
        case PRECHARGE_PHASE_INPUT:
            check_input(run, now_ms);
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
 * @brief      发布预充结果和四路漏电测量快照。
 * @param[in,out] run:预充线程持有的事务状态。
 * @param[in]  mp10:由预充线程更新的接收状态。
 * @retval     None
 ******************************************************************************
 */
static void publish_report(precharge_run_t *run, const mp10_context_t *mp10)
{
    bool monitoring =
        run->report.status == PRECHARGE_WAIT_HANDOVER || run->report.status == PRECHARGE_COMPLETE;

    for (size_t i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        const mp10_channel_t *channel = &mp10->channels[i];

        run->report.leakage[i] = (precharge_leakage_t){
            .current_deci_ma = channel->sample.current_deci_ma,
            .error_code = channel->sample.error_code,
            .online = channel->online,
            .fault = channel->sample.fault,
            .selftest_passed = channel->selftest_result == MP10_SELFTEST_PASS,
            .usable = monitoring && channel->sample.valid
                      && channel->sample.received_ms >= run->leakage_from_ms,
        };
    }

    k_mutex_lock(&precharge_lock, K_FOREVER);
    exchange.report = run->report;
    exchange.can_begin =
        (run->report.status == PRECHARGE_IDLE || run->report.status == PRECHARGE_FAILED
         || run->report.status == PRECHARGE_STOPPED)
        && !run->report.release_error && !exchange.stop;
    exchange.handover_by_ms = run->deadline_ms;
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
int precharge_begin(void)
{
    int err = 0;

    k_mutex_lock(&precharge_lock, K_FOREVER);
    if (!exchange.ready)
    {
        err = -EAGAIN;
    }
    else if (!exchange.can_begin || exchange.start || exchange.stop)
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
void precharge_stop(void)
{
    k_mutex_lock(&precharge_lock, K_FOREVER);
    exchange.stop = true;
    exchange.confirm = false;
    k_mutex_unlock(&precharge_lock);
}
/**
 ******************************************************************************
 * @brief      通知预充模块主控旁路已确认吸合。
 * @retval     0:确认已受理，由预充线程释放输出。
 * @retval     -EINVAL:当前未等待接管，或已有停止请求。
 * @retval     -EAGAIN:预充线程尚未就绪。
 * @retval     -ETIMEDOUT:接管确认超过时限。
 ******************************************************************************
 */
int precharge_confirm(void)
{
    int err = 0;

    k_mutex_lock(&precharge_lock, K_FOREVER);
    if (!exchange.ready)
    {
        err = -EAGAIN;
    }
    else if (exchange.report.status != PRECHARGE_WAIT_HANDOVER || exchange.stop)
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
 * @brief      复制最近一次预充事务和四路漏电测量。
 * @param[out] report:快照输出地址。
 * @retval     0:读取成功。
 * @retval     -EINVAL:输出地址为空。
 ******************************************************************************
 */
int precharge_get_report(precharge_report_t *report)
{
    if (report == NULL)
    {
        return -EINVAL;
    }

    k_mutex_lock(&precharge_lock, K_FOREVER);
    *report = exchange.report;
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
void precharge_ctrl_init(precharge_run_t *run, int initial_error)
{
    if (initial_error)
    {
        run->report.status = PRECHARGE_FAILED;
        run->report.cause = PRECHARGE_CAUSE_INTERNAL;
        run->report.error = initial_error;
    }

    k_mutex_lock(&precharge_lock, K_FOREVER);
    exchange.report = run->report;
    exchange.ready = true;
    exchange.can_begin = true;
    k_mutex_unlock(&precharge_lock);
}

/**
 ******************************************************************************
 * @brief      处理指令、推进预充状态并发布结果。
 * @param[in,out] run:预充线程持有的事务状态。
 * @param[in]  mp10:当前周期的 MP10 状态。
 * @retval     None
 ******************************************************************************
 */
void precharge_ctrl_step(precharge_run_t *run, const mp10_context_t *mp10)
{
    advance_precharge(run);
    publish_report(run, mp10);
}

/**
 ******************************************************************************
 * @brief      停止接收预充请求，撤销输出并发布故障结果。
 * @param[in,out] run:预充线程持有的事务状态。
 * @param[in]  mp10:当前周期的 MP10 状态。
 * @param[in]  error:预充线程退出原因。
 * @retval     None
 ******************************************************************************
 */
void precharge_ctrl_shutdown(precharge_run_t *run, const mp10_context_t *mp10, int error)
{
    k_mutex_lock(&precharge_lock, K_FOREVER);
    exchange.ready = false;
    exchange.start = false;
    k_mutex_unlock(&precharge_lock);

    run->report.cause = PRECHARGE_CAUSE_INTERNAL;
    finish_run(run, PRECHARGE_FAILED, error);
    publish_report(run, mp10);
}

#ifndef BRINGUP_PRECHARGE_CTRL_H_
#define BRINGUP_PRECHARGE_CTRL_H_

#include "mp10.h"
#include "precharge.h"

typedef enum
{
    PRECHARGE_PHASE_IDLE,   /**< 无待执行的电压检查。 */
    PRECHARGE_PHASE_SETTLE, /**< 等待 ADC 参考稳定。 */
    PRECHARGE_PHASE_INPUT,  /**< 采集吸合前输入。 */
    PRECHARGE_PHASE_CHARGE, /**< 等待 2T 检查。 */
} precharge_phase_e;

/** 由预充线程独占的本次运行状态。 */
typedef struct
{
    precharge_report_t report; /**< 事务结果及最新测量。 */
    precharge_phase_e phase;   /**< 电压检查阶段。 */
    int64_t deadline_ms;       /**< 当前阶段的截止时间。 */
    int64_t leakage_from_ms;   /**< 允许用于判定的最早 MP10 接收时间。 */
    int32_t min_mv[PRECHARGE_CHANNEL_COUNT]; /**< 前置采样窗口最小值。 */
    int32_t max_mv[PRECHARGE_CHANNEL_COUNT]; /**< 前置采样窗口最大值。 */
} precharge_run_t;

/**
 ******************************************************************************
 * @brief      发布启动结果并允许通信线程提交预充请求。
 * @param[in,out] run:初始化为零的预充任务状态。
 * @param[in]  initial_error:启动错误，为零时正常就绪。
 * @retval     None
 * @details    启动错误通过现有内部故障上报，不改变原有电压检测请求受理策略。
 ******************************************************************************
 */
void precharge_ctrl_init(precharge_run_t *run, int initial_error);

/**
 ******************************************************************************
 * @brief      处理指令、推进预充状态并发布结果。
 * @param[in,out] run:预充线程持有的事务状态。
 * @param[in]  mp10:当前周期的 MP10 状态。
 * @retval     None
 ******************************************************************************
 */
void precharge_ctrl_step(precharge_run_t *run, const mp10_context_t *mp10);

/**
 ******************************************************************************
 * @brief      停止接收预充请求，撤销输出并发布故障结果。
 * @param[in,out] run:预充线程持有的事务状态。
 * @param[in]  mp10:当前周期的 MP10 状态。
 * @param[in]  error:预充线程退出原因。
 * @retval     None
 ******************************************************************************
 */
void precharge_ctrl_shutdown(precharge_run_t *run, const mp10_context_t *mp10, int error);

#endif /* BRINGUP_PRECHARGE_CTRL_H_ */

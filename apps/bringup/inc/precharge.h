#ifndef BRINGUP_PRECHARGE_H_
/** 防止本头文件在同一编译单元内重复包含。 */
#define BRINGUP_PRECHARGE_H_

#include <stdbool.h>
#include <stdint.h>

/** 单次预充同步检测的通道数。 */
#define PRECHARGE_CHANNEL_COUNT 4u

typedef enum
{
    PRECHARGE_CAUSE_NONE,     /**< 无故障。 */
    PRECHARGE_CAUSE_INPUT,    /**< 输入电压检查不通过。 */
    PRECHARGE_CAUSE_VOLTAGE,  /**< 预充结束时电压不合格。 */
    PRECHARGE_CAUSE_ADC,      /**< ADC 采样失败。 */
    PRECHARGE_CAUSE_GPIO,     /**< GPIO 操作失败。 */
    PRECHARGE_CAUSE_HANDOVER, /**< 接管确认超时。 */
    PRECHARGE_CAUSE_RELEASE,  /**< 预充输出释放失败。 */
    PRECHARGE_CAUSE_INTERNAL, /**< 预充线程内部错误。 */
} precharge_cause_e;

/** 对外事件类别。 */
typedef enum
{
    PRECHARGE_EVENT_CANCEL, /**< 本轮接管已失效。 */
    PRECHARGE_EVENT_DONE, /**< 等待主板接管。 */
    PRECHARGE_EVENT_FAULT, /**< 预充或输出故障。 */
    PRECHARGE_EVENT_MP10_FAULT, /**< MP10 故障通道。 */
    PRECHARGE_EVENT_LEAKAGE, /**< 漏电超限。 */
    PRECHARGE_EVENT_COUNT, /**< 事件槽数量。 */
} precharge_event_e;

/** 交付通信任务的事件副本。 */
typedef struct
{
    precharge_event_e type; /**< 事件类别。 */
    uint32_t id; /**< 本地接管标识。 */
    precharge_cause_e cause; /**< 故障原因。 */
    uint8_t channels; /**< 故障通道位图。 */
    int32_t current[PRECHARGE_CHANNEL_COUNT]; /**< 漏电电流，单位 0.1 mA。 */
} precharge_event_t;

/**
 ******************************************************************************
 * @brief      启动预充线程。
 * @retval     0:已创建线程。
 * @details    在板级初始化和参数加载完成后调用一次。
 ******************************************************************************
 */
int precharge_start_task(void);

/**
 ******************************************************************************
 * @brief      提交一次四路预充请求。
 * @retval     0:请求已接收。
 * @retval     -EBUSY:已有活动事务或上次输出释放失败。
 * @retval     -EAGAIN:预充线程尚未就绪。
 ******************************************************************************
 */
int precharge_request_start(void);

/**
 ******************************************************************************
 * @brief      请求停止，允许重复调用，由预充线程撤销输出。
 * @retval     None
 * @details    在线程上下文调用，返回表示请求已提交，撤销失败通过事件交付。
 ******************************************************************************
 */
void precharge_request_stop(void);

/**
 ******************************************************************************
 * @brief      请求预充线程清除故障和待上报故障事件。
 * @retval     None
 * @details    在线程上下文调用。已清除的故障可由检测流程重新确认。
 ******************************************************************************
 */
void precharge_request_clear_faults(void);

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
int precharge_confirm_handover(uint32_t completion_id);

/**
 ******************************************************************************
 * @brief      领取待发送事件。
 * @param[out] event:事件输出地址。
 * @retval     1:已复制事件。
 * @retval     0:暂无事件。
 * @retval     -EINVAL:输出地址为空。
 ******************************************************************************
 */
int precharge_take_event(precharge_event_t *event);

#endif /* BRINGUP_PRECHARGE_H_ */

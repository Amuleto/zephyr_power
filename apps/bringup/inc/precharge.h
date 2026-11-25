#ifndef BRINGUP_PRECHARGE_H_
/** 防止本头文件在同一编译单元内重复包含。 */
#define BRINGUP_PRECHARGE_H_

#include <stdbool.h>
#include <stdint.h>

/** 单次预充同步检测的通道数。 */
#define PRECHARGE_CHANNEL_COUNT 4u

typedef enum
{
    PRECHARGE_IDLE,          /**< 尚无预充事务。 */
    PRECHARGE_CHECKING,      /**< 正在检查电压并执行预充。 */
    PRECHARGE_WAIT_HANDOVER, /**< 电压检查通过，等待旁路吸合确认。 */
    PRECHARGE_COMPLETE,      /**< 已确认接管并释放预充输出。 */
    PRECHARGE_FAILED,        /**< 检查失败，已尝试释放全部输出。 */
    PRECHARGE_STOPPED,       /**< 收到停止指令，已尝试释放全部输出。 */
} precharge_status_e;

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

/** 单路 MP10 快照。电流仅在 usable 为真时用于漏电判定。 */
typedef struct
{
    int64_t current_deci_ma; /**< 剩余电流，单位为 0.1 mA。 */
    uint16_t error_code;     /**< MP10 设备故障码。 */
    bool online;             /**< MP10 周期帧未过期。 */
    bool fault;              /**< MP10 报告设备故障。 */
    bool selftest_passed;    /**< 该模块本次上电自检已通过。 */
    bool usable;             /**< 已越过 U46 和自检的恢复窗口。 */
} precharge_leakage_t;

/** 供主板通信线程读取的事务快照。 */
typedef struct
{
    uint32_t id;               /**< 本次事务编号，零表示尚无事务。 */
    precharge_status_e status; /**< 当前事务状态。 */
    precharge_cause_e cause;   /**< 本次预充的故障类别。 */
    uint8_t failed_channels;   /**< 检查失败的通道，第 1 路对应 bit 0。 */
    int error;                 /**< 检查、采样或接管失败的错误码。 */
    int release_error;         /**< 释放输出时的首个错误码。 */
    int32_t input_mv[PRECHARGE_CHANNEL_COUNT]; /**< 继电器前高压侧电压，单位为 mV。 */
    int32_t load_mv[PRECHARGE_CHANNEL_COUNT];  /**< 电阻后高压侧电压，单位为 mV。 */
    precharge_leakage_t leakage[PRECHARGE_CHANNEL_COUNT]; /**< 各路最新 MP10 状态。 */
} precharge_report_t;

/**
 ******************************************************************************
 * @brief      启动预充线程。
 * @retval     None
 * @details    在板级初始化和参数加载完成后调用一次。
 ******************************************************************************
 */
void precharge_start(void);

/**
 ******************************************************************************
 * @brief      提交一次四路预充请求。
 * @retval     0:请求已接收。
 * @retval     -EBUSY:已有活动事务或上次输出释放失败。
 * @retval     -EAGAIN:预充线程尚未就绪。
 ******************************************************************************
 */
int precharge_begin(void);

/**
 ******************************************************************************
 * @brief      请求停止，允许重复调用，由预充线程撤销输出。
 * @retval     None
 * @details    在线程上下文调用，返回表示请求已提交，执行结果通过快照获取。
 ******************************************************************************
 */
void precharge_stop(void);

/**
 ******************************************************************************
 * @brief      通知预充模块主控旁路已确认吸合。
 * @retval     0:确认已受理，由预充线程释放输出。
 * @retval     -EINVAL:当前未等待接管，或已有停止请求。
 * @retval     -EAGAIN:预充线程尚未就绪。
 * @retval     -ETIMEDOUT:接管确认超过时限。
 ******************************************************************************
 */
int precharge_confirm(void);

/**
 ******************************************************************************
 * @brief      复制最近一次预充事务和四路漏电测量。
 * @param[out] report:快照输出地址。
 * @retval     0:读取成功。
 * @retval     -EINVAL:输出地址为空。
 ******************************************************************************
 */
int precharge_get_report(precharge_report_t *report);

#endif /* BRINGUP_PRECHARGE_H_ */

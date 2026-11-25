#ifndef BRINGUP_MP10_H_
/** 防止本头文件重复包含。 */
#define BRINGUP_MP10_H_

#include <stdbool.h>
#include <stdint.h>

/** 四台 MP10 的逻辑通道数量。 */
#define MP10_CHANNEL_COUNT 4u

typedef enum
{
    MP10_SELFTEST_NOT_RUN,   /**< 本次上电尚未自检。 */
    MP10_SELFTEST_PENDING,   /**< 正在等待发送或应答。 */
    MP10_SELFTEST_PASS,      /**< 设备报告通过。 */
    MP10_SELFTEST_FAIL,      /**< 设备报告失败。 */
    MP10_SELFTEST_TX_FAILED, /**< 提交请求或发送失败。 */
    MP10_SELFTEST_TIMEOUT,   /**< 等待发送或应答超时。 */
} mp10_selftest_result_e;

typedef enum
{
    MP10_TEST_IDLE,       /**< 可选择下一台设备。 */
    MP10_TEST_WAIT_TX,    /**< 等待驱动发送结果。 */
    MP10_TEST_WAIT_REPLY, /**< 等待设备应答。 */
    MP10_TEST_RECOVER,    /**< 等待自检电流消退。 */
} mp10_selftest_state_e;

/** 最近一次漏电测量。 */
typedef struct
{
    int64_t received_ms;     /**< 测量接收时间，单位为毫秒。 */
    int64_t current_deci_ma; /**< 剩余电流，单位为 0.1 mA。 */
    uint16_t error_code;     /**< 设备故障码。 */
    bool valid;              /**< 测量未过期，且可用于漏电判定。 */
    bool fault;              /**< 设备故障标志。 */
} mp10_sample_t;

/** 单台设备的测量和上电自检状态，由预充线程独占。 */
typedef struct
{
    mp10_sample_t sample;                   /**< 最近一次测量。 */
    mp10_selftest_result_e selftest_result; /**< 本次上电的自检结果。 */
    bool online;                            /**< 最近仍收到该路的测量帧。 */
    int64_t measurement_from_ms;            /**< 自检后可用测量帧的最早接收时间。 */
} mp10_channel_t;

/** 四台设备和单个在途自检的运行状态。 */
typedef struct
{
    mp10_channel_t channels[MP10_CHANNEL_COUNT]; /**< 各路设备状态。 */
    mp10_selftest_state_e state;                 /**< 当前自检事务阶段。 */
    unsigned int active;             /**< 当前自检通道，从 1 开始，仅在非空闲阶段有效。 */
    int64_t deadline_ms;             /**< 当前自检截止时间，单位为毫秒。 */
    int64_t request_ms;              /**< 提交自检的时间，单位为毫秒。 */
    int64_t reply_ms;                /**< 自检应答的接收时间，单位为毫秒。 */
    uint8_t reply;                   /**< 自检应答首字节，零表示尚未收到。 */
    int64_t next_measurement_log_ms; /**< 四路测量共用的下次日志时间，单位为毫秒。 */
} mp10_context_t;

/**
 ******************************************************************************
 * @brief      安装 MP10 的 CAN 接收过滤器。
 * @retval     0:启动成功。
 * @retval     负值:CAN 设备或过滤器配置失败。
 ******************************************************************************
 */
int mp10_init(void);

/**
 ******************************************************************************
 * @brief      更新四路测量、在线状态和上电自检。
 * @param[in,out] task:初始化为零的线程私有状态。
 * @retval     None
 * @details    由预充线程周期调用，每轮最多处理八条 CAN 回调事件；四路测量共用每秒一条日志限流。
 ******************************************************************************
 */
void mp10_task(mp10_context_t *task);

#endif /* BRINGUP_MP10_H_ */

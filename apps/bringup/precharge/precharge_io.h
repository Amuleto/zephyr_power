#ifndef BRINGUP_PRECHARGE_IO_H_
#define BRINGUP_PRECHARGE_IO_H_

#include <stdbool.h>
#include <stdint.h>

#include "precharge.h"

/** 四路完整回读，电压为高压侧 mV，位图从第一路起。 */
typedef struct
{
    int32_t input_mv[PRECHARGE_CHANNEL_COUNT]; /**< 继电器前电压。 */
    int32_t load_mv[PRECHARGE_CHANNEL_COUNT]; /**< 电阻后电压。 */
    int64_t read_ms; /**< 成功读取完整 DMA 快照后的本地时间，单位为毫秒。 */
    uint8_t valid_channels; /**< 前后采样均有效的通道。 */
} precharge_adc_snapshot_t;

/**
 ******************************************************************************
 * @brief      读取同一采样轮次的四路前后高压侧电压。
 * @param[out] snapshot:输出快照，成功时按位图读取各路电压。
 * @retval     0:读取成功。
 * @retval     -EINVAL:输出地址为空。
 * @retval     其他负值:ADC 采样失败。
 ******************************************************************************
 */
int precharge_read_snapshot(precharge_adc_snapshot_t *snapshot);

/**
 ******************************************************************************
 * @brief      设置四路预充输出。
 * @param[in]  active:true 吸合，false 释放。
 * @retval     0:全部输出设置成功。
 * @retval     负值:首个 GPIO 错误。吸合失败即返回，释放时仍尝试其余输出。
 ******************************************************************************
 */
int precharge_set_relays(bool active);

#endif /* BRINGUP_PRECHARGE_IO_H_ */

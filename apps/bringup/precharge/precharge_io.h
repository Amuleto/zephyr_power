#ifndef BRINGUP_PRECHARGE_IO_H_
#define BRINGUP_PRECHARGE_IO_H_

#include <stdbool.h>
#include <stdint.h>

#include "precharge.h"

/**
 ******************************************************************************
 * @brief      读取同一采样轮次的四路前后高压侧电压。
 * @param[out] input_mv:继电器前电压数组，单位为 mV，容量至少四项。
 * @param[out] load_mv:电阻后电压数组，单位为 mV，容量至少四项。
 * @retval     0:读取成功。
 * @retval     -EINVAL:输出地址为空。
 * @retval     其他负值:ADC 采样失败。
 ******************************************************************************
 */
int precharge_read_voltages(int32_t input_mv[PRECHARGE_CHANNEL_COUNT],
                            int32_t load_mv[PRECHARGE_CHANNEL_COUNT]);

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

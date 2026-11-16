#ifndef BRINGUP_BOARD_ADC_H_
#define BRINGUP_BOARD_ADC_H_

#include <stdint.h>
#include <stddef.h>

enum app_adc_channel_e
{
    APP_ADC_PA0 = 0, /**< 通道 1 继电器前。 */
    APP_ADC_PA1,     /**< 通道 1 电阻后。 */
    APP_ADC_PA2,     /**< 通道 2 继电器前。 */
    APP_ADC_PA3,     /**< 通道 2 电阻后。 */
    APP_ADC_PA4,     /**< 通道 3 继电器前。 */
    APP_ADC_PA5,     /**< 通道 3 电阻后。 */
    APP_ADC_PA6,     /**< 通道 4 继电器前。 */
    APP_ADC_PA7      /**< 通道 4 电阻后。 */
};

/**
 ******************************************************************************
 * @brief      配置 ADC 通道并启动连续 DMA 采样。
 * @retval     0:初始化成功。
 * @retval     -ENODEV:ADC 设备未就绪。
 * @retval     其他负值:ADC 通道配置失败。
 * @details    由板级初始化在线程上下文调用一次。
 ******************************************************************************
 */
int app_board_adc_init(void);

/**
 ******************************************************************************
 * @brief      获取指定通道最近一轮完整采样的引脚电压。
 * @param[in]  channel:PA0～PA7 对应的通道枚举。
 * @param[out] voltage_mv:电压输出地址，单位为 mV，须有效。
 * @retval     0:读取成功。
 * @retval     -EINVAL:通道或输出地址无效。
 * @retval     -EAGAIN:采样尚未就绪或未取得稳定快照。
 * @retval     -EIO:DMA 传输错误。
 * @retval     其他负值:电压换算失败。
 * @details    在线程上下文调用。使用设备树中的名义参考电压换算，
 *             输出为 MCU 引脚电压。失败时保持输出值。
 ******************************************************************************
 */
int app_board_adc_get_voltage(enum app_adc_channel_e channel, int32_t *voltage_mv);

/**
 ******************************************************************************
 * @brief      读取同一轮采样中 PA0～PA7 的引脚电压。
 * @param[out] voltages_mv:输出数组，单位为 mV。
 * @param[in]  count:数组容量，至少八项。
 * @retval     0:读取成功。
 * @retval     -EINVAL:输出地址或容量无效。
 * @retval     其他负值:采样或换算失败，输出保持原值。
 ******************************************************************************
 */
int app_board_adc_read_all(int32_t *voltages_mv, size_t count);

#endif /* BRINGUP_BOARD_ADC_H_ */

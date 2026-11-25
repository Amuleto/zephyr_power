#include <errno.h>

#include "board_adc.h"
#include "board_gpio.h"
#include "precharge_io.h"

/** 高压采样为零时的 ADC 引脚电压，单位为 mV。 */
#define PRECHARGE_ADC_ZERO_MV 1500
/** 高压侧与 ADC 电压的比例分子。 */
#define PRECHARGE_ADC_GAIN_NUM 3441
/** 高压侧与 ADC 电压的比例分母。 */
#define PRECHARGE_ADC_GAIN_DEN 2

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
                            int32_t load_mv[PRECHARGE_CHANNEL_COUNT])
{
    int32_t adc_mv[2u * PRECHARGE_CHANNEL_COUNT];
    int err;

    if (input_mv == NULL || load_mv == NULL)
    {
        return -EINVAL;
    }

    err = app_board_adc_read_all(adc_mv, 2u * PRECHARGE_CHANNEL_COUNT);
    if (err)
    {
        return err;
    }

    for (unsigned int i = 0; i < 2u * PRECHARGE_CHANNEL_COUNT; i++)
    {
        /* ADC 贴地时，直接换算会把失效的通道当成高电压。 */
        if (adc_mv[i] <= 0)
        {
            return -ERANGE;
        }
    }

    /* 引脚输入和换算结果都使用 mV，保留高压侧的符号。 */
    for (unsigned int i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        input_mv[i] = (PRECHARGE_ADC_ZERO_MV - adc_mv[2u * i]) * PRECHARGE_ADC_GAIN_NUM
                      / PRECHARGE_ADC_GAIN_DEN;
        load_mv[i] = (PRECHARGE_ADC_ZERO_MV - adc_mv[2u * i + 1u]) * PRECHARGE_ADC_GAIN_NUM
                     / PRECHARGE_ADC_GAIN_DEN;
    }
    return 0;
}

/**
 ******************************************************************************
 * @brief      设置四路预充输出。
 * @param[in]  active:true 吸合，false 释放。
 * @retval     0:全部输出设置成功。
 * @retval     负值:首个 GPIO 错误。吸合失败即返回，释放时仍尝试其余输出。
 ******************************************************************************
 */
int precharge_set_relays(bool active)
{
    int first_error = 0;

    for (unsigned int i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        int err = app_board_gpio_write((board_gpio_output_e)(BOARD_GPIO_OUT_JDQ1 + i), active);

        if (err && !first_error)
        {
            first_error = err;
            if (active)
            {
                break;
            }
        }
    }
    return first_error;
}

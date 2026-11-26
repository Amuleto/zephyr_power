#include <errno.h>

#include <zephyr/kernel.h>

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
 * @param[out] snapshot:输出快照，成功时按位图读取各路电压。
 * @retval     0:读取成功。
 * @retval     -EINVAL:输出地址为空。
 * @retval     其他负值:ADC 采样失败。
 ******************************************************************************
 */
int precharge_read_snapshot(precharge_adc_snapshot_t *snapshot)
{
    int32_t adc_mv[2u * PRECHARGE_CHANNEL_COUNT];
    int err;

    if (snapshot == NULL)
    {
        return -EINVAL;
    }

    err = app_board_adc_read_all(adc_mv, 2u * PRECHARGE_CHANNEL_COUNT);
    if (err)
    {
        return err;
    }

    *snapshot = (precharge_adc_snapshot_t){.read_ms = k_uptime_get()};
    for (unsigned int i = 0; i < PRECHARGE_CHANNEL_COUNT; i++)
    {
        int32_t input = adc_mv[2u * i];
        int32_t load = adc_mv[2u * i + 1u];

        /* 贴地读数可能是失效输入，此时不能换算为高压。 */
        if (input <= 0 || load <= 0)
        {
            continue;
        }
        snapshot->input_mv[i] = (PRECHARGE_ADC_ZERO_MV - input) * PRECHARGE_ADC_GAIN_NUM
                                / PRECHARGE_ADC_GAIN_DEN;
        snapshot->load_mv[i] = (PRECHARGE_ADC_ZERO_MV - load) * PRECHARGE_ADC_GAIN_NUM
                               / PRECHARGE_ADC_GAIN_DEN;
        snapshot->valid_channels |= BIT(i);
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

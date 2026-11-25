#include <errno.h>
#include <string.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include <gd32_adc.h>
#include <gd32f30x_dma.h>
#include <gd32_rcu.h>

#include "board_adc.h"

/** 循环 DMA 保留两轮，供读取端复制上一完整轮。 */
#define ADC_DMA_SCANS 2u
/** ADC0 和 ADC1 的配对结果由 DMA0 通道 0 搬运。 */
#define ADC_DMA_CHANNEL DMA_CH0
/** 四对回读加一组 PB0 监测。 */
#define ADC_SCAN_RANKS 5u
/** DMA 缓冲中的 32 位结果字数。 */
#define ADC_DMA_COUNT (ADC_DMA_SCANS * ADC_SCAN_RANKS)
/** PA0～PA7 的回读通道数量。 */
#define ADC_MEASUREMENT_COUNT (APP_ADC_PA7 + 1u)
/** ADC1 两个校准阶段共用的启动超时，单位为毫秒。 */
#define ADC_CALIBRATION_TIMEOUT_MS 10u

/** ADC 输入列表及名称所在的设备树节点。 */
#define ADC_USER_NODE DT_PATH(zephyr_user)
/** 展开 io-channels 的一项，供 Zephyr 设备树宏生成静态配置表。 */
#define ADC_SPEC(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec inputs[] = {
    DT_FOREACH_PROP_ELEM(ADC_USER_NODE, io_channels, ADC_SPEC)};

static const char *const input_names[] = DT_PROP(ADC_USER_NODE, io_channel_names);

static K_MUTEX_DEFINE(adc_lock);
static bool adc_ready;
/* 低半字为 ADC0，高半字为同节拍的 ADC1。 */
static volatile uint32_t adc_dma_samples[ADC_DMA_COUNT];

BUILD_ASSERT(ARRAY_SIZE(inputs) == ARRAY_SIZE(input_names));
BUILD_ASSERT(ARRAY_SIZE(inputs) == 9);
BUILD_ASSERT(!DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(dma0)));
BUILD_ASSERT(!DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(adc1)));

/**
 ******************************************************************************
 * @brief      配置双 ADC 并行扫描及循环 DMA，并启动首次转换。
 * @retval     0:连续采集已启动。
 * @retval     -ETIMEDOUT:ADC1 校准超时。
 * @details    ADC0 已由 Zephyr 初始化。ADC1 由本模块独占。
 ******************************************************************************
 */
static int start_adc_stream(void)
{
    int64_t deadline_ms;

    rcu_periph_clock_enable(RCU_ADC1);
    adc_enable(ADC1);
    k_busy_wait(2u);
    deadline_ms = k_uptime_get() + ADC_CALIBRATION_TIMEOUT_MS;
    ADC_CTL1(ADC1) |= ADC_CTL1_RSTCLB;
    while (ADC_CTL1(ADC1) & ADC_CTL1_RSTCLB)
    {
        if (k_uptime_get() >= deadline_ms)
        {
            return -ETIMEDOUT;
        }
    }
    ADC_CTL1(ADC1) |= ADC_CTL1_CLB;
    while (ADC_CTL1(ADC1) & ADC_CTL1_CLB)
    {
        if (k_uptime_get() >= deadline_ms)
        {
            return -ETIMEDOUT;
        }
    }

    rcu_periph_clock_enable(RCU_DMA0);
    ADC_CTL1(ADC0) &= ~(ADC_CTL1_ETERC | ADC_CTL1_CTN | ADC_CTL1_ETSRC);
    ADC_CTL0(ADC0) |= ADC_SCAN_MODE;
    ADC_CTL0(ADC1) |= ADC_SCAN_MODE;
    /* 规则并行模式要求从 ADC 选择软件触发，由 ADC0 同步启动。 */
    ADC_CTL1(ADC1) &= ~ADC_CTL1_ETSRC;
    ADC_CTL1(ADC1) |= ADC_CTL1_CTN | ADC_CTL1_ETERC | ADC0_1_2_EXTTRIG_ROUTINE_NONE;
    adc_mode_config(ADC_DAUL_ROUTINE_PARALLEL);
    adc_channel_length_config(ADC0, ADC_ROUTINE_CHANNEL, ADC_SCAN_RANKS);
    adc_channel_length_config(ADC1, ADC_ROUTINE_CHANNEL, ADC_SCAN_RANKS);
    for (size_t i = 0; i < ADC_MEASUREMENT_COUNT / 2u; i++)
    {
        adc_routine_channel_config(ADC0, i, inputs[i * 2u].channel_id, ADC_SAMPLETIME_239POINT5);
        adc_routine_channel_config(ADC1, i, inputs[i * 2u + 1u].channel_id,
                                   ADC_SAMPLETIME_239POINT5);
    }
    /* PB0 单独占最后一个节拍。ADC1 同步转换 PA1，其结果不使用。 */
    adc_routine_channel_config(ADC0, ADC_SCAN_RANKS - 1u, inputs[ADC_MEASUREMENT_COUNT].channel_id,
                               ADC_SAMPLETIME_239POINT5);
    adc_routine_channel_config(ADC1, ADC_SCAN_RANKS - 1u, inputs[1].channel_id,
                               ADC_SAMPLETIME_239POINT5);

    DMA_CHCTL(DMA0, ADC_DMA_CHANNEL) = 0;
    DMA_INTC(DMA0) = DMA_CHINTF_RESET_VALUE;
    DMA_CHPADDR(DMA0, ADC_DMA_CHANNEL) = (uintptr_t)&ADC_RDATA(ADC0);
    DMA_CHMADDR(DMA0, ADC_DMA_CHANNEL) = (uintptr_t)adc_dma_samples;
    DMA_CHCNT(DMA0, ADC_DMA_CHANNEL) = ADC_DMA_COUNT;
    DMA_CHCTL(DMA0, ADC_DMA_CHANNEL) = DMA_CHXCTL_CMEN | DMA_CHXCTL_MNAGA
                                       | DMA_PERIPHERAL_WIDTH_32BIT | DMA_MEMORY_WIDTH_32BIT
                                       | DMA_PRIORITY_HIGH | DMA_CHXCTL_CHEN;

    /* 先启用 DMA，再触发一次 ADC，后续扫描由硬件连续执行。 */
    ADC_CTL1(ADC0) |= ADC_CTL1_DMA | ADC_CTL1_CTN | ADC0_1_2_EXTTRIG_ROUTINE_NONE | ADC_CTL1_ETERC;
    adc_software_trigger_enable(ADC0, ADC_ROUTINE_CHANNEL);
    return 0;
}

/**
 ******************************************************************************
 * @brief      复制 DMA 缓冲区中最近一轮完整采样。
 * @param[out] samples:可容纳全部 ADC 通道的输出数组。
 * @retval     0:已复制完整采样。
 * @retval     -EIO:DMA 报告传输错误。
 * @retval     -EAGAIN:DMA 尚未完成首圈传输，或未取得稳定快照。
 ******************************************************************************
 */
static int adc_snapshot(uint16_t *samples)
{
    if ((DMA_INTF(DMA0) & DMA_FLAG_ERR) != 0)
    {
        return -EIO;
    }
    if ((DMA_INTF(DMA0) & DMA_FLAG_FTF) == 0)
    {
        return -EAGAIN;
    }

    for (int attempt = 0; attempt < 4; attempt++)
    {
        unsigned int key = irq_lock();
        uint32_t remaining = DMA_CHCNT(DMA0, ADC_DMA_CHANNEL);
        size_t next_scan = (ADC_DMA_COUNT - remaining) / ADC_SCAN_RANKS;
        size_t previous = (next_scan + ADC_DMA_SCANS - 1) % ADC_DMA_SCANS;
        bool stable;

        /* 复制期间若 DMA 计数变化，重新获取上一轮完整采样。 */
        for (size_t i = 0; i < ADC_SCAN_RANKS - 1u; i++)
        {
            uint32_t pair = adc_dma_samples[previous * ADC_SCAN_RANKS + i];

            samples[i * 2u] = (uint16_t)pair;
            samples[i * 2u + 1u] = (uint16_t)(pair >> 16);
        }
        samples[ADC_MEASUREMENT_COUNT] =
            (uint16_t)adc_dma_samples[previous * ADC_SCAN_RANKS + ADC_SCAN_RANKS - 1u];
        stable = remaining == DMA_CHCNT(DMA0, ADC_DMA_CHANNEL);
        irq_unlock(key);
        if (stable)
        {
            return 0;
        }
    }
    return -EAGAIN;
}

/**
 ******************************************************************************
 * @brief      配置 ADC 通道并启动连续 DMA 采样。
 * @retval     0:初始化成功。
 * @retval     -ENODEV:ADC 设备未就绪。
 * @retval     其他负值:ADC 通道配置失败。
 * @details    由板级初始化在线程上下文调用一次。
 ******************************************************************************
 */
int app_board_adc_init(void)
{
    int err;

    for (size_t i = 0; i < ARRAY_SIZE(inputs); i++)
    {
        if (!adc_is_ready_dt(&inputs[i]))
        {
            return -ENODEV;
        }
        err = adc_channel_setup_dt(&inputs[i]);
        if (err)
        {
            return err;
        }
    }

    err = start_adc_stream();
    if (err)
    {
        return err;
    }

    k_mutex_lock(&adc_lock, K_FOREVER);
    adc_ready = true;
    k_mutex_unlock(&adc_lock);
    return 0;
}

/**
 ******************************************************************************
 * @brief      获取当前已完成的一轮 ADC 采样。
 * @param[out] samples:至少容纳全部 ADC 通道的数组。
 * @retval     0:读取成功。
 * @retval     -EAGAIN:采样尚未就绪。
 * @retval     -EIO:DMA 传输错误。
 ******************************************************************************
 */
static int read_adc_samples(uint16_t *samples)
{
    int err;

    k_mutex_lock(&adc_lock, K_FOREVER);
    err = -EAGAIN;
    if (adc_ready)
    {
        err = adc_snapshot(samples);
    }
    k_mutex_unlock(&adc_lock);
    return err;
}

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
int app_board_adc_get_voltage(app_adc_channel_e channel, int32_t *voltage_mv)
{
    uint16_t samples[ARRAY_SIZE(inputs)];
    int32_t millivolts;
    int err;

    if ((unsigned int)channel >= ADC_MEASUREMENT_COUNT || voltage_mv == NULL)
    {
        return -EINVAL;
    }

    err = read_adc_samples(samples);
    if (err)
    {
        return err;
    }

    millivolts = samples[channel];
    err = adc_raw_to_millivolts_dt(&inputs[channel], &millivolts);
    if (err)
    {
        return err;
    }

    *voltage_mv = millivolts;
    return 0;
}

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
int app_board_adc_read_all(int32_t *voltages_mv, size_t count)
{
    uint16_t samples[ARRAY_SIZE(inputs)];
    int32_t converted[ADC_MEASUREMENT_COUNT];
    int err;

    if (voltages_mv == NULL || count < ARRAY_SIZE(converted))
    {
        return -EINVAL;
    }

    err = read_adc_samples(samples);
    if (err)
    {
        return err;
    }

    for (size_t i = 0; i < ARRAY_SIZE(converted); i++)
    {
        converted[i] = samples[i];
        err = adc_raw_to_millivolts_dt(&inputs[i], &converted[i]);
        if (err)
        {
            return err;
        }
    }

    memcpy(voltages_mv, converted, sizeof(converted));
    return 0;
}

/**
 ******************************************************************************
 * @brief      打印九路 ADC 的原始值和名义引脚电压。
 * @param[in]  sh:输出诊断结果的 Shell 实例。
 * @param[in]  argc:参数数量，由 Shell 校验。
 * @param[in]  argv:命令参数，未使用。
 * @retval     0:读取成功。
 * @retval     负值:采样或电压换算失败。
 ******************************************************************************
 */
static int cmd_board_adc(const struct shell *sh, size_t argc, char **argv)
{
    uint16_t samples[ARRAY_SIZE(inputs)];
    int32_t millivolts;
    int err;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    err = read_adc_samples(samples);
    if (err)
    {
        shell_error(sh, "ADC sample unavailable: %d", err);
        return err;
    }

    for (size_t i = 0; i < ARRAY_SIZE(inputs); i++)
    {
        millivolts = samples[i];
        err = adc_raw_to_millivolts_dt(&inputs[i], &millivolts);
        if (err)
        {
            return err;
        }
        shell_print(sh, "ADC%u_IN%u (%s): raw=%d, pin=%d mV (nominal VDDA)", (unsigned int)(i % 2u),
                    (unsigned int)inputs[i].channel_id, input_names[i], samples[i], millivolts);
    }
    return 0;
}

/**
 ******************************************************************************
 * @brief      打印 PA0～PA7 同一轮采样的引脚电压。
 * @param[in]  sh:输出结果的 Shell 实例。
 * @param[in]  argc:参数数量，由 Shell 校验。
 * @param[in]  argv:命令参数，未使用。
 * @retval     0:读取成功。
 * @retval     负值:采样或电压换算失败。
 ******************************************************************************
 */
static int cmd_adc(const struct shell *sh, size_t argc, char **argv)
{
    int32_t voltage_mv[ADC_MEASUREMENT_COUNT];
    int err;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    err = app_board_adc_read_all(voltage_mv, ARRAY_SIZE(voltage_mv));
    if (err)
    {
        shell_error(sh, "ADC error: %d", err);
        return err;
    }

    shell_print(sh, "mV: PA0=%d PA1=%d PA2=%d PA3=%d PA4=%d PA5=%d PA6=%d PA7=%d", voltage_mv[0],
                voltage_mv[1], voltage_mv[2], voltage_mv[3], voltage_mv[4], voltage_mv[5],
                voltage_mv[6], voltage_mv[7]);
    return 0;
}

SHELL_CMD_ARG_REGISTER(adc, NULL, "Show PA0-PA7 voltages in mV", cmd_adc, 1, 0);

SHELL_STATIC_SUBCMD_SET_CREATE(board_commands,
    SHELL_CMD_ARG(adc, NULL, "Read eight measurement inputs and the 3 V monitor",
                  cmd_board_adc, 1, 0),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(board, &board_commands, "Board diagnostics", NULL);

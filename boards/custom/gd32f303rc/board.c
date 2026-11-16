#include <zephyr/init.h>

#include <gd32_gpio.h>
#include <gd32_rcu.h>

static int precharge_board_init(void)
{
    /* 释放 PB3/PB4 的 JTAG 复用以控制继电器，保留 PA13/PA14 的 SWD。 */
    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);

#if defined(CONFIG_ADC)
    /* APB2 为 120 MHz，板级代码设置 ADC 公共分频为 4。 */
    rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV4);
#endif /* CONFIG_ADC */

    return 0;
}

/* 在 ADC 驱动初始化和校准前设置分频。 */
SYS_INIT(precharge_board_init, PRE_KERNEL_1, 50);

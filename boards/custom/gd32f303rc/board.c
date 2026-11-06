#include <zephyr/init.h>

#include <gd32_gpio.h>
#include <gd32_rcu.h>

static int precharge_board_init(void)
{
	/* PB3/PB4 control relays: release JTAG while keeping PA13/PA14 SWD usable. */
	rcu_periph_clock_enable(RCU_AF);
	gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);

#if defined(CONFIG_ADC)
	/* APB2 is 120 MHz. The F30x ADC driver does not set this shared divider. */
	rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV6);
#endif

	return 0;
}

/* Run before POST_KERNEL ADC initialization and its calibration sequence. */
SYS_INIT(precharge_board_init, PRE_KERNEL_1, 50);

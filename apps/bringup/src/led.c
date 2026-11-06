#include <errno.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "led.h"

enum led_mode {
	LED_OFF,
	LED_ON,
	LED_BLINK,
};

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static K_MUTEX_DEFINE(led_lock);
static enum led_mode mode = LED_BLINK;
static bool led_ready;

int app_led_init(void)
{
	int err;

	k_mutex_lock(&led_lock, K_FOREVER);
	err = gpio_is_ready_dt(&led) ?
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) : -ENODEV;
	led_ready = (err == 0);
	k_mutex_unlock(&led_lock);

	return err;
}

int app_led_update(void)
{
	int err;

	k_mutex_lock(&led_lock, K_FOREVER);
	err = mode == LED_BLINK ? gpio_pin_toggle_dt(&led) : 0;
	k_mutex_unlock(&led_lock);

	return err;
}

static int cmd_led(const struct shell *sh, size_t argc, char **argv)
{
	enum led_mode next;
	int err;

	ARG_UNUSED(argc);

	if (!strcmp(argv[1], "on")) {
		next = LED_ON;
	} else if (!strcmp(argv[1], "off")) {
		next = LED_OFF;
	} else if (!strcmp(argv[1], "blink")) {
		next = LED_BLINK;
	} else {
		shell_error(sh, "Usage: led <on|off|blink>");
		return -EINVAL;
	}

	/* Shell commands and the main loop share the GPIO and mode state. */
	k_mutex_lock(&led_lock, K_FOREVER);
	err = led_ready ? gpio_pin_set_dt(&led, next != LED_OFF) : -ENODEV;
	if (!err) {
		mode = next;
	}
	k_mutex_unlock(&led_lock);

	if (err) {
		shell_error(sh, "LED unavailable: %d", err);
		return err;
	}

	shell_print(sh, "LED mode: %s", argv[1]);
	return 0;
}

SHELL_CMD_ARG_REGISTER(led, NULL, "LED control: on, off, blink", cmd_led, 2, 0);

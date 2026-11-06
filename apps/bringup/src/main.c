#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "board_io.h"
#include "led.h"
#include "params.h"
#include "serial_rx.h"

#define MAIN_WATCHDOG_TIMEOUT_MS 3000
#define MAIN_LOOP_INTERVAL_MS 500

LOG_MODULE_REGISTER(bringup, LOG_LEVEL_INF);

static int cmd_uptime(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Uptime: %lld ms", (long long)k_uptime_get());
	return 0;
}

SHELL_CMD_ARG_REGISTER(uptime, NULL, "Show time since boot", cmd_uptime, 1, 0);

int main(void)
{
	const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(fwdgt));
	int health_channel;
	int err;

	err = app_board_io_init();
	if (err) {
		LOG_ERR("Board I/O initialization failed: %d", err);
		return 0;
	}

	err = app_serial_rx_init();
	if (err) {
		LOG_ERR("Business UART RX initialization failed: %d", err);
		return 0;
	}

	err = app_params_init();
	if (err) {
		LOG_ERR("Parameter initialization failed: %d", err);
		return 0;
	}

	err = app_led_init();
	if (err) {
		LOG_ERR("LED initialization failed: %d", err);
		return 0;
	}

	LOG_INF("Board %s ready; use help, led on/off/blink, uptime", CONFIG_BOARD);
	if (!device_is_ready(DEVICE_DT_GET(DT_ALIAS(rtc0)))) {
		LOG_ERR("RTC HXTAL/128 initialization failed: %d", -ENODEV);
	} else {
		LOG_INF("Hardware RTC: 8 MHz HXTAL/128, prescaler 62499; "
			"set date to enable UTC time");
	}

	if (!device_is_ready(wdt)) {
		LOG_ERR("Hardware watchdog not ready");
		return 0;
	}

	err = task_wdt_init(wdt);
	if (err) {
		LOG_ERR("Watchdog initialization failed: %d", err);
		return 0;
	}

	health_channel = task_wdt_add(MAIN_WATCHDOG_TIMEOUT_MS, NULL, NULL);
	if (health_channel < 0) {
		LOG_ERR("Watchdog registration failed: %d", health_channel);
		return 0;
	}

	LOG_INF("Main task monitored: %d ms; hardware fallback enabled",
		MAIN_WATCHDOG_TIMEOUT_MS);

	/* Confirm only after the current startup checks have succeeded. */
	err = boot_write_img_confirmed();
	if (err) {
		LOG_ERR("Image confirmation failed: %d", err);
		return 0;
	}

	for (;;) {
		err = app_led_update();
		if (err) {
			LOG_ERR("LED toggle failed: %d", err);
			return 0;
		}

		/* Report health only after the monitored work has completed. */
		task_wdt_feed(health_channel);
		k_msleep(MAIN_LOOP_INTERVAL_MS);
	}
}

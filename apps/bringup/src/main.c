#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <string.h>
#include <errno.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/drivers/rtc.h>

LOG_MODULE_REGISTER(bringup, LOG_LEVEL_INF);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static K_MUTEX_DEFINE(led_lock);
enum led_mode { LED_OFF, LED_ON, LED_BLINK };
static enum led_mode mode = LED_BLINK;
static bool led_ready;

static int cmd_led(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    enum led_mode next;
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
    k_mutex_lock(&led_lock, K_FOREVER);
    int err = led_ready ? gpio_pin_set_dt(&led, next != LED_OFF) : -ENODEV;
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

static int cmd_uptime(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shell_print(sh, "Uptime: %lld ms", (long long)k_uptime_get());
    return 0;
}

SHELL_CMD_ARG_REGISTER(led, NULL, "LED control: on, off, blink", cmd_led, 2, 0);
SHELL_CMD_ARG_REGISTER(uptime, NULL, "Show time since boot", cmd_uptime, 1, 0);

int main(void)
{
    k_mutex_lock(&led_lock, K_FOREVER);
    int err = gpio_is_ready_dt(&led) ?
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) : -ENODEV;
    led_ready = (err == 0);
    k_mutex_unlock(&led_lock);
    if (err) {
        LOG_ERR("LED initialization failed: %d", err);
        return 0;
    }
    LOG_INF("Board %s ready; use help, led on/off/blink, uptime", CONFIG_BOARD);
    err = device_is_ready(DEVICE_DT_GET(DT_ALIAS(rtc0))) ? 0 : -ENODEV;
    if (err) {
        LOG_ERR("RTC HXTAL/128 initialization failed: %d", err);
    } else {
        LOG_INF("Hardware RTC: 8 MHz HXTAL/128, prescaler 62499; set date to enable UTC time");
    }
    const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(fwdgt));
    if (!device_is_ready(wdt)) {
        LOG_ERR("Hardware watchdog not ready");
        return 0;
    }
    err = task_wdt_init(wdt);
    if (err) {
        LOG_ERR("Watchdog initialization failed: %d", err);
        return 0;
    }
    int health_channel = task_wdt_add(3000, NULL, NULL);
    if (health_channel < 0) {
        LOG_ERR("Watchdog registration failed: %d", health_channel);
        return 0;
    }
    LOG_INF("Main task monitored: 3000 ms; hardware fallback enabled");
    for (;;) {
        k_mutex_lock(&led_lock, K_FOREVER);
        err = mode == LED_BLINK ? gpio_pin_toggle_dt(&led) : 0;
        k_mutex_unlock(&led_lock);
        if (err) {
            LOG_ERR("LED toggle failed: %d", err);
            return 0;
        }
        task_wdt_feed(health_channel);
        k_msleep(500);
    }
}

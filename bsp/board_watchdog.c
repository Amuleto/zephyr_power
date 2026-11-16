#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "board_watchdog.h"

LOG_MODULE_REGISTER(board_watchdog, LOG_LEVEL_INF);

int board_watchdog_init(void)
{
    const struct device *const wdt = DEVICE_DT_GET(DT_NODELABEL(fwdgt));
    int err;

    if (!device_is_ready(wdt))
    {
        LOG_ERR("Hardware watchdog not ready: %d", -ENODEV);
        return -ENODEV;
    }

    err = task_wdt_init(wdt);
    if (err)
    {
        LOG_ERR("Watchdog initialization failed: %d", err);
    }
    return err;
}

int board_watchdog_register(uint32_t timeout_ms)
{
    int channel = task_wdt_add(timeout_ms, NULL, NULL);

    if (channel < 0)
    {
        LOG_ERR("Watchdog registration failed: %d", channel);
    }

    return channel;
}

int board_watchdog_feed(int channel)
{
    int err = channel < 0 ? -EAGAIN : task_wdt_feed(channel);

    if (err)
    {
        LOG_ERR("Watchdog feed failed: %d", err);
    }
    return err;
}

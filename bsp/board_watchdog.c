#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "board_watchdog.h"

LOG_MODULE_REGISTER(board_watchdog, LOG_LEVEL_INF);

/**
 ******************************************************************************
 * @brief      检查看门狗设备并初始化任务看门狗服务。
 * @retval     0:初始化成功。
 * @retval     负值:设备未就绪或服务初始化失败。
 ******************************************************************************
 */
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

/**
 ******************************************************************************
 * @brief      注册独立任务通道并启动运行监控。
 * @param[in]  timeout_ms:任务报告进度的最大间隔，单位为毫秒。
 * @retval     非负值:任务看门狗通道编号。
 * @retval     负值:注册失败。
 ******************************************************************************
 */
int board_watchdog_register(uint32_t timeout_ms)
{
    int channel = task_wdt_add(timeout_ms, NULL, NULL);

    if (channel < 0)
    {
        LOG_ERR("Watchdog registration failed: %d", channel);
    }

    return channel;
}

/**
 ******************************************************************************
 * @brief      报告注册任务已完成一轮工作。
 * @param[in]  channel:该任务的看门狗通道编号。
 * @retval     0:喂狗成功。
 * @retval     负值:通道未启动或喂狗失败。
 ******************************************************************************
 */
int board_watchdog_feed(int channel)
{
    int err;

    if (channel < 0)
    {
        return -EAGAIN;
    }

    err = task_wdt_feed(channel);

    if (err)
    {
        LOG_ERR("Watchdog feed failed: %d", err);
    }
    return err;
}

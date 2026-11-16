#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "board_gpio.h"
#include "board_watchdog.h"
#include "mp10.h"
#include "precharge.h"

/** 预充回读的联调间隔，单位为毫秒，量产值依据实际检测时限确定。 */
#define PRECHARGE_TASK_INTERVAL_MS 20u
/** 心跳灯相邻两次翻转的间隔，单位为毫秒。 */
#define PRECHARGE_LED_INTERVAL_MS 500u
/** 预充线程沿用此前主线程的栈预算，单位为字节。 */
#define PRECHARGE_TASK_STACK_SIZE 1024u
/** 预充线程优先级，高于 RS485 通信线程。 */
#define PRECHARGE_TASK_PRIORITY 0

/** Precharge 线程进度上报的超时，单位为毫秒。 */
#define PRECHARGE_WATCHDOG_TIMEOUT_MS 3000u

LOG_MODULE_REGISTER(precharge, LOG_LEVEL_ERR);

static K_THREAD_STACK_DEFINE(precharge_stack, PRECHARGE_TASK_STACK_SIZE);
static struct k_thread precharge_thread;

/**
 ******************************************************************************
 * @brief      到达设定时间时翻转运行灯。
 * @retval     None
 ******************************************************************************
 */
static void led_flash(void)
{
    static int64_t next_led_update_ms;
    static bool led_active;

    int64_t now_ms = k_uptime_get();
    int err;

    if (now_ms < next_led_update_ms)
    {
        return;
    }

    err = app_board_gpio_write(BOARD_GPIO_OUT_LED, !led_active);
    if (!err)
    {
        led_active = !led_active;
    }
    else
    {
        LOG_ERR("Heartbeat LED update failed: %d", err);
    }
    next_led_update_ms = now_ms + PRECHARGE_LED_INTERVAL_MS;
}

/**
 ******************************************************************************
 * @brief      周期更新预充回读和心跳，并报告任务进度。
 * @param[in]  arg1:Zephyr 保留参数，未使用。
 * @param[in]  arg2:Zephyr 保留参数，未使用。
 * @param[in]  arg3:Zephyr 保留参数，未使用。
 * @retval     None
 *
 * @details    看门狗操作失败时退出线程。
 ******************************************************************************
 */
static void precharge_entry(void *arg1, void *arg2, void *arg3)
{
    mp10_context_t mp10 = {0};
    int channel;
    int err;

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    channel = board_watchdog_register(PRECHARGE_WATCHDOG_TIMEOUT_MS);
    if (channel < 0)
    {
        LOG_ERR("Precharge watchdog registration failed: %d", channel);
        return;
    }

    err = mp10_init();
    if (err)
    {
        LOG_ERR("MP10 receive filter setup failed: %d", err);
    }

    for (;;)
    {
        led_flash();

        mp10_task(&mp10);

        err = board_watchdog_feed(channel);
        if (err)
        {
            return;
        }

        k_msleep(PRECHARGE_TASK_INTERVAL_MS);
    }
}

/**
 ******************************************************************************
 * @brief      启动预充线程。
 * @retval     None
 * @details    在板级初始化和参数加载完成后调用一次。
 ******************************************************************************
 */
void precharge_start(void)
{
    (void)k_thread_create(&precharge_thread, precharge_stack,
                         K_THREAD_STACK_SIZEOF(precharge_stack), precharge_entry,
                         NULL, NULL, NULL, PRECHARGE_TASK_PRIORITY, 0, K_NO_WAIT);
}

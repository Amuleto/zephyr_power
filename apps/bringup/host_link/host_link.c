#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "board_watchdog.h"
#include "host_link.h"
#include "serial_rx.h"

/** RS485 通信线程栈大小，单位为字节。 */
#define HOST_LINK_STACK_SIZE 1024u
/** 通信线程优先级，低于预充线程。 */
#define HOST_LINK_PRIORITY 5
/** 总线空闲时的最长单次等待，单位为毫秒。 */
#define HOST_LINK_RX_WAIT_MS 100u

/** RS485 线程进度上报的超时，单位为毫秒。 */
#define HOST_LINK_WATCHDOG_TIMEOUT_MS 3000u

LOG_MODULE_REGISTER(host_link, LOG_LEVEL_WRN);

static K_THREAD_STACK_DEFINE(host_link_stack, HOST_LINK_STACK_SIZE);
static struct k_thread host_link_thread;

/**
 ******************************************************************************
 * @brief      接收 RS485 数据，预留主板协议接入点。
 * @param[in]  arg1:Zephyr 保留参数，未使用。
 * @param[in]  arg2:线程参数，未使用。
 * @param[in]  arg3:线程参数，未使用。
 * @retval     None
 *
 * @details    主板协议确定前只消费接收数据。
 ******************************************************************************
 */
static void host_link_entry(void *arg1, void *arg2, void *arg3)
{
    uint8_t bytes[32];
    int channel;

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    channel = board_watchdog_register(HOST_LINK_WATCHDOG_TIMEOUT_MS);
    if (channel < 0)
    {
        LOG_ERR("RS485 watchdog registration failed: %d", channel);
        return;
    }

    for (;;)
    {
        ssize_t received = app_serial_rx_read(APP_SERIAL_RS485, bytes, sizeof(bytes),
                                             K_MSEC(HOST_LINK_RX_WAIT_MS));

        if (received < 0)
        {
            LOG_ERR("RS485 receive failed: %d", (int)received);
            return;
        }

        /* 空闲超时也是有效进度，卡在收发路径时不会执行到这里。 */
        if (board_watchdog_feed(channel))
        {
            return;
        }
    }
}

/**
 ******************************************************************************
 * @brief      启动 RS485 接收与应答线程。
 * @retval     None
 * @details    在串口和应用参数就绪后调用一次。
 ******************************************************************************
 */
void host_link_start(void)
{
    (void)k_thread_create(&host_link_thread, host_link_stack,
                        K_THREAD_STACK_SIZEOF(host_link_stack), host_link_entry,
                        NULL, NULL, NULL, HOST_LINK_PRIORITY, 0, K_NO_WAIT);
}

#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "board_gpio.h"
#include "rs485_tx.h"

/** 最后一个字节写入后等待移位寄存器发送完成的上限，单位为毫秒。 */
#define RS485_TX_DRAIN_TIMEOUT_MS 5u

/**
 ******************************************************************************
 * @brief      发送 RS485 数据，结束后切回接收。
 * @param[in]  data:待发送数据，由调用方持有。
 * @param[in]  length:数据字节数。
 * @retval     0:发送成功。
 * @retval     -EINVAL:参数无效。
 * @retval     -ETIMEDOUT:等待最后一位发送超时。
 * @retval     其他负值:方向 GPIO 控制失败或串口驱动不支持发送完成查询。
 * @details    由 RS485 通信线程独占调用。
 ******************************************************************************
 */
int board_rs485_send(const uint8_t *data, size_t length)
{
    const struct device *const uart = DEVICE_DT_GET(DT_ALIAS(rs485_uart));
    int64_t deadline_ms;
    int complete;
    int err;

    if (data == NULL || length == 0u)
    {
        return -EINVAL;
    }

    err = app_board_gpio_write(BOARD_GPIO_OUT_RS485_DE, true);
    if (err)
    {
        return err;
    }

    for (size_t i = 0; i < length; i++)
    {
        uart_poll_out(uart, data[i]);
    }

    /* TBE 只表示数据寄存器可写，TC 才表示移位寄存器已清空。 */
    deadline_ms = k_uptime_get() + RS485_TX_DRAIN_TIMEOUT_MS;
    while ((complete = uart_irq_tx_complete(uart)) == 0 && k_uptime_get() < deadline_ms)
    {
        k_busy_wait(10u);
    }

    err = app_board_gpio_write(BOARD_GPIO_OUT_RS485_DE, false);
    if (err)
    {
        return err;
    }

    if (complete < 0)
    {
        return complete;
    }
    if (!complete)
    {
        return -ETIMEDOUT;
    }
    return 0;
}

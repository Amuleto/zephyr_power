#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "board_gpio.h"
#include "rs485_tx.h"

#define RS485_TX_DRAIN_TIMEOUT_MS 5u

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

    return complete > 0 ? 0 : (complete < 0 ? complete : -ETIMEDOUT);
}

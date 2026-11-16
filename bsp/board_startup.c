#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

#include "board_adc.h"
#include "board_gpio.h"
#include "board_startup.h"
#include "board_watchdog.h"
#include "serial_rx.h"

LOG_MODULE_REGISTER(board_startup, LOG_LEVEL_INF);

/**
 ******************************************************************************
 * @brief      启动板级 CAN 控制器。
 * @retval     0:控制器已启动。
 * @retval     负值:控制器未就绪或启动失败。
 ******************************************************************************
 */
static int start_can(void)
{
    const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
    int err;

    if (!device_is_ready(can_dev))
    {
        return -ENODEV;
    }

    err = can_start(can_dev);
    return err == -EALREADY ? 0 : err;
}

int board_init(void)
{
    int err;

    err = app_board_gpio_init();
    if (err)
    {
        LOG_ERR("Board GPIO initialization failed: %d", err);
        return err;
    }

    err = app_board_adc_init();
    if (err)
    {
        LOG_ERR("Board ADC initialization failed: %d", err);
        return err;
    }

    err = app_serial_rx_init();
    if (err)
    {
        LOG_ERR("Business UART RX initialization failed: %d", err);
        return err;
    }

    /* RTC 供日期命令使用，时钟故障时继续提供其他功能。 */
    if (!device_is_ready(DEVICE_DT_GET(DT_ALIAS(rtc0))))
    {
        LOG_ERR("RTC HXTAL/128 initialization failed: %d", -ENODEV);
    }

    err = start_can();
    if (err)
    {
        LOG_ERR("CAN startup failed: %d", err);
    }

    return board_watchdog_init();
}

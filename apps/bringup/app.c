#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>

#include "app.h"
#include "host_link.h"
#include "precharge.h"

#if defined(CONFIG_SETTINGS_NVS)
#include "params.h"
#endif

LOG_MODULE_REGISTER(bringup_app, LOG_LEVEL_ERR);

/**
 ******************************************************************************
 * @brief      按配置加载参数并启动预充和 RS485 两个线程。
 * @retval     0:线程已启动。
 * @retval     负值:参数加载或预充线程启动失败。
 *
 * @details    板级初始化完成后调用一次。成功后将控制权交还调用方。
 ******************************************************************************
 */
int app_run(void)
{
    int err;

#if defined(CONFIG_SETTINGS_NVS)
    err = app_params_init();

    if (err)
    {
        LOG_ERR("Application initialization failed: %d", err);
        return err;
    }

#endif

    err = boot_write_img_confirmed();
    if (err)
    {
        LOG_ERR("Image confirmation failed: %d", err);
    }

    err = precharge_start_task();
    if (err)
    {
        return err;
    }

    host_link_start();
    return 0;
}

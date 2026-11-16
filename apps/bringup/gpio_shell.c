#include <errno.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include "board_gpio.h"

/**
 ******************************************************************************
 * @brief      将指定继电器或 PE 输出引脚设置为给定的物理电平。
 * @param[in]  sh:用于输出执行结果的 Shell 实例。
 * @param[in]  argc:参数数量，由 Shell 注册配置校验。
 * @param[in]  argv:命令参数，第 1 项为引脚编号 3～6 或 13，第 2 项为电平 0 或 1。
 * @retval     0:引脚设置成功。
 * @retval     -EINVAL:参数无效或引脚未映射。
 * @retval     其他负值:GPIO 接口返回的错误码。
 *
 * @details    用于板级调试，直接驱动继电器，调用方负责操作时序。
 ******************************************************************************
 */
static int cmd_board_gpio(const struct shell *sh, size_t argc, char **argv)
{
    enum board_gpio_output_e output;
    unsigned int pin;
    bool high;
    int err;

    ARG_UNUSED(argc);

    if (strcmp(argv[1], "13") == 0)
    {
        pin = 13u;
    }
    else if (argv[1][0] >= '3' && argv[1][0] <= '6' && argv[1][1] == '\0')
    {
        pin = (unsigned int)(argv[1][0] - '0');
    }
    else
    {
        shell_error(sh, "Usage: gpio <3-6|13> <0|1>");
        return -EINVAL;
    }

    if ((argv[2][0] != '0' && argv[2][0] != '1') || argv[2][1] != '\0')
    {
        shell_error(sh, "Usage: gpio <3-6|13> <0|1>");
        return -EINVAL;
    }
    high = (argv[2][0] == '1');

    err = app_board_gpio_find_pb(pin, &output);
    if (!err)
    {
        err = app_board_gpio_write_raw(output, high);
    }

    if (err)
    {
        shell_error(sh, "PB%u: %d", pin, err);
        return err;
    }

    shell_print(sh, "PB%u=%s", pin, argv[2]);
    return 0;
}

SHELL_CMD_ARG_REGISTER(gpio, NULL, "Set PB3-PB6 or PB13: gpio <3-6|13> <0|1>",
                       cmd_board_gpio, 3, 0);

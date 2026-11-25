#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "board_gpio.h"

/** 板级输出引脚在设备树中的归属节点。 */
#define BOARD_GPIO_USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec outputs[BOARD_GPIO_OUT_COUNT] = {
        [BOARD_GPIO_OUT_JDQ1] = GPIO_DT_SPEC_GET_BY_IDX(BOARD_GPIO_USER_NODE, relay_gpios, 0),
        [BOARD_GPIO_OUT_JDQ2] = GPIO_DT_SPEC_GET_BY_IDX(BOARD_GPIO_USER_NODE, relay_gpios, 1),
        [BOARD_GPIO_OUT_JDQ3] = GPIO_DT_SPEC_GET_BY_IDX(BOARD_GPIO_USER_NODE, relay_gpios, 2),
        [BOARD_GPIO_OUT_JDQ4] = GPIO_DT_SPEC_GET_BY_IDX(BOARD_GPIO_USER_NODE, relay_gpios, 3),
        [BOARD_GPIO_OUT_LED] = GPIO_DT_SPEC_GET(BOARD_GPIO_USER_NODE, led_gpios),
        [BOARD_GPIO_OUT_RS485_DE] = GPIO_DT_SPEC_GET(BOARD_GPIO_USER_NODE, rs485_de_gpios),
        [BOARD_GPIO_OUT_PE] = GPIO_DT_SPEC_GET(BOARD_GPIO_USER_NODE, pe_gpios),
};

static K_MUTEX_DEFINE(board_lock);
static bool board_ready;

/**
 ******************************************************************************
 * @brief      初始化板级 GPIO 输出，默认置为非有效态。
 * @retval     0:初始化成功。
 * @retval     负值:GPIO 未就绪或配置失败。
 *
 * @details    在线程上下文中调用一次。
 ******************************************************************************
 */
int app_board_gpio_init(void)
{
    /* 继电器默认断开，LED 默认熄灭，RS485 默认接收，PE 默认低电平。 */
    for (size_t i = 0; i < ARRAY_SIZE(outputs); i++)
    {
        int err;

        if (!gpio_is_ready_dt(&outputs[i]))
        {
            return -ENODEV;
        }

        err = gpio_pin_configure_dt(&outputs[i], GPIO_OUTPUT_INACTIVE);
        if (err)
        {
            return err;
        }
    }

    k_mutex_lock(&board_lock, K_FOREVER);
    board_ready = true;
    k_mutex_unlock(&board_lock);
    return 0;
}

static int write_output(board_gpio_output_e output, bool value, bool raw)
{
    const struct gpio_dt_spec *gpio;
    int err = -EAGAIN;

    if ((unsigned int)output >= ARRAY_SIZE(outputs))
    {
        return -EINVAL;
    }

    gpio = &outputs[output];
    k_mutex_lock(&board_lock, K_FOREVER);
    if (board_ready)
    {
        if (raw)
        {
            err = gpio_pin_set_raw(gpio->port, gpio->pin, value);
        }
        else
        {
            err = gpio_pin_set_dt(gpio, value);
        }
    }
    k_mutex_unlock(&board_lock);
    return err;
}

/**
 ******************************************************************************
 * @brief      按设备树定义的极性设置指定输出的逻辑状态。
 * @param[in]  output:输出信号。
 * @param[in]  active:true 表示有效，false 表示非有效。
 * @retval     0:设置成功。
 * @retval     -EINVAL:输出信号无效。
 * @retval     -EAGAIN:GPIO 尚未初始化。
 * @retval     其他负值:GPIO 驱动返回的错误码。
 ******************************************************************************
 */
int app_board_gpio_write(board_gpio_output_e output, bool active)
{
    return write_output(output, active, false);
}

/**
 ******************************************************************************
 * @brief      设置指定输出的物理电平。
 * @param[in]  output:输出信号。
 * @param[in]  high:true 表示高电平，false 表示低电平。
 * @retval     0:设置成功。
 * @retval     -EINVAL:输出信号无效。
 * @retval     -EAGAIN:GPIO 尚未初始化。
 * @retval     其他负值:GPIO 驱动返回的错误码。
 ******************************************************************************
 */
int app_board_gpio_write_raw(board_gpio_output_e output, bool high)
{
    return write_output(output, high, true);
}

/**
 ******************************************************************************
 * @brief      查找映射到 GPIOB 指定引脚的输出信号。
 * @param[in]  pin:GPIOB 引脚编号。
 * @param[out] output:匹配时写入逻辑输出编号。
 * @retval     0:找到输出。
 * @retval     -EINVAL:输出指针为空或引脚未映射。
 ******************************************************************************
 */
int app_board_gpio_find_pb(unsigned int pin, board_gpio_output_e *output)
{
    if (output == NULL)
    {
        return -EINVAL;
    }

    for (size_t i = 0; i < ARRAY_SIZE(outputs); i++)
    {
        if (outputs[i].port == DEVICE_DT_GET(DT_NODELABEL(gpiob)) && outputs[i].pin == pin)
        {
            *output = (board_gpio_output_e)i;
            return 0;
        }
    }

    return -EINVAL;
}

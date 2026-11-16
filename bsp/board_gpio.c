#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "board_gpio.h"

#define BOARD_GPIO_USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec outputs[BOARD_GPIO_OUT_COUNT] =
{
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

static int write_output(enum board_gpio_output_e output, bool value, bool raw)
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
        err = raw ? gpio_pin_set_raw(gpio->port, gpio->pin, value) : gpio_pin_set_dt(gpio, value);
    }
    k_mutex_unlock(&board_lock);
    return err;
}

int app_board_gpio_write(enum board_gpio_output_e output, bool active)
{
    return write_output(output, active, false);
}

int app_board_gpio_write_raw(enum board_gpio_output_e output, bool high)
{
    return write_output(output, high, true);
}

int app_board_gpio_find_pb(unsigned int pin, enum board_gpio_output_e *output)
{
    if (output == NULL)
    {
        return -EINVAL;
    }

    for (size_t i = 0; i < ARRAY_SIZE(outputs); i++)
    {
        if (outputs[i].port == DEVICE_DT_GET(DT_NODELABEL(gpiob)) && outputs[i].pin == pin)
        {
            *output = (enum board_gpio_output_e)i;
            return 0;
        }
    }

    return -EINVAL;
}

#include <errno.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "board_io.h"

#define USER_NODE DT_PATH(zephyr_user)
#define ADC_SPEC(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx),
#define RELAY_SPEC(node_id, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

static const struct adc_dt_spec inputs[] = {
	DT_FOREACH_PROP_ELEM(USER_NODE, io_channels, ADC_SPEC)
};
static const char *const input_names[] = DT_PROP(USER_NODE, io_channel_names);
static const struct gpio_dt_spec relays[] = {
	DT_FOREACH_PROP_ELEM(USER_NODE, relay_gpios, RELAY_SPEC)
};
static const struct gpio_dt_spec rs485_de = GPIO_DT_SPEC_GET(USER_NODE, rs485_de_gpios);

static K_MUTEX_DEFINE(board_lock);
static bool board_ready;

BUILD_ASSERT(ARRAY_SIZE(inputs) == ARRAY_SIZE(input_names));
BUILD_ASSERT(ARRAY_SIZE(relays) == 4);

int app_board_io_init(void)
{
	int err;

	/* Establish inactive outputs before allowing any other board operation. */
	for (size_t i = 0; i < ARRAY_SIZE(relays); i++) {
		if (!gpio_is_ready_dt(&relays[i])) {
			return -ENODEV;
		}
		err = gpio_pin_configure_dt(&relays[i], GPIO_OUTPUT_INACTIVE);
		if (err) {
			return err;
		}
	}

	if (!gpio_is_ready_dt(&rs485_de)) {
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&rs485_de, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}

	for (size_t i = 0; i < ARRAY_SIZE(inputs); i++) {
		if (!adc_is_ready_dt(&inputs[i])) {
			return -ENODEV;
		}
		err = adc_channel_setup_dt(&inputs[i]);
		if (err) {
			return err;
		}
	}

	if (!device_is_ready(DEVICE_DT_GET(DT_ALIAS(factory_uart))) ||
	    !device_is_ready(DEVICE_DT_GET(DT_ALIAS(rs485_uart)))) {
		return -ENODEV;
	}

	k_mutex_lock(&board_lock, K_FOREVER);
	board_ready = true;
	k_mutex_unlock(&board_lock);
	return 0;
}

int app_board_relay_set(size_t index, bool enabled)
{
	int err;

	if (index >= ARRAY_SIZE(relays)) {
		return -EINVAL;
	}

	k_mutex_lock(&board_lock, K_FOREVER);
	err = board_ready ? gpio_pin_set_dt(&relays[index], enabled) : -EAGAIN;
	k_mutex_unlock(&board_lock);
	return err;
}

int app_board_rs485_direction_set(bool transmit)
{
	int err;

	k_mutex_lock(&board_lock, K_FOREVER);
	err = board_ready ? gpio_pin_set_dt(&rs485_de, transmit) : -EAGAIN;
	k_mutex_unlock(&board_lock);
	return err;
}

static int cmd_board_adc(const struct shell *sh, size_t argc, char **argv)
{
	struct adc_sequence sequence;
	int16_t raw;
	int32_t millivolts;
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_mutex_lock(&board_lock, K_FOREVER);
	if (!board_ready) {
		k_mutex_unlock(&board_lock);
		return -EAGAIN;
	}
	k_mutex_unlock(&board_lock);

	for (size_t i = 0; i < ARRAY_SIZE(inputs); i++) {
		sequence = (struct adc_sequence){
			.buffer = &raw,
			.buffer_size = sizeof(raw),
		};
		err = adc_sequence_init_dt(&inputs[i], &sequence);
		if (!err) {
			err = adc_read_dt(&inputs[i], &sequence);
		}
		if (err) {
			shell_error(sh, "%s: ADC read failed: %d", input_names[i], err);
			return err;
		}

		/* Pin voltage only: the high-voltage front-end needs separate calibration. */
		millivolts = raw;
		err = adc_raw_to_millivolts_dt(&inputs[i], &millivolts);
		if (err) {
			return err;
		}
		shell_print(sh, "%s: raw=%d, pin=%d mV (nominal VDDA)",
			    input_names[i], raw, millivolts);
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(board_commands,
	SHELL_CMD_ARG(adc, NULL, "Read eight measurement inputs and the 3 V monitor",
		      cmd_board_adc, 1, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(board, &board_commands, "Board diagnostics", NULL);

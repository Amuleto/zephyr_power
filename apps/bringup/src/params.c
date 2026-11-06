#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>

#include "params.h"

#define PARAMS_DEFAULT_DEVICE_ID 1U
#define PARAMS_DEVICE_ID_KEY "app/device_id"

static K_MUTEX_DEFINE(params_lock);
static uint32_t current_device_id = PARAMS_DEFAULT_DEVICE_ID;
static bool params_ready;
static int load_error;

static int params_load(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	uint8_t encoded[sizeof(uint32_t)];
	ssize_t count;

	if (strcmp(name, "device_id") != 0) {
		return -ENOENT;
	}
	if (len != sizeof(encoded)) {
		load_error = -EINVAL;
		return load_error;
	}

	count = read_cb(cb_arg, encoded, sizeof(encoded));
	if (count < 0) {
		load_error = (int)count;
		return load_error;
	}
	if (count != sizeof(encoded)) {
		load_error = -EIO;
		return load_error;
	}

	/* Stable on-Flash representation, independent of CPU byte order. */
	k_mutex_lock(&params_lock, K_FOREVER);
	current_device_id = sys_get_le32(encoded);
	k_mutex_unlock(&params_lock);
	return 0;
}

static int params_commit(void)
{
	/* Settings load does not propagate h_set errors; report them at commit. */
	return load_error;
}

SETTINGS_STATIC_HANDLER_DEFINE(app_params, "app", NULL, params_load, params_commit, NULL);

int app_params_init(void)
{
	int err;

	err = settings_subsys_init();
	if (err) {
		return err;
	}

	err = settings_load_subtree("app");
	if (err) {
		return err;
	}

	k_mutex_lock(&params_lock, K_FOREVER);
	params_ready = true;
	k_mutex_unlock(&params_lock);
	return 0;
}

int app_params_get_device_id(uint32_t *device_id)
{
	if (device_id == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&params_lock, K_FOREVER);
	if (!params_ready) {
		k_mutex_unlock(&params_lock);
		return -EAGAIN;
	}

	*device_id = current_device_id;
	k_mutex_unlock(&params_lock);
	return 0;
}

int app_params_set_device_id(uint32_t device_id)
{
	uint8_t encoded[sizeof(uint32_t)];
	int err;

	k_mutex_lock(&params_lock, K_FOREVER);
	if (!params_ready) {
		k_mutex_unlock(&params_lock);
		return -EAGAIN;
	}

	if (device_id == current_device_id) {
		k_mutex_unlock(&params_lock);
		return 0;
	}

	sys_put_le32(device_id, encoded);
	err = settings_save_one(PARAMS_DEVICE_ID_KEY, encoded, sizeof(encoded));
	if (!err) {
		current_device_id = device_id;
	}
	k_mutex_unlock(&params_lock);
	return err;
}

static int cmd_param_get(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t device_id;
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	err = app_params_get_device_id(&device_id);
	if (err) {
		shell_error(sh, "Parameter read failed: %d", err);
		return err;
	}

	shell_print(sh, "device_id=%lu", (unsigned long)device_id);
	return 0;
}

static int cmd_param_set(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long parsed;
	char *end;
	int err;

	ARG_UNUSED(argc);

	/* Accept decimal digits only; strtoul alone would accept a minus sign. */
	if (argv[1][0] == '\0' || strspn(argv[1], "0123456789") != strlen(argv[1])) {
		return -EINVAL;
	}

	errno = 0;
	parsed = strtoul(argv[1], &end, 10);
	if (errno == ERANGE || *end != '\0' || parsed > UINT32_MAX) {
		return -ERANGE;
	}

	err = app_params_set_device_id((uint32_t)parsed);
	if (err) {
		shell_error(sh, "Parameter save failed: %d", err);
		return err;
	}

	shell_print(sh, "Saved device_id=%lu", parsed);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(param_commands,
	SHELL_CMD_ARG(get, NULL, "Read device identifier", cmd_param_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, "Save decimal device identifier: param set <id>",
		      cmd_param_set, 2, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(param, &param_commands, "Persistent device parameters", NULL);

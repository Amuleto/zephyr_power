#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>

#include "params.h"

/** 存储中没有设备编号时使用的默认值。 */
#define PARAMS_DEFAULT_DEVICE_ID 1U
/** 设备编号在 Settings 中的完整键名。 */
#define PARAMS_DEVICE_ID_KEY "app/device_id"

static K_MUTEX_DEFINE(params_lock);
static uint32_t current_device_id = PARAMS_DEFAULT_DEVICE_ID;
static bool params_ready;
static int load_error;

/**
 ******************************************************************************
 * @brief      从 Settings 加载设备编号。
 * @param[in]  name:app 子树下的参数名。
 * @param[in]  len:存储值的字节数。
 * @param[in]  read_cb:读取存储值的回调。
 * @param[in]  cb_arg:传给读取回调的上下文。
 * @retval     0:加载成功。
 * @retval     -ENOENT:参数名未知。
 * @retval     -EINVAL:存储长度不符。
 * @retval     -EIO:读到的字节数不符。
 * @retval     其他负值:读取回调失败。
 ******************************************************************************
 */
static int params_load(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    uint8_t encoded[sizeof(uint32_t)];
    ssize_t count;

    if (strcmp(name, "device_id") != 0)
    {
        return -ENOENT;
    }
    if (len != sizeof(encoded))
    {
        load_error = -EINVAL;
        return load_error;
    }

    count = read_cb(cb_arg, encoded, sizeof(encoded));
    if (count < 0)
    {
        load_error = (int)count;
        return load_error;
    }
    if (count != sizeof(encoded))
    {
        load_error = -EIO;
        return load_error;
    }

    /* 固定 Flash 字节序，使存储格式独立于 CPU 字节序。 */
    k_mutex_lock(&params_lock, K_FOREVER);
    current_device_id = sys_get_le32(encoded);
    k_mutex_unlock(&params_lock);
    return 0;
}

/**
 ******************************************************************************
 * @brief      将加载阶段记录的错误返回给 Settings。
 * @retval     0:没有记录加载错误。
 * @retval     负值:加载阶段记录的错误。
 ******************************************************************************
 */
static int params_commit(void)
{
    /* 在提交回调中返回 Settings 加载阶段未传递的 h_set 错误。 */
    return load_error;
}

SETTINGS_STATIC_HANDLER_DEFINE(app_params, "app", NULL, params_load, params_commit, NULL);

/**
 ******************************************************************************
 * @brief      初始化 Settings/NVS 并加载设备参数。
 * @retval     0:初始化成功。
 * @retval     负值:存储初始化或参数加载失败。
 *
 * @details    启动时调用一次。缺失的参数采用默认值。
 ******************************************************************************
 */
int app_params_init(void)
{
    int err;

    err = settings_subsys_init();
    if (err)
    {
        return err;
    }

    err = settings_load_subtree("app");
    if (err)
    {
        return err;
    }

    k_mutex_lock(&params_lock, K_FOREVER);
    params_ready = true;
    k_mutex_unlock(&params_lock);
    return 0;
}

/**
 ******************************************************************************
 * @brief      获取 RAM 中的设备编号。
 * @param[out] device_id:编号输出地址，须有效。
 * @retval     0:读取成功。
 * @retval     -EINVAL:输出地址为空。
 * @retval     -EAGAIN:参数模块尚未就绪。
 ******************************************************************************
 */
int app_params_get_device_id(uint32_t *device_id)
{
    if (device_id == NULL)
    {
        return -EINVAL;
    }

    k_mutex_lock(&params_lock, K_FOREVER);
    if (!params_ready)
    {
        k_mutex_unlock(&params_lock);
        return -EAGAIN;
    }

    *device_id = current_device_id;
    k_mutex_unlock(&params_lock);
    return 0;
}

/**
 ******************************************************************************
 * @brief      持久化设备编号，成功后更新 RAM 副本。
 * @param[in]  device_id:设备编号，默认值为 1。
 * @retval     0:保存成功。
 * @retval     -EAGAIN:参数模块尚未就绪。
 * @retval     其他负值:存储错误。
 *
 * @details    写入 Flash，须在线程上下文调用。
 ******************************************************************************
 */
int app_params_set_device_id(uint32_t device_id)
{
    uint8_t encoded[sizeof(uint32_t)];
    int err;

    k_mutex_lock(&params_lock, K_FOREVER);
    if (!params_ready)
    {
        k_mutex_unlock(&params_lock);
        return -EAGAIN;
    }

    if (device_id == current_device_id)
    {
        k_mutex_unlock(&params_lock);
        return 0;
    }

    sys_put_le32(device_id, encoded);
    err = settings_save_one(PARAMS_DEVICE_ID_KEY, encoded, sizeof(encoded));
    if (!err)
    {
        current_device_id = device_id;
    }
    k_mutex_unlock(&params_lock);
    return err;
}

/**
 ******************************************************************************
 * @brief      打印当前设备编号。
 * @param[in]  sh:输出结果的 Shell 实例。
 * @param[in]  argc:参数数量，由 Shell 校验。
 * @param[in]  argv:命令参数，未使用。
 * @retval     0:查询成功。
 * @retval     负值:参数读取失败。
 ******************************************************************************
 */
static int cmd_param_get(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t device_id;
    int err;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    err = app_params_get_device_id(&device_id);
    if (err)
    {
        shell_error(sh, "Parameter read failed: %d", err);
        return err;
    }

    shell_print(sh, "device_id=%lu", (unsigned long)device_id);
    return 0;
}

/**
 ******************************************************************************
 * @brief      解析并保存十进制设备编号。
 * @param[in]  sh:输出结果的 Shell 实例。
 * @param[in]  argc:参数数量，由 Shell 校验。
 * @param[in]  argv:第 1 项为十进制编号。
 * @retval     0:保存成功。
 * @retval     -EINVAL:编号格式无效。
 * @retval     -ERANGE:编号超出范围。
 * @retval     其他负值:参数保存失败。
 ******************************************************************************
 */
static int cmd_param_set(const struct shell *sh, size_t argc, char **argv)
{
    unsigned long parsed;
    char *end;
    int err;

    ARG_UNUSED(argc);

    /* 仅接受十进制数字，因为 strtoul 也接受负号。 */
    if (argv[1][0] == '\0' || strspn(argv[1], "0123456789") != strlen(argv[1]))
    {
        return -EINVAL;
    }

    errno = 0;
    parsed = strtoul(argv[1], &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed > UINT32_MAX)
    {
        return -ERANGE;
    }

    err = app_params_set_device_id((uint32_t)parsed);
    if (err)
    {
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

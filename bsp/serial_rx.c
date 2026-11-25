#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>

#if defined(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif

#include "serial_rx.h"

/** 每路串口的中断接收状态；线程只在短临界区内读取缓冲区和统计。 */
typedef struct
{
    const struct device *dev;
    struct ring_buf ring;
    struct k_spinlock lock;
    struct k_sem available;
    app_serial_rx_stats_t stats;
    uint8_t storage[CONFIG_APP_UART_RX_BUFFER_SIZE];
} serial_rx_context_t;

static serial_rx_context_t ports[APP_SERIAL_PORT_COUNT] =
{
    [APP_SERIAL_FACTORY] = {.dev = DEVICE_DT_GET(DT_ALIAS(factory_uart))},
    [APP_SERIAL_RS485] = {.dev = DEVICE_DT_GET(DT_ALIAS(rs485_uart))},
};
static K_MUTEX_DEFINE(init_lock);
static atomic_t initialized;

BUILD_ASSERT(!DT_SAME_NODE(DT_ALIAS(factory_uart), DT_ALIAS(rs485_uart)));
#if DT_HAS_CHOSEN(zephyr_shell_uart)
BUILD_ASSERT(!DT_SAME_NODE(DT_ALIAS(factory_uart), DT_CHOSEN(zephyr_shell_uart)));
BUILD_ASSERT(!DT_SAME_NODE(DT_ALIAS(rs485_uart), DT_CHOSEN(zephyr_shell_uart)));
#endif /* DT_HAS_CHOSEN(zephyr_shell_uart) */

static bool port_valid(app_serial_port_e port)
{
    return (unsigned int)port < APP_SERIAL_PORT_COUNT;
}

/**
 ******************************************************************************
 * @brief      排空 UART FIFO，将新字节存入环形缓冲并通知读取线程。
 * @param[in]  dev:产生中断的串口设备。
 * @param[in,out] user_data:本串口的接收上下文，由初始化函数注册。
 * @retval     None
 * @details    在中断上下文执行，不等待缓冲空间；缓冲区满时丢弃新字节并计数。
 ******************************************************************************
 */
static void receive_irq(const struct device *dev, void *user_data)
{
    serial_rx_context_t *ctx = user_data;
    uint8_t bytes[16];
    k_spinlock_key_t key;
    uint32_t stored;
    int errors;
    int count;

    if (!uart_irq_update(dev))
    {
        return;
    }

    /* 仅有错误中断时也要读取并记录 UART 错误。 */
    errors = uart_err_check(dev);
    if (errors > 0)
    {
        key = k_spin_lock(&ctx->lock);
        ctx->stats.error_events++;
        ctx->stats.error_mask |= (uint32_t)errors;
        k_spin_unlock(&ctx->lock, key);
    }

    while (uart_irq_rx_ready(dev))
    {
        count = uart_fifo_read(dev, bytes, sizeof(bytes));
        if (count <= 0)
        {
            break;
        }

        /* 软件缓冲区满时仍排空硬件接收寄存器，避免中断持续挂起。 */
        key = k_spin_lock(&ctx->lock);
        stored = ring_buf_put(&ctx->ring, bytes, count);
        ctx->stats.received += (uint32_t)count;
        ctx->stats.dropped += (uint32_t)count - stored;
        k_spin_unlock(&ctx->lock, key);
        if (stored > 0)
        {
            k_sem_give(&ctx->available);
        }
    }
}

/**
 ******************************************************************************
 * @brief      注册回调并启用两个业务串口的接收中断。
 * @retval     0:初始化成功。
 * @retval     -ENODEV:串口未就绪。
 * @retval     -EALREADY:模块已初始化。
 * @retval     其他负值:回调注册失败。
 *
 * @details    启动时在线程上下文调用一次，随后由本模块独占两路串口接收。
 ******************************************************************************
 */
int app_serial_rx_init(void)
{
    int attached = 0;
    int err = 0;

    k_mutex_lock(&init_lock, K_FOREVER);
    if (atomic_get(&initialized))
    {
        err = -EALREADY;
        goto out;
    }

    for (size_t i = 0; i < ARRAY_SIZE(ports); i++)
    {
        if (!device_is_ready(ports[i].dev))
        {
            err = -ENODEV;
            goto out;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(ports); i++)
    {
        serial_rx_context_t *ctx = &ports[i];

        uart_irq_rx_disable(ctx->dev);
        uart_irq_err_disable(ctx->dev);
        ring_buf_init(&ctx->ring, sizeof(ctx->storage), ctx->storage);
        k_sem_init(&ctx->available, 0, 1);
        err = uart_irq_callback_user_data_set(ctx->dev, receive_irq, ctx);
        if (err)
        {
            goto rollback;
        }
        attached++;
    }

    /* 先公布初始化状态，再允许中断唤醒读取线程。 */
    atomic_set(&initialized, 1);
    for (size_t i = 0; i < ARRAY_SIZE(ports); i++)
    {
        uart_irq_err_enable(ports[i].dev);
        uart_irq_rx_enable(ports[i].dev);
    }
    goto out;

rollback:
    for (int i = 0; i < attached; i++)
    {
        uart_irq_callback_user_data_set(ports[i].dev, NULL, NULL);
    }

out:
    k_mutex_unlock(&init_lock);
    return err;
}

/**
 ******************************************************************************
 * @brief      读取业务串口当前可用的字节。
 * @param[in]  port:业务串口编号。
 * @param[out] buffer:容量至少为 length 字节的输出缓冲区。
 * @param[in]  length:最大读取字节数，为零时 buffer 可为空。
 * @param[in]  timeout:等待首字节的超时，支持 K_NO_WAIT 和 K_FOREVER。
 * @retval     正值:实际读取的字节数。
 * @retval     0:等待超时或请求长度为零。
 * @retval     -EINVAL:端口、缓冲区或长度无效。
 * @retval     -EAGAIN:模块尚未初始化。
 * @retval     -EWOULDBLOCK:在中断上下文调用。
 *
 * @details    每个端口由一个线程读取。返回字节片段，组帧由调用方负责。
 *             缓冲区满时丢弃新字节，调用方可通过统计检测数据中断。
 *             调用方保有 buffer 所有权。
 ******************************************************************************
 */
ssize_t app_serial_rx_read(app_serial_port_e port, uint8_t *buffer, size_t length,
                           k_timeout_t timeout)
{
    serial_rx_context_t *ctx;
    k_spinlock_key_t key;
    k_timepoint_t deadline;
    uint32_t count;

    if (!port_valid(port) || (buffer == NULL && length != 0) || length > UINT32_MAX)
    {
        return -EINVAL;
    }
    if (k_is_in_isr())
    {
        return -EWOULDBLOCK;
    }
    if (!atomic_get(&initialized))
    {
        return -EAGAIN;
    }
    if (length == 0)
    {
        return 0;
    }

    ctx = &ports[port];
    deadline = sys_timepoint_calc(timeout);
    for (;;)
    {
        key = k_spin_lock(&ctx->lock);
        count = ring_buf_get(&ctx->ring, buffer, length);
        k_spin_unlock(&ctx->lock, key);
        if (count > 0)
        {
            return count;
        }

        /* 信号量通知可合并，旧通知不得重新开始计时。 */
        if (k_sem_take(&ctx->available, sys_timepoint_timeout(deadline)) != 0)
        {
            return 0;
        }
    }
}

/**
 ******************************************************************************
 * @brief      获取一致的串口接收统计快照。
 * @param[in]  port:业务串口编号。
 * @param[out] stats:有效的统计输出地址。
 * @retval     0:复制成功。
 * @retval     -EINVAL:端口或输出地址无效。
 * @retval     -EAGAIN:模块尚未初始化。
 ******************************************************************************
 */
int app_serial_rx_stats_get(app_serial_port_e port, app_serial_rx_stats_t *stats)
{
    serial_rx_context_t *ctx;
    k_spinlock_key_t key;

    if (!port_valid(port) || stats == NULL)
    {
        return -EINVAL;
    }
    if (!atomic_get(&initialized))
    {
        return -EAGAIN;
    }

    ctx = &ports[port];
    key = k_spin_lock(&ctx->lock);
    *stats = ctx->stats;
    stats->pending = ring_buf_size_get(&ctx->ring);
    stats->capacity = sizeof(ctx->storage);
    k_spin_unlock(&ctx->lock, key);
    return 0;
}

#if defined(CONFIG_SHELL)
static const char *const port_names[] = {"factory", "rs485"};

/**
 ******************************************************************************
 * @brief      打印两路业务串口的接收缓存及丢包统计。
 * @param[in]  sh:输出结果的 Shell 实例。
 * @param[in]  argc:参数数量，由 Shell 校验。
 * @param[in]  argv:命令参数，未使用。
 * @retval     0:查询成功。
 * @retval     负值:串口状态不可用。
 ******************************************************************************
 */
static int cmd_serial_stats(const struct shell *sh, size_t argc, char **argv)
{
    app_serial_rx_stats_t stats;
    int err;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    for (int i = 0; i < APP_SERIAL_PORT_COUNT; i++)
    {
        err = app_serial_rx_stats_get(i, &stats);
        if (err)
        {
            return err;
        }
        shell_print(sh, "%s: pending=%u/%u received=%u dropped=%u errors=%u mask=0x%x",
                    port_names[i], stats.pending, stats.capacity, stats.received, stats.dropped,
                    stats.error_events, stats.error_mask);
    }
    return 0;
}

/**
 ******************************************************************************
 * @brief      非阻塞读取并显示产线串口已缓存的字节。
 * @param[in]  sh:输出结果的 Shell 实例。
 * @param[in]  argc:参数数量，由 Shell 校验。
 * @param[in]  argv:第 1 项为端口名。
 * @retval     0:读取成功或暂无数据。
 * @retval     -EBUSY:RS485 已由主板通信线程独占。
 * @retval     -EINVAL:端口名称无效。
 * @retval     其他负值:串口读取失败。
 * @details    最多消费 64 字节；正式产线任务使用该端口后需移除此调试入口。
 ******************************************************************************
 */
static int cmd_serial_read(const struct shell *sh, size_t argc, char **argv)
{
    uint8_t bytes[64];
    ssize_t count;

    ARG_UNUSED(argc);

    for (int i = 0; i < APP_SERIAL_PORT_COUNT; i++)
    {
        if (strcmp(argv[1], port_names[i]) != 0)
        {
            continue;
        }
        if (i == APP_SERIAL_RS485)
        {
            shell_error(sh, "RS485 RX owned by host link; use serial stats");
            return -EBUSY;
        }
        count = app_serial_rx_read(i, bytes, sizeof(bytes), K_NO_WAIT);
        if (count < 0)
        {
            return (int)count;
        }
        if (count == 0)
        {
            shell_print(sh, "No buffered data");
        }
        else
        {
            shell_hexdump(sh, bytes, count);
        }
        return 0;
    }

    shell_error(sh, "Use: serial read <factory|rs485>");
    return -EINVAL;
}

SHELL_STATIC_SUBCMD_SET_CREATE(serial_commands,
    SHELL_CMD_ARG(stats, NULL, "Show RX occupancy, drops and hardware errors",
                  cmd_serial_stats, 1, 0),
    SHELL_CMD_ARG(read, NULL, "Consume up to 64 RX bytes: serial read <factory|rs485>",
                  cmd_serial_read, 2, 0),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(serial, &serial_commands, "Business UART RX buffers", NULL);
#endif /* CONFIG_SHELL */

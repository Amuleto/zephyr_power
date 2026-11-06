#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>

#if defined(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif

#include "serial_rx.h"

struct serial_rx_context {
	const struct device *dev;
	struct ring_buf ring;
	struct k_spinlock lock;
	struct k_sem available;
	struct app_serial_rx_stats stats;
	uint8_t storage[CONFIG_APP_UART_RX_BUFFER_SIZE];
};

static struct serial_rx_context ports[APP_SERIAL_PORT_COUNT] = {
	[APP_SERIAL_FACTORY] = {.dev = DEVICE_DT_GET(DT_ALIAS(factory_uart))},
	[APP_SERIAL_RS485] = {.dev = DEVICE_DT_GET(DT_ALIAS(rs485_uart))},
};
static K_MUTEX_DEFINE(init_lock);
static atomic_t initialized;

BUILD_ASSERT(!DT_SAME_NODE(DT_ALIAS(factory_uart), DT_ALIAS(rs485_uart)));
#if DT_HAS_CHOSEN(zephyr_shell_uart)
BUILD_ASSERT(!DT_SAME_NODE(DT_ALIAS(factory_uart), DT_CHOSEN(zephyr_shell_uart)));
BUILD_ASSERT(!DT_SAME_NODE(DT_ALIAS(rs485_uart), DT_CHOSEN(zephyr_shell_uart)));
#endif

static bool port_valid(enum app_serial_port port)
{
	return (unsigned int)port < APP_SERIAL_PORT_COUNT;
}

static void receive_irq(const struct device *dev, void *user_data)
{
	struct serial_rx_context *ctx = user_data;
	uint8_t bytes[16];
	k_spinlock_key_t key;
	uint32_t stored;
	int errors;
	int count;

	if (!uart_irq_update(dev)) {
		return;
	}

	/* Check errors even when a callback was caused by an error-only interrupt. */
	errors = uart_err_check(dev);
	if (errors > 0) {
		key = k_spin_lock(&ctx->lock);
		ctx->stats.error_events++;
		ctx->stats.error_mask |= (uint32_t)errors;
		k_spin_unlock(&ctx->lock, key);
	}

	while (uart_irq_rx_ready(dev)) {
		count = uart_fifo_read(dev, bytes, sizeof(bytes));
		if (count <= 0) {
			break;
		}

		/* Always drain hardware, including when the software ring is full. */
		key = k_spin_lock(&ctx->lock);
		stored = ring_buf_put(&ctx->ring, bytes, count);
		ctx->stats.received += (uint32_t)count;
		ctx->stats.dropped += (uint32_t)count - stored;
		k_spin_unlock(&ctx->lock, key);
		if (stored > 0) {
			k_sem_give(&ctx->available);
		}
	}
}

int app_serial_rx_init(void)
{
	int attached = 0;
	int err = 0;

	k_mutex_lock(&init_lock, K_FOREVER);
	if (atomic_get(&initialized)) {
		err = -EALREADY;
		goto out;
	}

	for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
		if (!device_is_ready(ports[i].dev)) {
			err = -ENODEV;
			goto out;
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
		struct serial_rx_context *ctx = &ports[i];

		uart_irq_rx_disable(ctx->dev);
		uart_irq_err_disable(ctx->dev);
		ring_buf_init(&ctx->ring, sizeof(ctx->storage), ctx->storage);
		k_sem_init(&ctx->available, 0, 1);
		err = uart_irq_callback_user_data_set(ctx->dev, receive_irq, ctx);
		if (err) {
			goto rollback;
		}
		attached++;
	}

	/* Publish initialized state before an enabled interrupt can wake a reader. */
	atomic_set(&initialized, 1);
	for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
		uart_irq_err_enable(ports[i].dev);
		uart_irq_rx_enable(ports[i].dev);
	}
	goto out;

rollback:
	for (int i = 0; i < attached; i++) {
		(void)uart_irq_callback_user_data_set(ports[i].dev, NULL, NULL);
	}

out:
	k_mutex_unlock(&init_lock);
	return err;
}

ssize_t app_serial_rx_read(enum app_serial_port port, uint8_t *buffer,
			   size_t length, k_timeout_t timeout)
{
	struct serial_rx_context *ctx;
	k_spinlock_key_t key;
	k_timepoint_t deadline;
	uint32_t count;

	if (!port_valid(port) || (buffer == NULL && length != 0) || length > UINT32_MAX) {
		return -EINVAL;
	}
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}
	if (!atomic_get(&initialized)) {
		return -EAGAIN;
	}
	if (length == 0) {
		return 0;
	}

	ctx = &ports[port];
	deadline = sys_timepoint_calc(timeout);
	for (;;) {
		key = k_spin_lock(&ctx->lock);
		count = ring_buf_get(&ctx->ring, buffer, length);
		k_spin_unlock(&ctx->lock, key);
		if (count > 0) {
			return count;
		}

		/* Notifications coalesce; a stale token must not restart the timeout. */
		if (k_sem_take(&ctx->available, sys_timepoint_timeout(deadline)) != 0) {
			return 0;
		}
	}
}

int app_serial_rx_stats_get(enum app_serial_port port, struct app_serial_rx_stats *stats)
{
	struct serial_rx_context *ctx;
	k_spinlock_key_t key;

	if (!port_valid(port) || stats == NULL) {
		return -EINVAL;
	}
	if (!atomic_get(&initialized)) {
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

static int cmd_serial_stats(const struct shell *sh, size_t argc, char **argv)
{
	struct app_serial_rx_stats stats;
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (int i = 0; i < APP_SERIAL_PORT_COUNT; i++) {
		err = app_serial_rx_stats_get(i, &stats);
		if (err) {
			return err;
		}
		shell_print(sh, "%s: pending=%u/%u received=%u dropped=%u errors=%u mask=0x%x",
			    port_names[i], stats.pending, stats.capacity, stats.received,
			    stats.dropped, stats.error_events, stats.error_mask);
	}
	return 0;
}

static int cmd_serial_read(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t bytes[64];
	ssize_t count;

	ARG_UNUSED(argc);

	for (int i = 0; i < APP_SERIAL_PORT_COUNT; i++) {
		if (strcmp(argv[1], port_names[i]) != 0) {
			continue;
		}
		count = app_serial_rx_read(i, bytes, sizeof(bytes), K_NO_WAIT);
		if (count < 0) {
			return (int)count;
		}
		if (count == 0) {
			shell_print(sh, "No buffered data");
		} else {
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
#endif

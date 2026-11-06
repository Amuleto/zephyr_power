#include <errno.h>
#include <string.h>

#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>

#include "serial_rx.h"

static const struct device *const factory = DEVICE_DT_GET(DT_ALIAS(factory_uart));
static const struct device *const rs485 = DEVICE_DT_GET(DT_ALIAS(rs485_uart));

static struct app_serial_rx_stats snapshot(enum app_serial_port port)
{
	struct app_serial_rx_stats stats;

	zassert_ok(app_serial_rx_stats_get(port, &stats));
	return stats;
}

static void inject(enum app_serial_port port, const uint8_t *data, size_t length)
{
	const struct device *dev = port == APP_SERIAL_FACTORY ? factory : rs485;
	uint32_t before = snapshot(port).received;

	zassert_equal(uart_emul_put_rx_data(dev, data, length), length);
	for (int i = 0; i < 100; i++) {
		if ((uint32_t)(snapshot(port).received - before) == length) {
			return;
		}
		k_msleep(1);
	}
	zassert_unreachable("Receive callback did not drain the emulator");
}

static void *setup(void)
{
	uint8_t byte;

	zassert_equal(app_serial_rx_read(APP_SERIAL_FACTORY, &byte, 1, K_NO_WAIT), -EAGAIN);
	zassert_ok(app_serial_rx_init());
	zassert_equal(app_serial_rx_init(), -EALREADY);
	return NULL;
}

static void before(void *fixture)
{
	uint8_t discard[64];

	ARG_UNUSED(fixture);
	for (int i = 0; i < APP_SERIAL_PORT_COUNT; i++) {
		while (app_serial_rx_read(i, discard, sizeof(discard), K_NO_WAIT) > 0) {
		}
	}
}

ZTEST(serial_rx, test_independent_binary_streams)
{
	const uint8_t first[] = {0x00, 0x55, 0xff};
	const uint8_t second[] = {0x10, 0x20, 0x30, 0x40};
	uint8_t output[8];

	inject(APP_SERIAL_FACTORY, first, sizeof(first));
	inject(APP_SERIAL_RS485, second, sizeof(second));
	zassert_equal(app_serial_rx_read(APP_SERIAL_FACTORY, output, sizeof(output), K_NO_WAIT),
		      sizeof(first));
	zassert_mem_equal(output, first, sizeof(first));
	zassert_equal(app_serial_rx_read(APP_SERIAL_RS485, output, sizeof(output), K_NO_WAIT),
		      sizeof(second));
	zassert_mem_equal(output, second, sizeof(second));
}

ZTEST(serial_rx, test_overflow_wraparound_and_recovery)
{
	uint8_t input[73];
	uint8_t next[20];
	uint8_t output[64];
	struct app_serial_rx_stats initial = snapshot(APP_SERIAL_FACTORY);
	struct app_serial_rx_stats stats;

	for (size_t i = 0; i < sizeof(input); i++) {
		input[i] = i;
	}
	memset(next, 0xa5, sizeof(next));
	inject(APP_SERIAL_FACTORY, input, sizeof(input));
	stats = snapshot(APP_SERIAL_FACTORY);
	zassert_equal(stats.pending, 64);
	zassert_equal(stats.dropped - initial.dropped, 9);
	zassert_equal(app_serial_rx_read(APP_SERIAL_FACTORY, output, 30, K_NO_WAIT), 30);
	zassert_mem_equal(output, input, 30);
	inject(APP_SERIAL_FACTORY, next, sizeof(next));
	zassert_equal(app_serial_rx_read(APP_SERIAL_FACTORY, output, sizeof(output), K_NO_WAIT), 54);
	zassert_mem_equal(output, input + 30, 34);
	zassert_mem_equal(output + 34, next, sizeof(next));
	zassert_equal(snapshot(APP_SERIAL_FACTORY).pending, 0);
}

ZTEST(serial_rx, test_timeout_and_invalid_arguments)
{
	uint8_t byte;
	int64_t start = k_uptime_get();

	zassert_equal(app_serial_rx_read(APP_SERIAL_FACTORY, &byte, 1, K_MSEC(10)), 0);
	zassert_true(k_uptime_get() - start >= 10);
	zassert_equal(app_serial_rx_read(APP_SERIAL_FACTORY, NULL, 0, K_NO_WAIT), 0);
	zassert_equal(app_serial_rx_read(APP_SERIAL_FACTORY, NULL, 1, K_NO_WAIT), -EINVAL);
	zassert_equal(app_serial_rx_read(APP_SERIAL_PORT_COUNT, &byte, 1, K_NO_WAIT), -EINVAL);
	zassert_equal(app_serial_rx_stats_get(APP_SERIAL_FACTORY, NULL), -EINVAL);
}

static void delayed_rx(struct k_work *work)
{
	const uint8_t byte = 0x42;

	ARG_UNUSED(work);
	zassert_equal(uart_emul_put_rx_data(rs485, &byte, 1), 1);
}

K_WORK_DELAYABLE_DEFINE(delayed_rx_work, delayed_rx);

ZTEST(serial_rx, test_blocking_reader_wakes_on_data)
{
	uint8_t byte;

	k_work_schedule(&delayed_rx_work, K_MSEC(5));
	zassert_equal(app_serial_rx_read(APP_SERIAL_RS485, &byte, 1, K_MSEC(100)), 1);
	zassert_equal(byte, 0x42);
}

ZTEST(serial_rx, test_uart_error_accounting)
{
	const uint8_t byte = 0x73;
	struct app_serial_rx_stats initial = snapshot(APP_SERIAL_RS485);
	struct app_serial_rx_stats stats;

	uart_emul_set_errors(rs485, UART_ERROR_OVERRUN | UART_ERROR_FRAMING);
	inject(APP_SERIAL_RS485, &byte, 1);
	stats = snapshot(APP_SERIAL_RS485);
	zassert_equal(stats.error_events - initial.error_events, 1);
	zassert_equal(stats.error_mask & (UART_ERROR_OVERRUN | UART_ERROR_FRAMING),
		      UART_ERROR_OVERRUN | UART_ERROR_FRAMING);
}

ZTEST_SUITE(serial_rx, NULL, setup, before, NULL, NULL);

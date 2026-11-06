#ifndef BRINGUP_SERIAL_RX_H_
#define BRINGUP_SERIAL_RX_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <zephyr/kernel.h>

/** Business UART identifiers; UART4 belongs exclusively to the Shell. */
enum app_serial_port {
	APP_SERIAL_FACTORY,
	APP_SERIAL_RS485,
	APP_SERIAL_PORT_COUNT,
};

/** Snapshot of receive state; cumulative counters wrap modulo 2^32. */
struct app_serial_rx_stats {
	uint32_t pending; /**< Bytes currently buffered. */
	uint32_t capacity; /**< Buffer capacity in bytes. */
	uint32_t received; /**< Bytes read from the hardware, including dropped bytes. */
	uint32_t dropped; /**< New bytes discarded because the ring was full. */
	uint32_t error_events; /**< Positive UART error reports; not a lost-byte count. */
	uint32_t error_mask; /**< Accumulated UART_ERROR_* flags. */
};

/**
 * @brief Register receive callbacks and enable both business UARTs.
 *
 * Call once at startup from thread context. No TX interrupts or RS485 direction
 * changes are performed. The callbacks own RX on these two devices thereafter.
 *
 * @return 0 on success, or a negative driver error code.
 * @retval -ENODEV A device is not ready.
 * @retval -EALREADY The module has already been initialized.
 */
int app_serial_rx_init(void);

/**
 * @brief Read available bytes, waiting at most timeout for the first byte.
 *
 * Use one consumer thread per port. Returned chunks are not protocol frames.
 * Overflow drops new bytes and preserves queued bytes; monitor the counters to
 * detect stream discontinuities. Do not use uart_poll_in() on these devices.
 *
 * @param port Business UART to read.
 * @param[out] buffer Destination, valid for at least length bytes; may be NULL
 *                    only when length is zero.
 * @param length Maximum byte count to copy.
 * @param timeout Wait for data; K_NO_WAIT polls and K_FOREVER waits indefinitely.
 *
 * @return Positive byte count, zero on timeout or zero length, or negative errno.
 * @retval -EINVAL Invalid port, destination or length above UINT32_MAX.
 * @retval -EAGAIN The module is not initialized.
 * @retval -EWOULDBLOCK Called from interrupt context.
 */
ssize_t app_serial_rx_read(enum app_serial_port port, uint8_t *buffer,
			   size_t length, k_timeout_t timeout);

/**
 * @brief Obtain a consistent receive statistics snapshot.
 *
 * @param port Business UART to inspect.
 * @param[out] stats Destination for the snapshot; must not be NULL.
 *
 * @retval 0 Snapshot copied.
 * @retval -EINVAL Invalid port or NULL destination.
 * @retval -EAGAIN The module is not initialized.
 */
int app_serial_rx_stats_get(enum app_serial_port port, struct app_serial_rx_stats *stats);

#endif /* BRINGUP_SERIAL_RX_H_ */

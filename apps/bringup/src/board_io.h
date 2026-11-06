#ifndef BRINGUP_BOARD_IO_H_
#define BRINGUP_BOARD_IO_H_

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize board outputs inactive and configure ADC channels.
 *
 * No relay is energized and RS485 direction defaults to receive. Call once
 * during startup, from thread context, before using the board interfaces.
 *
 * @return 0 on success, or a negative GPIO/ADC/device error code.
 */
int app_board_io_init(void);

/**
 * @brief Control one precharge relay.
 *
 * This low-level function does not implement precharge timing or interlocks.
 * Callers must provide the application sequencing before energizing a relay.
 *
 * @param index Zero-based relay index, 0 through 3.
 * @param enabled True to energize, false to de-energize.
 *
 * @return 0 on success, or a negative GPIO error code.
 * @retval -EINVAL Invalid index.
 * @retval -EAGAIN Board initialization has not completed successfully.
 */
int app_board_relay_set(size_t index, bool enabled);

/**
 * @brief Select the RS485 transceiver direction on PB8.
 *
 * The transceiver direction signal must be physically connected to PB8.
 * Before switching to receive, the caller must wait for the final UART stop
 * bit to leave the transmitter; this function does not serialize UART traffic.
 *
 * @param transmit True for transmit, false for receive.
 *
 * @return 0 on success, or a negative GPIO error code.
 * @retval -EAGAIN Board initialization has not completed successfully.
 */
int app_board_rs485_direction_set(bool transmit);

#endif /* BRINGUP_BOARD_IO_H_ */

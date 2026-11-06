#ifndef BRINGUP_PARAMS_H_
#define BRINGUP_PARAMS_H_

#include <stdint.h>

/**
 * @brief Initialize Settings/NVS and load device parameters.
 *
 * Call once during startup before using the other parameter functions.
 * Missing values retain defaults. Initialization never erases the partition
 * as an error-recovery action.
 *
 * @return 0 on success, or a negative error code on storage failure.
 */
int app_params_init(void);

/**
 * @brief Read the device identifier currently held in RAM.
 *
 * @param[out] device_id Destination for the identifier. Must not be NULL.
 *
 * @retval 0 Identifier read successfully.
 * @retval -EINVAL NULL destination.
 * @retval -EAGAIN Parameter initialization has not completed successfully.
 */
int app_params_get_device_id(uint32_t *device_id);

/**
 * @brief Persist a device identifier and update its RAM copy on success.
 *
 * This operation writes Flash and must be called from thread context.
 *
 * @param device_id Identifier in the full uint32_t range; default is 1.
 *
 * @return 0 on success, or a negative storage error code.
 * @retval -EAGAIN Parameter initialization has not completed successfully.
 */
int app_params_set_device_id(uint32_t device_id);

#endif /* BRINGUP_PARAMS_H_ */

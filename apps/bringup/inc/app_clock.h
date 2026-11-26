#ifndef BRINGUP_APP_CLOCK_H_
#define BRINGUP_APP_CLOCK_H_

#include <stdint.h>

/**
 ******************************************************************************
 * @brief      在主板时间与本地 RTC 相差至少五秒时同步。
 * @param[in]  unix_seconds:主板 UTC Unix 秒数，支持 2000～2099 年。
 * @retval     1:已设置 RTC。
 * @retval     0:时差小于五秒，无需设置。
 * @retval     负值:时间无效或 RTC 操作失败。
 ******************************************************************************
 */
int app_clock_sync(uint32_t unix_seconds);

#endif /* BRINGUP_APP_CLOCK_H_ */

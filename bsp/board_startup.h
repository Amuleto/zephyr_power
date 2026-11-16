#ifndef BRINGUP_BOARD_STARTUP_H_
#define BRINGUP_BOARD_STARTUP_H_

/**
 ******************************************************************************
 * @brief      初始化板级外设并准备硬件看门狗。
 * @retval     0:启动所需外设已就绪。
 * @retval     负值:必需外设初始化失败。
 *
 * @details    RTC 或 CAN 启动失败只记录日志。此阶段尚未启动看门狗计时。
 ******************************************************************************
 */
int board_init(void);

#endif /* BRINGUP_BOARD_STARTUP_H_ */

#ifndef BRINGUP_PRECHARGE_H_
/** 防止本头文件在同一编译单元内重复包含。 */
#define BRINGUP_PRECHARGE_H_

/**
 ******************************************************************************
 * @brief      启动预充线程。
 * @retval     None
 * @details    在板级初始化和参数加载完成后调用一次。
 ******************************************************************************
 */
void precharge_start(void);

#endif /* BRINGUP_PRECHARGE_H_ */

#ifndef BRINGUP_HOST_LINK_H_
/** 防止本头文件在同一编译单元内重复包含。 */
#define BRINGUP_HOST_LINK_H_

/**
 ******************************************************************************
 * @brief      启动 RS485 接收与应答线程。
 * @retval     None
 * @details    在串口和应用参数就绪后调用一次。
 ******************************************************************************
 */
void host_link_start(void);

#endif /* BRINGUP_HOST_LINK_H_ */

#ifndef BRINGUP_RS485_TX_H_
#define BRINGUP_RS485_TX_H_

#include <stddef.h>
#include <stdint.h>

/**
 ******************************************************************************
 * @brief      发送 RS485 数据，结束后切回接收。
 * @param[in]  data:待发送数据，由调用方持有。
 * @param[in]  length:数据字节数。
 * @retval     0:发送成功。
 * @retval     -EINVAL:参数无效。
 * @retval     -ETIMEDOUT:等待最后一位发送超时。
 * @retval     其他负值:方向 GPIO 控制失败或串口驱动不支持发送完成查询。
 * @details    由 RS485 通信线程独占调用。
 ******************************************************************************
 */
int board_rs485_send(const uint8_t *data, size_t length);

#endif /* BRINGUP_RS485_TX_H_ */

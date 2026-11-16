#ifndef BRINGUP_SERIAL_RX_H_
#define BRINGUP_SERIAL_RX_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <zephyr/kernel.h>

/** 业务串口编号。UART4 由 Shell 独占。 */
enum app_serial_port
{
    APP_SERIAL_FACTORY, /**< 产线串口 USART0。 */
    APP_SERIAL_RS485, /**< RS485 串口 USART2。 */
    APP_SERIAL_PORT_COUNT, /**< 业务串口数量。 */
};

/** 接收状态快照。累计计数按 32 位无符号整数回绕。 */
struct app_serial_rx_stats {
    uint32_t pending; /**< 当前缓存字节数。 */
    uint32_t capacity; /**< 缓冲区容量，单位为字节。 */
    uint32_t received; /**< 从硬件读取的字节数，包含后续丢弃的字节。 */
    uint32_t dropped; /**< 因缓冲区满而丢弃的新字节数。 */
    uint32_t error_events; /**< UART 报告错误的次数。 */
    uint32_t error_mask; /**< 累计 UART_ERROR_* 标志。 */
};

/**
 ******************************************************************************
 * @brief      注册回调并启用两个业务串口的接收中断。
 * @retval     0:初始化成功。
 * @retval     -ENODEV:串口未就绪。
 * @retval     -EALREADY:模块已初始化。
 * @retval     其他负值:回调注册失败。
 *
 * @details    启动时在线程上下文调用一次，随后由本模块独占两路串口接收。
 ******************************************************************************
 */
int app_serial_rx_init(void);

/**
 ******************************************************************************
 * @brief      读取业务串口当前可用的字节。
 * @param[in]  port:业务串口编号。
 * @param[out] buffer:容量至少为 length 字节的输出缓冲区。
 * @param[in]  length:最大读取字节数，为零时 buffer 可为空。
 * @param[in]  timeout:等待首字节的超时，支持 K_NO_WAIT 和 K_FOREVER。
 * @retval     正值:实际读取的字节数。
 * @retval     0:等待超时或请求长度为零。
 * @retval     -EINVAL:端口、缓冲区或长度无效。
 * @retval     -EAGAIN:模块尚未初始化。
 * @retval     -EWOULDBLOCK:在中断上下文调用。
 *
 * @details    每个端口由一个线程读取。返回字节片段，组帧由调用方负责。
 *             缓冲区满时丢弃新字节，调用方可通过统计检测数据中断。
 *             调用方保有 buffer 所有权。
 ******************************************************************************
 */
ssize_t app_serial_rx_read(enum app_serial_port port, uint8_t *buffer,
               size_t length, k_timeout_t timeout);

/**
 ******************************************************************************
 * @brief      获取一致的串口接收统计快照。
 * @param[in]  port:业务串口编号。
 * @param[out] stats:有效的统计输出地址。
 * @retval     0:复制成功。
 * @retval     -EINVAL:端口或输出地址无效。
 * @retval     -EAGAIN:模块尚未初始化。
 ******************************************************************************
 */
int app_serial_rx_stats_get(enum app_serial_port port, struct app_serial_rx_stats *stats);

#endif /* BRINGUP_SERIAL_RX_H_ */

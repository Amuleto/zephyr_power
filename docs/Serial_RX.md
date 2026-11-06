# 业务串口接收缓冲

## 端口与容量

USART0（PA9/PA10，产线）和 USART2（PB10/PB11，RS485）分别使用独立的 512 字节 `ring_buf`。UART4 的接收仍归 Shell 后端管理，没有替换其回调。

容量由应用 Kconfig 的 `CONFIG_APP_UART_RX_BUFFER_SIZE` 控制，范围 64～4096 字节；该配置对两个业务串口分别分配同样大小的空间。默认数据缓冲总计 1024 字节，另有少量控制结构，不创建额外接收线程。

使用 UART 中断接收 API，不使用 DMA。ISR 将数据搬入环形缓冲并通知等待者；中断中不打印、不处理协议。缓冲满时保留已入队的字节，丢弃新字节并累计 `dropped`，继续排空硬件 FIFO，避免软件缓冲满导致中断持续挂起。

115200、8N1 连续接收时，512 字节约对应 44 ms 数据。业务线程需要及时消费；不要等现有 500 ms LED 主循环再读取。硬件本身的 overrun 可能丢失未知数量字节，`error_events` 不是丢失字节数。

## Shell 诊断

```text
serial stats
serial read factory
serial read rs485
```

* `stats`：显示当前缓存量、容量、累计接收字节、丢弃字节、UART 错误次数及标志位。
* `read`：非阻塞取出最多 64 字节，十六进制显示；再次执行可继续取出。
* 手动 `read` 会消费数据。正式协议线程开始使用某端口后，不要同时用 Shell 抢读该端口。
* 没有自动回显、转发或发送，不操作 RS485 DE；接收仍以物理连接 PB8、方向保持接收为前提。

## 业务线程接口

接口及参数、返回值约定见 `apps/bringup/src/serial_rx.h`。

```c
uint8_t data[64];
ssize_t count;

count = app_serial_rx_read(APP_SERIAL_RS485, data, sizeof(data), K_FOREVER);
if (count > 0) {
	/* Pass the received chunk to the protocol parser. */
}
```

可选 `K_NO_WAIT`、`K_MSEC(...)` 或 `K_FOREVER`。有数据时立即返回当前可读字节数，不等待填满用户缓冲；超时返回 0。每个端口只应有一个消费者线程。环形缓冲和统计通过自旋锁保护，中断通知通过信号量合并，陈旧通知不会重新开始超时计时。

`app_serial_rx_stats_get()` 提供一致的状态快照。累计计数按 uint32_t 自然回绕；`received` 包含从硬件读出后因缓冲已满而丢弃的字节，`error_mask` 是驱动报告的 `UART_ERROR_*` 累计位。

一旦丢弃或硬件错误计数增加，协议层应视为可能发生数据不连续，按协议重新寻找帧头并校验。当前只提供字节流，不按换行、空闲间隔或 Modbus 时序组帧，不实现 CRC、请求响应或 RS485 发送控制。

不要再给同一 UART 注册其他 RX 回调，也不要同时调用 `uart_poll_in()` 或异步 RX API，避免争抢接收数据。

## 验证

使用 Zephyr UART emulator 与 QEMU Cortex-M3 运行了 5 项 ztest，全部通过：

1. 两个串口独立，二进制 0x00/0xFF 数据不变。
2. 缓冲溢出丢弃新字节、计数正确，部分读取后回绕，释放空间后恢复接收。
3. 无数据超时、陈旧信号量通知处理和无效参数检查。
4. 阻塞读者在数据到达时唤醒。
5. UART overrun/framing 错误标志与次数统计。

测试位于 `tests/serial_rx/`，测试容量为 64 字节，直接链接应用的 serial_rx.c，没有复制一份实现。GD32 MCUboot 联合构建也已通过，本次应用 Flash 80,596 B、RAM 13,120 B。

尚未烧录、验证实板连续收发或最大吞吐量，模拟测试不代表 GD32 硬件时序已经验证。

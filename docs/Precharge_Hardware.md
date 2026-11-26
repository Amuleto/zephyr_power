# 预充检测板硬件配置

## 依据与实板差异

依据用户提供的《预充检测.pdf》：第 1 页原理图、第 2 页 PCB、第 3 页 BOM。MCU 为 U38 GD32F303RCT6，X1 为 8 MHz。

当前设备树将指示灯配置在 PB7，高电平亮。

实板确认 PB12 为 RS485 收发方向控制脚。

## 引脚表

| 功能 | MCU 引脚 | 当前配置 |
| --- | --- | --- |
| 调试日志和 Shell | UART4 TX PC12、RX PD2 | 115200，8N1，沿用已验证配置 |
| 产线串口 J14 | USART0 TX PA9、RX PA10 | 115200，8N1，仅初始化驱动，无产线协议 |
| RS485 串口 | USART2 TX PB10、RX PB11 | 9600，8E1，主控与预充板通信 |
| RS485 DE 与 /RE | PB12 | 高发送、低接收，默认低 |
| CAN0 | PA11 RX、PA12 TX | 经典 CAN，默认 500 kbit/s，通过 UART4 Shell 调试 |
| 实板指示灯 | PB7 | 高电平亮 |
| JDQ1 | PB6 | 高电平有效，初始化为低 |
| JDQ2 | PB5 | 高电平有效，初始化为低 |
| JDQ3 | PB4 | 高电平有效，初始化为低；释放 JTAG 引脚 |
| JDQ4 | PB3 | 高电平有效，初始化为低；释放 JTAG 引脚 |
| CH1 继电器前／电阻后 | PA0／PA1 | ADC0 通道 0／ADC1 通道 1 |
| CH2 继电器前／电阻后 | PA2／PA3 | ADC0 通道 2／ADC1 通道 3 |
| CH3 继电器前／电阻后 | PA4／PA5 | ADC0 通道 4／ADC1 通道 5 |
| CH4 继电器前／电阻后 | PA6／PA7 | ADC0 通道 6／ADC1 通道 7 |
| ISO_+3V 监测 | PB0 | ADC0 通道 8，是被测信号而非 ADC 参考引脚 |
| SWD | PA13、PA14 | 保留，JTAG 禁用，不占用 SWD |

USART2 按主控通信协议配置为 9600、8E1。USART0 和 UART4 使用 115200、8N1。

## 文件分工

* `boards/custom/gd32f303rc/precharge.dtsi`：ADC 通道、模拟引脚、产线/RS485 UART 和 GPIO 映射。
* `boards/custom/gd32f303rc/board.c`：GD32F30x 板级初始化，释放 PB3/PB4 的 JTAG 复用，保留 SWD；ADC 时钟配置为 APB2/4，即当前主频配置下 30 MHz。
* `bsp/board_gpio.c`：将继电器、LED 和 RS485 方向信号映射到设备树并统一初始化。
* `bsp/board_adc.c`：初始化 ADC 连续采样，提供电压读取接口。
* `apps/bringup/precharge/precharge.c` 管理指示灯心跳，`gpio_shell.c` 提供继电器调试命令。
* `bsp/serial_rx.c`：检查业务串口并初始化中断接收。
* `bsp/inc/`：BSP 对外头文件，由应用调用各模块的初始化接口。

当前 GD32F30x ADC 驱动未设置共享 ADC 预分频，故在 ADC 驱动初始化和校准之前通过板级 HAL 配置，未修改上游通用驱动。30 MHz 不超过 GD32F303 数据手册的 40 MHz ADC 时钟上限。

## ADC 参数与诊断

配置 9 路单端输入，12 位，增益 1，采样时间为 239.5 个 ADC 时钟周期。BSP 配置 ADC0／ADC1 规则组并行，前四个节拍成对采四路回读，第五个节拍读取 PB0。ADC0 软件触发一次后连续扫描，DMA0 通道 0 循环保存两轮 32 位结果。设备树的 ADC0 条目用于配置引脚与电压换算，ADC1 由 BSP 独占。用 `adc` 或 `board adc` 检查结果。

GD32 驱动的 `ADC_REF_INTERNAL` 在这里表示使用 VDDA，设备树声明名义值 3300 mV。它不是用 PB0 的 3 V 信号作为硬件参考源。

在 UART4 Shell 中执行：

```text
board adc
```

依次显示 `ch1-before`、`ch1-after` 至 `ch4-after` 和 `reference-3v` 的原始 ADC 值及引脚电压。输出 mV 采用名义 VDDA，并非被测高压侧的真实电压。高压换算、零点、增益、温漂及现场标定尚未实现。

串口接收已增加中断环形缓冲，见 [业务串口接收缓冲](Serial_RX.md)；协议和发送控制仍未实现。

## 输出与通信范围

继电器初始化为断开，RS485 初始化为接收。在 UART4 Shell 中用 `gpio <3-6> <0|1>` 设置继电器控制引脚。PB3～PB6 的高电平会直接吸合对应继电器，Shell 命令绕过预充时序和互锁。PB7 由应用每约 500 ms 切换一次，PB12 由 RS485 线程控制。

应用用 `app_board_gpio_write(BOARD_GPIO_OUT_JDQ1, true)` 吸合 JDQ1，预充时序和互锁由调用方负责。`app_board_gpio_write(BOARD_GPIO_OUT_RS485_DE, true)` 将 RS485 切到发送，调用方须等待 UART 最后一位停止位发完再切回接收。

原理图中的运放、光耦、电源芯片和 RS485 收发器不需要独立的 SPI/I2C 软件驱动。图中的 DO_IN 没有可确认的 MCU 引脚连接，未擅自分配。

## 离线验证结果

本次 `west build` 联合编译通过。MCUboot Flash 为 29,176 B，应用 Flash 为 77,980 B、RAM 为 11,968 B，均未超出已有分区。签名验证和 Flash 分区边界检查通过。

设备树检查覆盖 PB7 指示灯、PB12 方向口、4 路继电器映射顺序、9 路 ADC 通道和 3 个 UART 的启用状态。工作区根目录可执行：

```powershell
python firmware/tests/board/check_precharge.py build/bringup_gd32f303rc_mcuboot/bringup
python firmware/tests/storage/check_layout.py build/bringup_gd32f303rc_mcuboot
```

新增 C/H 文件 checkpatch 为 0 个错误，仍有缺少 SPDX 的警告，沿用仓库尚未选定源码许可证的状态。

## 验证边界

当前仅配置、编译和检查软件映射，没有烧录或接入高压测试。需实板验证：

1. UART4 Shell 和 PB7 LED 工作正常，DAP-Link 仍能通过 SWD 连接。
2. 继电器输出保持低，核对 PB3/PB4 已释放，测量复位及 bootloader 阶段输出波形。
3. 用已知低压输入检查 PA0～PA7、PB0 的原始读数和通道顺序，确认采样与名义 VDDA 的误差。
4. 核对 PB12 方向控制后，用匹配串口参数验证 USART2 通信和收发切换。
5. USART0 需另接产线设备测试，不能将“驱动 ready”视为协议已经通过。

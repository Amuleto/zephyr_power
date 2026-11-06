# 预充检测板硬件配置

## 依据与实板差异

依据用户提供的《预充检测.pdf》：第 1 页原理图、第 2 页 PCB、第 3 页 BOM。MCU 为 U38 GD32F303RCT6，X1 为 8 MHz。

用户明确确认：继续使用原实板，LED 为 PB12，高电平亮。因此不采用原理图的 PB7 LED，原理图标注的 PB12 RS485 方向也不能照搬。

按用户要求，选择原理图未连接、当前软件未占用的 PB8 作为 RS485 收发方向。实板必须将方向控制线连接或改接到 PB8。软件配置不能替代这项接线；尚未核实 PCB 实际铜线和焊接状态。

## 引脚表

| 功能 | MCU 引脚 | 当前配置 |
| --- | --- | --- |
| 调试日志和 Shell | UART4 TX PC12、RX PD2 | 115200，8N1，沿用已验证配置 |
| 产线串口 J14 | USART0 TX PA9、RX PA10 | 115200，8N1，仅初始化驱动，无产线协议 |
| RS485 串口 | USART2 TX PB10、RX PB11 | 115200，8N1，仅初始化驱动，无 Modbus/上传协议 |
| RS485 DE 与 /RE | PB8 | 高发送、低接收，默认低；需实际接线 |
| 实板指示灯 | PB12 | 高电平亮，沿用原 LED 应用 |
| JDQ1 | PB6 | 高电平有效，初始化为低 |
| JDQ2 | PB5 | 高电平有效，初始化为低 |
| JDQ3 | PB4 | 高电平有效，初始化为低；释放 JTAG 引脚 |
| JDQ4 | PB3 | 高电平有效，初始化为低；释放 JTAG 引脚 |
| CH1 继电器前／电阻后 | PA0／PA1 | ADC0 通道 0／1 |
| CH2 继电器前／电阻后 | PA2／PA3 | ADC0 通道 2／3 |
| CH3 继电器前／电阻后 | PA4／PA5 | ADC0 通道 4／5 |
| CH4 继电器前／电阻后 | PA6／PA7 | ADC0 通道 6／7 |
| ISO_+3V 监测 | PB0 | ADC0 通道 8，是被测信号而非 ADC 参考引脚 |
| SWD | PA13、PA14 | 保留，JTAG 禁用，不占用 SWD |

115200 是新增两个串口的开发默认值，原理图本身不定义通信速率；对接设备前需统一速率、校验和协议。

## 文件分工

* `boards/custom/gd32f303rc/precharge.dtsi`：ADC 通道、模拟引脚、产线/RS485 UART 和 GPIO 映射。
* `boards/custom/gd32f303rc/board.c`：GD32F30x 板级初始化，释放 PB3/PB4 的 JTAG 复用，保留 SWD；ADC 时钟配置为 APB2/6，即当前主频配置下 20 MHz。
* `apps/bringup/src/board_io.c`、`board_io.h`：通过通用 Zephyr GPIO/ADC 接口初始化、访问继电器及方向口，提供 ADC 只读 Shell 命令。
* 原有 `led.c` 保留 PB12，所有新增板级引脚从设备树获取。

当前 GD32F30x ADC 驱动未设置共享 ADC 预分频，故在 ADC 驱动初始化和校准之前通过板级 HAL 配置，未修改上游通用驱动。20 MHz 不超过 GD32F303 数据手册的 40 MHz ADC 时钟上限。

## ADC 参数与诊断

配置 9 路单端输入，12 位，增益 1；采样时间选择驱动支持的 240 tick 档，硬件对应 239.5 个采样周期。当前使用软件触发、同步读取，没有启用 DMA 和后台周期采样。

GD32 驱动的 `ADC_REF_INTERNAL` 在这里表示使用 VDDA，设备树声明名义值 3300 mV。它不是用 PB0 的 3 V 信号作为硬件参考源。

在 UART4 Shell 中执行：

```text
board adc
```

依次显示 `ch1-before`、`ch1-after` 至 `ch4-after` 和 `reference-3v` 的原始 ADC 值及引脚电压。输出 mV 采用名义 VDDA，并非被测高压侧的真实电压。高压换算、零点、增益、温漂及现场标定尚未实现。

串口接收已增加中断环形缓冲，见 [业务串口接收缓冲](Serial_RX.md)；协议和发送控制仍未实现。

## 输出与通信范围

初始化继电器为非有效态、RS485 为接收态，不自动吸合继电器，也不自动发送产线或 RS485 数据。未添加继电器 Shell 写命令。

`app_board_relay_set()` 只是底层输出接口，不包含预充顺序、超时、互锁或故障处理。`app_board_rs485_direction_set()` 仅控制 PB8；将来发送时必须在 UART 最后一位停止位发送完毕后才切回接收，不能仅以软件缓冲区写完作为完成条件。

原理图中的运放、光耦、电源芯片和 RS485 收发器不需要独立的 SPI/I2C 软件驱动。图中的 DO_IN 没有可确认的 MCU 引脚连接，未擅自分配。

## 离线验证结果

本次 `west build` 联合编译通过。MCUboot Flash 为 29,176 B，应用 Flash 为 77,980 B、RAM 为 11,968 B，均未超出已有分区。签名验证和 Flash 分区边界检查通过。

已检查生成的设备树，确认 PB12 LED 与 PB8 方向口不冲突、4 路继电器映射顺序、9 路 ADC 通道和 3 个 UART 的启用状态。工作区根目录可重复执行：

```powershell
python firmware/tests/board/check_precharge.py build/bringup_gd32f303rc_mcuboot/bringup
python firmware/tests/storage/check_layout.py build/bringup_gd32f303rc_mcuboot
```

新增 C/H 文件 checkpatch 为 0 个错误，仍有缺少 SPDX 的警告，沿用仓库尚未选定源码许可证的状态。

## 验证边界

当前仅配置、编译和检查软件映射，没有烧录或接入高压测试。需实板验证：

1. UART4 Shell 和 PB12 LED 保持正常，DAP-Link 仍能通过 SWD 连接。
2. 继电器输出保持低，核对 PB3/PB4 已释放，测量复位及 bootloader 阶段输出波形。
3. 用已知低压输入检查 PA0～PA7、PB0 的原始读数和通道顺序，确认采样与名义 VDDA 的误差。
4. 完成 PB8 方向线接线后，用匹配串口参数验证 USART2 通信和收发切换。
5. USART0 需另接产线设备测试，不能将“驱动 ready”视为协议已经通过。

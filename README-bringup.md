# GD32F303RCT6 bring-up

独立板级配置位于 `boards/custom/gd32f303rc`，应用位于 `apps/bringup`，厂商原有 START 板配置保持不变。

## 硬件

- GD32F303RCT6：256 KiB Flash、48 KiB SRAM。
- 8 MHz 外部晶振，厂商 HAL SystemInit 配置 PLL 到 120 MHz。
- UART4：PC12 TX、PD2 RX，115200、8N1；USB 转串口需 3.3V 电平并共地。
- PB12 高电平点亮，每 500 ms 切换一次，同时输出 LED ON/OFF。
- DAP-Link：SWDIO PA13、SWCLK PA14、GND，按调试器要求接目标电压参考。

## 编译

在 CMD 中运行 `build-gd32.cmd`，脚本设置自己的工具路径，无需手动激活 Python 环境。

输出：`../build/bringup_gd32f303rc/zephyr/zephyr.elf`、`zephyr.hex`、`zephyr.bin`。

## 烧录

`flash-gd32.cmd` 会覆盖目标板程序，确认可以覆盖后自行运行。
OpenOCD 配置采用 CMSIS-DAP / SWD 和 stm32f1x 兼容 Flash 驱动。已实测 DAP-Link 连接、读取寄存器和固件校验；该脚本的擦除、写入流程尚未实测。
若芯片探测失败，请保留完整日志，勿强制跳过 ID 检查或执行整片擦除。

## 验证范围

2026-11-03：已编译通过，用户烧录后确认 PB12 LED 闪烁和 UART4 串口输出正常。
设备树仍有厂商已有 EXTI 节点缺少 #address-cells 警告，不影响当前示例构建。
west.yml 仅包含本示例必需的 cmsis、cmsis_6 和 hal_gigadevice 模块，其他功能需先补充 manifest 依赖。

官方器件容量：https://www.gigadevice.com/product/mcu/mcus-product-selector/gd32f303rct6


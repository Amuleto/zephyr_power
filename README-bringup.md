# GD32F303RCT6 bring-up

独立板级配置位于 `boards/custom/gd32f303rc`，应用位于 `apps/bringup`，厂商原有 START 板配置保持不变。

## 硬件

- GD32F303RCT6：256 KiB Flash、48 KiB SRAM。
- 8 MHz 外部晶振，厂商 HAL SystemInit 配置 PLL 到 120 MHz。
- USART1：PA2 TX、PA3 RX，115200、8N1；USB 转串口需 3.3V 电平并共地。
- PB15 高电平点亮，每 500 ms 切换一次，同时输出 LED ON/OFF。
- DAP-Link：SWDIO PA13、SWCLK PA14、GND，按调试器要求接目标电压参考。

## 编译

在 CMD 中运行 `build-gd32.cmd`，脚本设置自己的工具路径，无需手动激活 Python 环境。

输出：`../build/bringup_gd32f303rc/zephyr/zephyr.elf`、`zephyr.hex`、`zephyr.bin`。

## 烧录

`flash-gd32.cmd` 会覆盖目标板程序，确认可以覆盖后自行运行。
OpenOCD 配置采用 CMSIS-DAP / SWD 和 stm32f1x 兼容 Flash 驱动；目前仅完成脚本解析检查，尚未实测连接、芯片识别、Flash 页擦除和下载。
若芯片探测失败，请保留完整日志，勿强制跳过 ID 检查或执行整片擦除。

## 验证范围

已完成 SDK 编译、链接及生成配置检查。尚未验证硬件上的晶振启动、串口波特率、LED 极性、DAP-Link 连接和烧录。
源码 ZIP 不含 Git 版本信息，构建会提示无法取得 Git 版本；设备树另有厂商已有 EXTI 节点缺少 #address-cells 警告。
当前只下载本示例必需的 cmsis、cmsis_6 和 hal_gigadevice 模块，其他功能可能需要额外 west update。

官方器件容量：https://www.gigadevice.com/product/mcu/mcus-product-selector/gd32f303rct6


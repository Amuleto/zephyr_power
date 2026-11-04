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

## Shell 与日志

新固件启用分级日志和 UART4 Shell（115200、8N1），日志通过 Shell 后端输出。
串口终端按回车后输入：

| 命令 | 效果 |
| --- | --- |
| `help` | 查看命令 |
| `led on` | LED 常亮 |
| `led off` | LED 熄灭 |
| `led blink` | 恢复每 500 ms 切换 |
| `uptime` | 显示开机运行毫秒数 |

上电默认闪烁；不再每次闪烁打印日志。Shell 需要双向接线：PC12 接 USB 串口 RX，PD2 接 TX，并共地。
LED 控制使用互斥锁，避免 Shell 与主线程同时操作。新增 Shell 交互尚待实板验证，之前的实板验证仅覆盖旧版 LED 和日志输出。

## 任务健康监控和时间

- 主循环完成一次 GPIO 操作后上报健康，间隔约 500 ms；连续 3000 ms 未上报，Task Watchdog 请求复位。
- 使用 GD32 FWDGT 作为硬件后备，名义超时约 3000 ms，实际受内部低速振荡器精度影响。后备由 Task Watchdog 管理，不能另起任务无条件喂狗。
- 当前仅监控主循环，不把等待用户输入的 Shell 当作失活任务。后续业务线程需各自注册 `task_wdt_add()` 并在完成有效工作后 `task_wdt_feed()`。
- 看门狗初始化失败会记录错误并停止主应用；软件超时复位、硬件后备复位及调试暂停行为尚未实板验证。
- `date` 查询 UTC 硬件 RTC；`date 2026-11-04 08:00:00` 设置 UTC 时间（北京时间减 8 小时）。每次启动后需重新设时，避免把掉电停走后的旧值当作有效日期。
- RTC 时钟为 8 MHz HXTAL / 128 = 62500 Hz，预分频为 62499，秒计数器运行在 1 Hz。板级 DTS 配置 `gd,gd32-rtc` 节点，`gd32_zephyr/drivers/rtc/rtc_gd32.c` 实现 Zephyr RTC API，应用只调用通用接口。
- HXTAL 在核心电源域掉电时停止，不能提供仅 VBAT 供电时的连续计时；不支持掉电走时或自动校时。尚未实板验证计时精度及复位行为。
- 已存在其他 RTC 时钟源时返回 -ENOTSUP，不会自动复位备份域；需要另行确认备份域数据后才能切换。初始化和写入等待超时为 200 ms。
- 驱动首版支持 GD32F30x、2000—2099 年、整秒设时和读时。每次启动后首次设时前读取返回 -ENODATA；暂不支持闹钟、校准、更新回调和掉电保持。
- 当前驱动在本地 `gd32_zephyr` 的 `feature/gd32-rtc` 分支。发布前需提交到自己的 fork，并同步 firmware/west.yml 的 URL 和 revision，否则 west update 不会取得本地驱动修改。

## 烧录命令

`flash-gd32.cmd` 会覆盖目标板程序，确认可以覆盖后自行运行。
OpenOCD 配置采用 CMSIS-DAP / SWD 和 stm32f1x 兼容 Flash 驱动。已实测 DAP-Link 连接、读取寄存器和固件校验；该脚本的擦除、写入流程尚未实测。
若芯片探测失败，请保留完整日志，勿强制跳过 ID 检查或执行整片擦除。

## 验证范围

2026-11-03：已编译通过，用户烧录后确认 PB12 LED 闪烁和 UART4 串口输出正常。
设备树仍有厂商已有 EXTI 节点缺少 #address-cells 警告，不影响当前示例构建。
west.yml 仅包含本示例必需的 cmsis、cmsis_6 和 hal_gigadevice 模块，其他功能需先补充 manifest 依赖。

官方器件容量：https://www.gigadevice.com/product/mcu/mcus-product-selector/gd32f303rct6


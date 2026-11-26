# GD32F303RCT6 bring-up

独立板级配置位于 `boards/custom/gd32f303rc`，应用位于 `apps/bringup`，厂商原有 START 板配置保持不变。

## 应用代码结构

- `bsp/main.c`：完成板级初始化，启动应用线程后返回。
- `bsp/board_startup.c`：提供 `board_init()`，统一初始化 GPIO、ADC、业务串口，检查 RTC 并准备看门狗。
- `bsp/board_watchdog.c`：提供硬件后备看门狗初始化、任务通道注册和进度上报接口。
- `apps/bringup/app.c`：通过 `app_run()` 加载参数、确认镜像并启动预充和 RS485 线程。
- `apps/bringup/precharge/` 存放预充和 MP10 实现，公共接口为 `inc/precharge.h`。
- `apps/bringup/host_link/` 存放 RS485 任务实现，公共接口为 `inc/host_link.h`。
- `bsp/`：板级 GPIO、ADC 和业务串口接收，公共头文件位于 `bsp/inc/`。LED 闪烁和 Shell 调试逻辑位于应用层。
- `apps/bringup/datetime.c`：日期解析、RTC 设时/读时，以及 date、uptime 命令。
- `apps/bringup/prj.conf`：按 GPIO、Shell、日志、看门狗、RTC 和构建产物分组。

板级初始化启动 CAN 控制器；预充线程安装 MP10 接收过滤器，约每 20 ms 更新 MP10 和预充状态，
每约 500 ms 翻转心跳灯，每轮完成后喂任务看门狗。电压采集与继电器操作位于 precharge_io.c，
预充时序和对外请求接口位于 precharge_ctrl.c，任务入口保留模块调度。

原理图外设配置见 [预充检测板硬件说明](docs/Precharge_Hardware.md)。新增 `board adc` 读取 9 路 ADC，继电器保持关闭，RS485 方向使用待接线的 PB8。

## 硬件

- GD32F303RCT6：256 KiB Flash、48 KiB SRAM。
- 8 MHz 外部晶振，厂商 HAL SystemInit 配置 PLL 到 120 MHz。
- UART4：PC12 TX、PD2 RX，115200、8N1；USB 转串口需 3.3V 电平并共地。
- PB7 高电平点亮，应用每约 500 ms 切换一次。
- DAP-Link：SWDIO PA13、SWCLK PA14、GND，按调试器要求接目标电压参考。

## 编译

在已配置工具 PATH 的终端进入工作区根目录，运行 `west build`。首次配置或删除整个 build 后，运行 `west build firmware/apps/bringup`。环境配置见 [工程说明](README.md)。

当前采用 MCUboot 联合构建，产物位于 `../build/bringup_gd32f303rc_mcuboot/`。首次烧录需要 bootloader 和签名应用，分区与命令见 [MCUboot 与参数存储](docs/Storage_and_MCUboot.md)。

`apps/bringup/params.c` 和 `inc/params.h` 提供 Settings/NVS 参数读写，Shell 使用 `param get`、`param set <id>`。

USART0 和 USART2 各有 512 字节 RX 环形缓冲。`serial stats` 查看两路状态，`serial read factory` 读取产线串口。RS485 线程独占 USART2 和 PB8。

RS485 使用 9600、8E1，预充板发起心跳并根据主控应答校准 RTC。双 ADC 连续扫描，使用 UART4 的 `adc` 查询 MCU 引脚电压。自动检测时序待定，继电器可通过 UART4 的 `gpio` 命令调试。MP10 测量和上电自检状态由预充线程维护。

## Shell 与日志

新固件启用分级日志和 UART4 Shell（115200、8N1），日志通过 Shell 后端输出。
串口终端按回车后输入：

| 命令 | 效果 |
| --- | --- |
| `help` | 查看命令 |
| `gpio <3-6> <0\|1>` | 手动设置继电器控制引脚 |
| `uptime` | 显示开机运行毫秒数 |

上电默认闪烁；不再每次闪烁打印日志。Shell 需要双向接线：PC12 接 USB 串口 RX，PD2 接 TX，并共地。

## 任务健康监控和时间

- 预充线程每轮完成后上报健康；连续 3000 ms 未上报时，Task Watchdog 请求复位。
- 使用 GD32 FWDGT 作为硬件后备，名义超时约 3000 ms，实际受内部低速振荡器精度影响。后备由 Task Watchdog 管理，不能另起任务无条件喂狗。
- 预充和 RS485 在线程入口各自注册一个 3 秒看门狗通道。RS485 最多等待 100 ms，空闲时仍上报进度。已注册的线程停止上报将触发复位。
- 看门狗初始化失败会记录错误并停止主应用；软件超时复位、硬件后备复位及调试暂停行为尚未实板验证。
- `date` 查询 UTC 硬件 RTC；`date 2026-11-04 08:00:00` 设置 UTC 时间（北京时间减 8 小时）。每次启动后需重新设时，避免把掉电停走后的旧值当作有效日期。
- RTC 时钟为 8 MHz HXTAL / 128 = 62500 Hz，预分频为 62499，秒计数器运行在 1 Hz。板级 DTS 配置 `gd,gd32-rtc` 节点，`gd32_zephyr/drivers/rtc/rtc_gd32.c` 实现 Zephyr RTC API，应用只调用通用接口。
- HXTAL 在核心电源域掉电时停止，不能提供仅 VBAT 供电时的连续计时；不支持掉电走时或自动校时。尚未实板验证计时精度及复位行为。
- 已存在其他 RTC 时钟源时返回 -ENOTSUP，不会自动复位备份域；需要另行确认备份域数据后才能切换。初始化和写入等待超时为 200 ms。
- 驱动首版支持 GD32F30x、2000—2099 年、整秒设时和读时。每次启动后首次设时前读取返回 -ENODATA；暂不支持闹钟、校准、更新回调和掉电保持。
- GD32 底层支持统一在自己的 fork 的 `gd32-dev` 长期分支维护，RTC、看门狗及后续外设按功能分别提交，不要求每个外设建立分支。firmware/west.yml 固定到该分支上经过验证的具体提交；分支改名不影响现有提交号。

## 烧录命令

在工作区根目录执行 `west flash`，会检查构建并覆盖连接目标板的程序。
OpenOCD 配置采用 CMSIS-DAP / SWD 和 stm32f1x 兼容 Flash 驱动。已实测 DAP-Link 连接、读取寄存器和固件校验；用户已确认 west 烧录成功；当前源码的验证范围见下文。
若芯片探测失败，请保留完整日志，勿强制跳过 ID 检查或执行整片擦除。

## 验证范围

2026-11-03：已编译通过，用户烧录后确认 PB12 LED 闪烁和 UART4 串口输出正常。
设备树仍有厂商已有 EXTI 节点缺少 #address-cells 警告，不影响当前示例构建。
west.yml 包含本示例使用的 MCUboot、cmsis、cmsis_6 和 hal_gigadevice 模块，其他功能需先补充 manifest 依赖。

官方器件容量：https://www.gigadevice.com/product/mcu/mcus-product-selector/gd32f303rct6


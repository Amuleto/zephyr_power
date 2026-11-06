# MCUboot 分区与参数存储

## 当前布局

目标为 GD32F303RCT6：256 KiB 内部 Flash，bank 0 擦除页为 2 KiB。分区以 [设备树](../boards/custom/gd32f303rc/partitions.dtsi) 为唯一来源。

| 区域 | 起始地址 | 结束地址（不包含） | 大小 |
| --- | --- | --- | --- |
| MCUboot | 0x08000000 | 0x08008000 | 32 KiB |
| app 主槽 | 0x08008000 | 0x08021000 | 100 KiB |
| OTA 次槽 | 0x08021000 | 0x0803A800 | 102 KiB |
| Settings/NVS 参数 | 0x0803A800 | 0x0803C800 | 8 KiB |
| 业务数据预留 | 0x0803C800 | 0x08040000 | 14 KiB |

合计 256 KiB，无重叠且全部按 2 KiB 对齐。业务数据区只是预留，目前没有文件系统或记录存储实现。

MCUboot 采用 ECDSA-P256 校验和 swap-using-offset。次槽比主槽多一页，更新镜像从次槽第二页开始，即 0x08021800。不要将普通 app BIN 直接写到 OTA 分区起点。将来接入下载通道时应使用 Zephyr image manager API 处理偏移、边界和写入，而不是在业务代码中硬编码地址。

升级状态由 MCUboot 管理，保存在槽尾的 trailer 中，不再单独划分“升级状态区”。100 KiB 主槽包含镜像头、程序、签名元数据及 trailer，不等于程序正文可以使用全部 100 KiB。32 KiB boot 是当前精简配置的容量，增加串口恢复、其他签名算法或功能后需要重新测量。

## 编译与烧录

依赖通过 firmware/west.yml 固定 MCUboot 提交，跟随当前 GD32 Zephyr 使用的版本。恢复工作区后安装签名工具依赖：

```powershell
west update
python -m pip install -r bootloader/mcuboot/scripts/requirements.txt
```

在工作区根目录首次联合构建：

```powershell
west build --sysbuild -b gd32f303rc firmware/apps/bringup -d build/bringup_gd32f303rc_mcuboot -- "-DBOARD_ROOT=$((Resolve-Path firmware).Path.Replace('\', '/'))"
```

应用目录的 `sysbuild.conf` 选择 MCUboot、ECDSA-P256 和 offset 交换模式，`sysbuild/mcuboot.conf` 配置 51 页的镜像槽上限。镜像头和签名由 sysbuild 统一生成。

若本地默认目录和 sysbuild 已按工程 README 设置，后续仍用 `west build` 和 `west flash`。首次烧录必须包含 MCUboot 和签名应用，使用 sysbuild 根目录执行 west flash 会按 domains.yaml 的顺序烧录两个域；不要只烧录旧 build 下的单应用文件。

产物相对于 `build/bringup_gd32f303rc_mcuboot/`：

* `mcuboot/zephyr/zephyr.hex`：bootloader，地址 0x08000000。
* `bringup/zephyr/zephyr.signed.hex`：签名应用，地址 0x08008000。
* `bringup/zephyr/zephyr.signed.bin`：签名镜像，可供后续更新通道使用，不是完整芯片镜像。

当前使用 MCUboot 仓库中公开的测试密钥，只用于开发。生产前必须自行生成和保管密钥，重新构建匹配的 bootloader 和签名应用；私钥不得提交到公开仓库。当前没有实现串口/网络固件上传通道，也未完成升级掉电测试。

## 参数接口

`apps/bringup/src/params.c` 使用 Settings + NVS，参数分区为 4 个 2 KiB 扇区，启用 NVS 数据 CRC。NVS 需保留空间用于垃圾回收，因此 8 KiB 分区不等于 8 KiB 净参数容量。

首个示例参数为 `app/device_id`，默认值 1，可取完整 uint32_t 范围。以固定 4 字节小端形式持久化，避免直接保存结构体带来的填充与版本问题。

```text
param get
param set 123
param get
```

* 启动时初始化并加载参数，无记录时使用默认值，不会每次启动写入。
* 设置成功后才更新 RAM；重复设置当前值不写 Flash。
* 读取和修改由互斥锁保护；存储失败返回错误，不自动清空分区。
* 接口见 `params.h`，不要从中断中调用 Flash 写入操作。
* `mcuboot` 命令可查询镜像状态；Shell 中的擦除和请求升级命令是实际操作，不是模拟。

应用在参数、LED、看门狗等现有启动步骤完成后确认当前镜像。后续产品应将确认时机扩展到真实业务健康检查通过后；目前的启动确认不等于验证了整机业务。

## 本次离线验证

2026-11-04（加入预充板外设之前的基线）：sysbuild 联合构建通过。MCUboot 使用 Flash 28,716 B、RAM 15,808 B；应用使用 Flash 74,388 B、RAM 11,712 B。该结果对应当前 ECDSA-P256 开发配置，不代表增加其他功能后的大小。

已验证分区总长、擦除页对齐、无重叠、HEX 链接地址与边界、签名校验及修改镜像后校验失败。两个域的 OpenOCD runner 配置已通过 `west flash --no-rebuild --context --domain <域名>` 检查，未操作目标硬件。

可从工作区运行布局检查：

```powershell
python firmware/tests/storage/check_layout.py build/bringup_gd32f303rc_mcuboot
```

checkpatch 对新增 params.c/params.h 报告 0 个错误，仅有缺少 SPDX 标识的警告，源码许可证仍需项目所有者决定。构建仍有原有 EXTI 设备树警告。

## 实板验证步骤

以下项目尚未执行，需接板验证：

1. 通过 sysbuild 烧录 MCUboot 和签名应用，复位，检查 boot 日志、LED、Shell 和 RTC。
2. `param get` 应显示默认值 1；执行 `param set 123` 后再读应为 123。
3. 复位和断电重上电后，`param get` 应仍为 123。
4. 测试 `param set 0`、`param set 4294967295`；负数、超范围值、混合字符应被拒绝，旧值不变。
5. 通过只擦除镜像涉及扇区的常规烧录更新应用，确认参数保留；整片擦除会删除参数。
6. 后续接入固件上传后，测试新镜像确认、未确认回滚，以及交换中途断电恢复。当前不宣称已验证这些行为。

## 官方参考

* [MCUboot 与 Zephyr 集成](https://docs.mcuboot.com/readme-zephyr.html)
* [MCUboot 交换与 trailer 设计](https://docs.mcuboot.com/design.html)
* [Zephyr Settings](https://docs.zephyrproject.org/latest/services/storage/settings/index.html)

# Shared firmware

文档入口：[工程文档](docs/README.md)，编码遵循 [Zephyr 中文编码风格指南](docs/Style_Guide.md)。

此目录作为独立 Git 仓库维护，同一应用通过 Board、overlay 和配置片段适配不同芯片，不为每块板复制业务代码。

- `apps/`: 应用入口和应用配置，当前为 `bringup`。
- `boards/`: 自定义板子，当前为 `custom/gd32f303rc`。
- `west.yml`: 固定已验证的 GD32 Zephyr、CMSIS 和 HAL 提交。包含 MCUboot；当前依赖覆盖 bringup；新增功能需添加其依赖。
- `README-bringup.md`: 接线、运行预期及验证范围。

需要公共业务库、自定义驱动和自动测试时，再新增 `lib/`、`drivers/`、`tests/`，避免预先建立空目录。

## Windows 构建

将安装 west 的 Python 环境 Scripts 目录以及 CMake、Ninja、gperf、DTC 目录加入 PATH。当前 west 位于 `D:\build\zephyr-venv312\Scripts`；SDK 位于 `D:\build\zephyr-sdk-1.0.1`。修改用户 PATH 后重新打开终端，用 `west --version` 确认。

在工作区根目录执行：

```powershell
west build
west flash
```

`west flash` 会检查构建并烧录连接的目标板。当前默认设置保存在工作区 `.west/config`，不属于 firmware Git 仓库。

换电脑或恢复工作区后，从工作区根目录设置默认值：

```powershell
west config build.board gd32f303rc
west config build.dir-fmt build/bringup_gd32f303rc_mcuboot
west config build.sysbuild true
west config build.cmake-args "-DBOARD_ROOT=$((Resolve-Path firmware).Path.Replace('\', '/'))"
west build firmware/apps/bringup
```

删除整个 build 目录后，首次运行 `west build firmware/apps/bringup`，以后继续用 `west build`。产物在 `build/bringup_gd32f303rc_mcuboot/bringup/zephyr/`。

若当前终端尚未配置 west 的 PATH，可以临时执行：

```powershell
$env:Path = 'D:\build\zephyr-venv312\Scripts;' + $env:Path
west --version
```

这只影响当前终端；长期使用应把对应目录加入用户 PATH。构建工具也应能通过 PATH 找到；必要时设置 `ZEPHYR_SDK_INSTALL_DIR` 指向已安装的 SDK。

## 从仓库恢复工作区

分区、首次联合烧录及参数命令见 [MCUboot 与参数存储](docs/Storage_and_MCUboot.md)。

将本仓库克隆为一个空工作区下的 `firmware`，在工作区运行 `west init -l firmware`、`west update`，再在 Python 3.12 环境安装 `gd32_zephyr/scripts/requirements-base.txt` 和 `bootloader/mcuboot/scripts/requirements.txt`。安装 SDK 和工具、配置 PATH 后，按上面的 west 命令设置并构建。

此目录的 `west.yml` 是工作区依赖入口；更新依赖时先调整提交、编译和验证，再提交 manifest。无需修改厂商源码里的 west.yml。

## 上传 GitHub

只上传本 `firmware` Git 仓库。Zephyr、modules、构建产物和 archives 位于工作区外层，不属于本仓库。仓库不包含 SDK 或 Python 环境，使用者需自行安装。

仓库尚未选择开源许可证；公开源码不等于授予开源使用许可。发布前请按自己的授权意愿补充 LICENSE。

# Shared firmware

此目录作为独立 Git 仓库维护，同一应用通过 Board、overlay 和配置片段适配不同芯片，不为每块板复制业务代码。

- `apps/`: 应用入口和应用配置，当前为 `bringup`。
- `boards/`: 自定义板子，当前为 `custom/gd32f303rc`。
- `west.yml`: 固定已验证的 GD32 Zephyr、CMSIS 和 HAL 提交。当前最小依赖仅覆盖 bringup；新增功能需添加其依赖。
- `build-gd32.cmd` / `flash-gd32.cmd`: Windows 编译与烧录入口。
- `README-bringup.md`: 接线、运行预期及验证范围。

需要公共业务库、自定义驱动和自动测试时，再新增 `lib/`、`drivers/`、`tests/`，避免预先建立空目录。

## Windows 构建

运行 `build-gd32.cmd`，无需手动激活环境。默认工具位于 D:\build，SDK 为 1.0.1，Python 为 3.12.10。产物位于工作区 `build/bringup_gd32f303rc/zephyr/`。

脚本通过 `env.cmd` 设置环境，可用 `ZEPHYR_TOOLS_ROOT` 覆盖工具根目录，或分别设置 `VIRTUAL_ENV` 和 `ZEPHYR_SDK_INSTALL_DIR`。CMake、Ninja、gperf、DTC 若采用其他目录布局，请提前加入 PATH。

## 从仓库恢复工作区

将本仓库克隆为一个空工作区下的 `firmware`，在工作区运行 `west init -l firmware`、`west update`，再在 Python 3.12 环境安装 `gd32_zephyr-main/scripts/requirements-base.txt`。安装 SDK 和工具后执行构建脚本。

此目录的 `west.yml` 是工作区依赖入口；更新依赖时先调整提交、编译和验证，再提交 manifest。无需修改厂商源码里的 west.yml。

## 上传 GitHub

只上传本 `firmware` Git 仓库。Zephyr、modules、构建产物和 archives 位于工作区外层，不属于本仓库。仓库不包含 SDK 或 Python 环境，使用者需自行安装。

仓库尚未选择开源许可证；公开源码不等于授予开源使用许可。发布前请按自己的授权意愿补充 LICENSE。

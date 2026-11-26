# 工程文档

## 开发入口

* [预充任务设计](预充任务重设计说明.md)：预充阶段、MP10 监测、任务接口及联调验收。
* [主控板与预充板通信协议](主控板与预充板通信协议-V0.1.md)：RS485 帧格式、命令、上报与应答。

* [业务串口接收缓冲](Serial_RX.md)：独立环形缓冲、读取接口、溢出策略及模拟测试。

* [预充检测板硬件配置](Precharge_Hardware.md)：原理图引脚、GPIO 命令、ADC 和串口配置及验证范围。

* [MCUboot 分区与参数存储](Storage_and_MCUboot.md)：Flash 布局、签名构建及验证步骤。当前应用默认关闭 Settings/NVS，参数命令仅在重新启用后可用。

* [项目编码规范](coding_standard.md)：项目自有代码的格式、注释、接口和并发要求。
* [协作与设计规则](../AGENTS.md)：任务职责、接口简化及交付检查要求。
* [Zephyr 风格参考](Style_Guide.md)：阅读或修改 Zephyr 依赖源码时使用。
* [工程说明](../README.md)：目录组织、依赖恢复、构建和 Git 管理。
* [GD32F303 板级说明](../README-bringup.md)：硬件接线、编译烧录及验证说明。

格式配置统一放在 firmware 根目录：

* [`.clang-format`](../.clang-format)：项目 C/C++ 的 4 空格、Allman 大括号和 100 列配置。
* [`.editorconfig`](../.editorconfig)：编辑器缩进、字符编码和换行约定。

## 来源与许可证

编码风格文档参考 Golioth 文档组织方式，已改写为 Zephyr 规则，并在文中保留来源链接和修改说明。[Apache-2.0 许可证副本](LICENSE)适用于相关文档和配置，不代表整个 firmware 仓库已经选择该许可证。

# 工程文档

## 开发入口

* [业务串口接收缓冲](Serial_RX.md)：独立环形缓冲、读取接口、溢出策略及模拟测试。

* [预充检测板硬件配置](Precharge_Hardware.md)：原理图引脚、PB12/PB8 实板调整、ADC 和串口配置及验证范围。

* [MCUboot 分区与参数存储](Storage_and_MCUboot.md)：Flash 布局、签名构建、参数命令和验证步骤。

* [编码风格指南](Style_Guide.md)：Zephyr 中文编码规则、注释与 Doxygen 要求，以及格式检查方法。
* [工程说明](../README.md)：目录组织、依赖恢复、构建和 Git 管理。
* [GD32F303 板级说明](../README-bringup.md)：硬件接线、编译烧录及验证说明。

格式配置统一放在 firmware 根目录：

* [`.clang-format`](../.clang-format)：与当前 gd32_zephyr 一致的 C/C++ 格式配置。
* [`.editorconfig`](../.editorconfig)：编辑器缩进、字符编码和换行约定。

## 来源与许可证

编码风格文档参考 Golioth 文档组织方式，已改写为 Zephyr 规则，并在文中保留来源链接和修改说明。[Apache-2.0 许可证副本](LICENSE)适用于相关文档和配置，不代表整个 firmware 仓库已经选择该许可证。

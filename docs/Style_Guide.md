<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- 2026-11-04：参考 Golioth 文档组织方式重写为 Zephyr 编码规范，不再是 Golioth 原文译本。 -->

# 编码风格指南

本工程新增和修改的 C 代码遵循 Zephyr Coding Style。本文以中文说明常用规则；细节以当前依赖版本的官方文档和相邻代码为准。

## 规范来源与优先顺序

1. 当前 Zephyr 源码中的 [C 代码规范](../../gd32_zephyr/doc/contribute/style/code.rst)和 [Doxygen 规范](../../gd32_zephyr/doc/contribute/style/doxygen.rst)。
2. 对应子系统、文件及函数附近已有的代码风格。
3. 本文明确标注的项目补充约定。

上面的本地文档位于工作区兄弟仓库。在仅克隆 firmware 时，可阅读 [Zephyr 官方编码规范](https://docs.zephyrproject.org/latest/contribute/style/index.html)。

本文参考了 [Golioth Style Guide](https://github.com/golioth/golioth-firmware-sdk/blob/033c129a1722af53612994dec1cff0f90b888f03/docs/Style_Guide.md) 的组织方式，但其 4 空格、Allman 大括号、`//`、`///` 等规则已替换为 Zephyr 规则。Golioth 的其他项目约定不自动适用于本工程。

## 工具与检查

firmware 根目录的 [`.clang-format`](../.clang-format) 复制自当前 gd32_zephyr，使用 Tab、8 列缩进和 100 列行宽。根目录的 [`.editorconfig`](../.editorconfig) 为编辑器提供基础格式设置。

在 firmware 根目录，可以对具体修改文件运行：

```sh
clang-format --dry-run --Werror apps/bringup/src/main.c
clang-format -i apps/bringup/src/main.c
```

`-i` 会修改文件，应检查差异后再提交。clang-format 不能代替人工审查，也不会检查所有命名、注释与接口文档要求。

Zephyr 还提供 `scripts/checkpatch.pl`。官方不支持在原生 Windows 中运行该检查，可在 Linux/WSL 环境中运行，例如从 firmware 根目录：

```sh
git diff --cached | perl ../gd32_zephyr/scripts/checkpatch.pl -
```

应人工判断检查结果，工具可能产生误报。本工程目前没有配置上述检查的 GitHub CI，不应将文档中的命令理解为已经启用自动检查。

## 命名

函数和变量使用 `snake_case`，名称应清楚表达含义；宏和枚举常量通常使用大写下划线形式。遵循所属子系统的现有前缀。

```c
#define RTC_SYNC_TIMEOUT_US 200000

static bool time_valid;

enum led_mode {
	LED_OFF,
	LED_ON,
	LED_BLINK,
};
```

不要强制给所有静态变量添加 `_` 前缀，也不要强制将所有 const 变量名大写。一般不为普通结构体和枚举添加 typedef；已有公共 API 要求的类型名称保持不变。

```c
struct rtc_context {
	bool valid;
};
```

项目补充约定：新文件使用小写下划线名称；时间、频率等量值的名称尽量带单位，例如 `timeout_ms`、`frequency_hz`。

## 缩进、空白与行长度

* 缩进使用 Tab，Tab 按 8 列显示；为对齐需要时使用空格。
* 每行不超过 100 列；文档中的 URL 可按官方例外处理。
* 控制语句关键字与左括号之间留空格，函数名与左括号之间不留空格。
* 逗号后留空格；不保留行尾空白。
* 函数之间空一行，声明和后续执行语句之间空一行。
* 预处理指令从行首开始。
* 指针星号靠近变量名，例如 `const struct device *dev`。

```c
static int set_led(const struct device *dev, bool enabled)
{
	int err;

	err = gpio_pin_set(dev, 12, enabled);
	if (err < 0) {
		return err;
	}

	return 0;
}
```

该片段仅演示格式；实际引脚应来自设备树，不要照抄硬编码引脚。

## 大括号

函数定义的左大括号单独一行。`if`、`else`、`for`、`while`、`switch` 等控制结构的左大括号与语句同行。所有控制结构的主体都加大括号，即使只有一条语句。

```c
static void update_state(bool ready)
{
	if (ready) {
		start_work();
	} else {
		stop_work();
	}
}
```

不使用 Golioth 的 Allman 控制语句大括号风格。

## 变量与函数

变量在所属代码块开头声明，避免在执行语句之间插入声明。按语义选择类型；硬件寄存器、协议字段使用适合其宽度的类型，长度使用 `size_t`，错误码保留 Zephyr API 约定的 `int`。

函数应保持职责清晰，避免过深嵌套。出错时可提前返回；需要统一释放资源时可以使用清理标签。这里不把 Golioth 的“80 行函数”“7 个变量”作为 Zephyr 的硬性限制。

返回值、参数顺序、回调签名以及设备 API 实现遵循所属 Zephyr 子系统，不另行强制所有接口使用相同返回方式。

## 普通注释

普通注释使用 `/* ... */`，不用 `//`。多行注释采用以下形式：

```c
/*
 * Changing the clock source requires a backup-domain reset.
 * Keep the existing configuration to avoid losing retained data.
 */
```

重点说明不直观的设计原因、硬件限制和约束，避免重复显而易见的代码。不得提交被注释掉的废弃代码，应交给 Git 保存历史。UART、CMake 等专有名称使用正确大小写。

项目补充约定：源代码注释使用英文，开发说明使用中文。复杂寄存器操作可注明参考手册章节，但必须与实际芯片及手册版本相符。

内部 `static` 函数不要求统一添加完整 Doxygen 模板；对于复杂算法、并发和硬件限制，应提供必要说明。

## Doxygen 文档

公开头文件及公开 API 符号需要完整文档，并归属至少一个 Doxygen 分组。使用 `/** ... */`，成员的行尾说明可使用 `/**< ... */`。命令使用 `@`，例如 `@param`。

参数按声明顺序记录；写入的指针参数使用 `@param[out]`，读写参数使用 `@param[in,out]`。const 指针和标量可省略输入方向。`@brief` 可省略，此时首句作为简述。

```c
/**
 * @brief Read the stored timestamp.
 *
 * @param dev RTC device.
 * @param[out] timestamp_s Seconds since the Unix epoch. Must not be NULL.
 *
 * @retval 0 Timestamp read successfully.
 * @retval -ENODATA No valid timestamp is available.
 */
int app_rtc_read(const struct device *dev, int64_t *timestamp_s);
```

根据接口实际行为记录有效范围、单位、NULL 是否允许、缓冲区大小、所有权和生命周期。一般返回描述使用 `@return`，具体返回码使用 `@retval`。不要列出实现不会返回的错误码。

公开头文件应包含 `@file` 文档块并归属分组；分组应复用或归属于合适的现有分组。详细写法以当前版本 Doxygen 规范为准。

## 头文件与许可证

使用有唯一名称的 include guard 防止头文件重复包含，遵循 Zephyr 相邻头文件的做法，不强制使用 `#pragma once`。

```c
#ifndef APP_TIME_SERVICE_H_
#define APP_TIME_SERVICE_H_

/* Public declarations. */

#endif /* APP_TIME_SERVICE_H_ */
```

新增源文件应按实际授权提供 SPDX 标识及必要的版权信息。不能因为参考了 Golioth 文档，就将 Golioth 版权名称写入自己的原创代码；也不能将未选择许可证的整个 firmware 仓库自动视为 Apache-2.0。

## 项目补充约定

以下为本工程的开发约定，不声称全部是 Zephyr Coding Style 的强制条款：

* 业务代码通过 Zephyr API 访问硬件；板级引脚、时钟和外设差异放入 Board、设备树和配置。
* 避免无理由的动态分配、递归、共享可变全局状态；确有需要时说明资源上限、并发方式和失败处理。
* 配置、超时和缓冲区容量尽量使用具名常量或 Kconfig，不在多个位置重复魔数。
* 修改行为时执行对应的构建和验证；仅格式修改也要检查差异，避免混入功能变化。
* 不大范围重排无关上游代码；修改现有文件时优先保持所属子系统的一致性。

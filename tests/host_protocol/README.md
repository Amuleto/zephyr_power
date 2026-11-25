# 主板协议收包回归测试

测试直接编译应用的 `host_protocol.c`，在 QEMU 中运行，不需要连接硬件。
覆盖分段与连续帧、噪声与错误长度、校验失败后的缓存排空、载荷中的帧头字节、
半帧超时、组帧结果与容量边界。协议例帧手工计算，避免编码器与解码器共享错误假设。

在工作区根目录、已配置 Python 和 SDK 的终端执行：

```powershell
west build --no-sysbuild -b qemu_cortex_m3 firmware/tests/host_protocol -d build/test_host_protocol
west build --no-sysbuild -d build/test_host_protocol -t run
```

预期为 6 项通过，并出现 `PROJECT EXECUTION SUCCESSFUL`。按 `Ctrl+A` 后按 `X` 退出 QEMU。
测试不覆盖 RS485 方向切换、总线时序和主板实际协议交互，这些需要实板联调。

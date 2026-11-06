# 串口 RX 模拟测试

从工作区根目录构建测试，不使用应用的 MCUboot sysbuild：

```powershell
west build --no-sysbuild -b qemu_cortex_m3 firmware/tests/serial_rx -d build/test_serial_rx
```

本机 SDK 的 QEMU 依赖 MSYS2 DLL，需要在测试终端加入其目录：

```powershell
$env:Path = 'D:\build\msys2\ucrt64\bin;D:\build\msys2\mingw64\bin;D:\build\msys2\usr\bin;' + $env:Path
west build --no-sysbuild -d build/test_serial_rx -t run
```

看到 `pass = 5, fail = 0` 和 `PROJECT EXECUTION SUCCESSFUL` 表示测试完成。若 QEMU 未退出，使用 Ctrl+A、X 退出控制台。本次自动验证使用无窗口 QEMU，捕获完成结果后终止模拟器。

测试使用两个 zephyr,uart-emul 节点替代实际串口，执行的接收模块仍为 apps/bringup/src/serial_rx.c。没有连接或操作 GD32 实板。

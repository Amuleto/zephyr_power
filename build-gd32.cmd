@echo off
setlocal
set "PATH=D:\build\zephyr-venv312\Scripts;D:\build\cmake\cmake-4.4.4-windows-x86_64\bin;D:\build\ninja;D:\build\gperf;D:\build\dtc\usr\bin;%PATH%"
set "ZEPHYR_BASE=%~dp0..\gd32_zephyr-main"
set "ZEPHYR_SDK_INSTALL_DIR=D:\build\zephyr-sdk-1.0.1"
set "ZEPHYR_TOOLCHAIN_VARIANT=zephyr"
cd /d "%~dp0.."
west build -b gd32f303rc firmware/apps/bringup -d build/bringup_gd32f303rc -- -DBOARD_ROOT="%~dp0."
exit /b %ERRORLEVEL%

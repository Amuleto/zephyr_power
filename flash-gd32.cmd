@echo off
setlocal
set "PATH=D:\build\zephyr-venv312\Scripts;D:\build\cmake\cmake-4.4.4-windows-x86_64\bin;D:\build\ninja;%PATH%"
set "ZEPHYR_BASE=%~dp0..\gd32_zephyr-main"
cd /d "%~dp0.."
west flash -d build/bringup_gd32f303rc -r openocd --openocd D:/build/zephyr-sdk-1.0.1/hosttools/openocd/bin/openocd.exe
exit /b %ERRORLEVEL%

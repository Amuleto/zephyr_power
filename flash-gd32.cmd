@echo off
setlocal
call "%~dp0env.cmd"
cd /d "%~dp0.."
west flash -d build/bringup_gd32f303rc -r openocd --openocd "%ZEPHYR_SDK_INSTALL_DIR%/hosttools/openocd/bin/openocd.exe"
exit /b %ERRORLEVEL%

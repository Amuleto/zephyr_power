@echo off
rem Override ZEPHYR_TOOLS_ROOT, ZEPHYR_SDK_INSTALL_DIR or VIRTUAL_ENV as needed.
if not defined ZEPHYR_TOOLS_ROOT set "ZEPHYR_TOOLS_ROOT=D:\build"
if not defined ZEPHYR_SDK_INSTALL_DIR set "ZEPHYR_SDK_INSTALL_DIR=%ZEPHYR_TOOLS_ROOT%\zephyr-sdk-1.0.1"
if not defined VIRTUAL_ENV set "VIRTUAL_ENV=%ZEPHYR_TOOLS_ROOT%\zephyr-venv312"
set "PATH=%VIRTUAL_ENV%\Scripts;%ZEPHYR_TOOLS_ROOT%\cmake\cmake-4.4.4-windows-x86_64\bin;%ZEPHYR_TOOLS_ROOT%\ninja;%ZEPHYR_TOOLS_ROOT%\gperf;%ZEPHYR_TOOLS_ROOT%\dtc\usr\bin;%PATH%"
set "ZEPHYR_BASE=%~dp0..\gd32_zephyr-main"
if exist "%~dp0..\gd32_zephyr\CMakeLists.txt" set "ZEPHYR_BASE=%~dp0..\gd32_zephyr"
set "ZEPHYR_TOOLCHAIN_VARIANT=zephyr"
exit /b 0

@echo off
setlocal
call "%~dp0env.cmd"
cd /d "%~dp0.."
west build -b gd32f303rc "%~dp0apps\bringup" -d build/bringup_gd32f303rc -- -DBOARD_ROOT="%~dp0."
exit /b %ERRORLEVEL%

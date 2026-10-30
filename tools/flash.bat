@echo off
setlocal
title SmartBoot - flasher
cd /d "%~dp0"
echo.
echo   SmartBoot flasher for the M5Stack Cardputer ADV
echo   -----------------------------------------------------
echo   1. Plug the Cardputer ADV into this PC with a USB-C data cable.
echo   2. Turn it on.
echo.
echo   This replaces everything on the Cardputer: its firmware, installed apps and settings.
echo   Files on the SD card are not touched.
echo.
pause
set "IMAGE=@IMAGE@"
if not exist "%IMAGE%" (
  echo   Could not find %IMAGE% next to this file. Unzip the whole package first.
  goto fail
)
set "ESPTOOL="
if exist "%~dp0esptool.exe" set ESPTOOL="%~dp0esptool.exe"
if not defined ESPTOOL (
  where esptool >nul 2>nul && set "ESPTOOL=esptool"
)
if not defined ESPTOOL (
  echo   esptool.exe is missing. Download the SmartBoot Windows zip again and unzip all of it.
  goto fail
)
%ESPTOOL% --chip esp32s3 --baud 921600 write_flash 0x0 "%IMAGE%"
if errorlevel 1 goto retry
echo.
echo   Done! SmartBoot is starting on your Cardputer. Have fun!
echo.
pause
exit /b 0

:retry
echo.
echo   Flashing didn't work. Things to try:
echo    - Use a USB cable that carries data (some cables only charge).
echo    - Hold the G0 button while plugging the Cardputer in, then run this again.
echo    - Close programs that use the Cardputer's port (Arduino IDE, M5Burner, serial monitors).
:fail
echo.
pause
exit /b 1

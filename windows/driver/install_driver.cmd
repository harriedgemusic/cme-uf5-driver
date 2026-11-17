@echo off
setlocal enabledelayedexpansion
title CME UF5 - WinUSB Driver Installer

echo =====================================================================
echo  CME UF Series (UF5/UF6/UF7/UF8) WinUSB Driver Installer
echo  Compatible with Windows 10 and Windows 11 (64-bit / 32-bit / ARM64)
echo =====================================================================
echo.

:: Check for Administrator privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [!] Requesting administrative privileges...
    powershell -Command "Start-Process cmd -ArgumentList '/c \"\"%~dp0install_driver.cmd\"\"' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"

echo [*] Installing CME UF WinUSB driver via PnPUtil...
pnputil /add-driver "%~dp0cme_uf5.inf" /install
if %errorLevel% equ 0 (
    echo.
    echo [+] Driver installed successfully!
    echo [*] Connect your CME UF5 keyboard to a USB port.
    echo [*] Device Manager will now associate it with WinUSB automatically.
) else (
    echo.
    echo [!] PnPUtil returned code %errorLevel%.
    echo [*] If Windows prompted for driver installation approval, please verify.
)

echo.
pause


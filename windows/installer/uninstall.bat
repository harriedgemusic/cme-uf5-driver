@echo off
setlocal enabledelayedexpansion
title CME UF5 - Uninstaller

net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [*] Administrator privileges required. Requesting elevation...
    powershell -Command "Start-Process cmd -ArgumentList '/c \"\"%~dp0uninstall.bat\"\"' -Verb RunAs"
    exit /b
)

set "TARGET_DIR=%ProgramFiles%\CME UF5"

echo [*] Removing shortcuts...
del "%USERPROFILE%\Desktop\CME UF5 Control.lnk" 2>nul
del "%PUBLIC%\Desktop\CME UF5 Control.lnk" 2>nul
rd /S /Q "%ProgramData%\Microsoft\Windows\Start Menu\Programs\CME UF5" 2>nul

echo [*] Stopping running processes...
taskkill /F /IM CMEUF5Control.exe 2>nul
taskkill /F /IM CMEUF5Bridge.exe 2>nul

echo [*] Removing program files...
rd /S /Q "%TARGET_DIR%" 2>nul

echo [*] Removing registry configuration...
reg delete "HKCU\Software\CME-Pro\UF5" /f 2>nul

echo [+] CME UF5 software has been successfully removed from your system.
echo.
pause


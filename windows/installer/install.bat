@echo off
setlocal enabledelayedexpansion
title CME UF5 - Windows 10/11 Setup

echo =====================================================================
echo   CME UF Series USB MIDI Master Keyboards (UF5/UF6/UF7/UF8)
echo   Complete Driver and Control Center Installer for Windows 10/11
echo =====================================================================
echo.

:: Check for Administrator elevation
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [*] Administrator privileges required. Requesting elevation...
    powershell -Command "Start-Process cmd -ArgumentList '/c \"\"%~dp0install.bat\"\"' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"
set "BIN_DIR=%~dp0..\bin"
set "DRIVER_DIR=%~dp0..\driver"
set "TARGET_DIR=%ProgramFiles%\CME UF5"

echo [*] Creating application directory: "%TARGET_DIR%"
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"

echo [*] Installing WinUSB driver for CME UF Series (VID 0x7104, PID 0x2202)...
pnputil /add-driver "%DRIVER_DIR%\cme_uf5.inf" /install
if %errorLevel% neq 0 (
    echo [!] Warning: pnputil reported code %errorLevel%. Driver may already be installed.
) else (
    echo [+] WinUSB driver installed successfully.
)

echo [*] Copying application files...
if exist "%BIN_DIR%\CMEUF5Control.exe" (
    copy /Y "%BIN_DIR%\CMEUF5Control.exe" "%TARGET_DIR%\" >nul
    echo [+] Copied CMEUF5Control.exe
)
if exist "%BIN_DIR%\CMEUF5Bridge.exe" (
    copy /Y "%BIN_DIR%\CMEUF5Bridge.exe" "%TARGET_DIR%\" >nul
    echo [+] Copied CMEUF5Bridge.exe
)
if exist "%BIN_DIR%\cme-uf5-ctl.exe" (
    copy /Y "%BIN_DIR%\cme-uf5-ctl.exe" "%TARGET_DIR%\" >nul
    echo [+] Copied cme-uf5-ctl.exe
    setx PATH "%PATH%;%TARGET_DIR%" /M >nul 2>&1
)

echo [*] Creating Desktop and Start Menu shortcuts...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ws = New-Object -ComObject WScript.Shell; " ^
    "$s = $ws.CreateShortcut([System.IO.Path]::Combine([Environment]::GetFolderPath('Desktop'), 'CME UF5 Control.lnk')); " ^
    "$s.TargetPath = '%TARGET_DIR%\CMEUF5Control.exe'; " ^
    "$s.WorkingDirectory = '%TARGET_DIR%'; " ^
    "$s.Save(); " ^
    "$sm = [System.IO.Path]::Combine([Environment]::GetFolderPath('CommonPrograms'), 'CME UF5'); " ^
    "if (-not (Test-Path $sm)) { New-Item -ItemType Directory -Path $sm | Out-Null }; " ^
    "$s2 = $ws.CreateShortcut([System.IO.Path]::Combine($sm, 'CME UF5 Control Center.lnk')); " ^
    "$s2.TargetPath = '%TARGET_DIR%\CMEUF5Control.exe'; " ^
    "$s2.WorkingDirectory = '%TARGET_DIR%'; " ^
    "$s2.Save();"

echo.
echo =====================================================================
echo [+] INSTALLATION COMPLETE!
echo.
echo 1. Connect your CME UF5 keyboard to any USB port.
echo 2. Launch 'CME UF5 Control' from your Desktop or Start Menu.
echo 3. The virtual MIDI port 'CME UF5' will be active in all DAWs!
echo =====================================================================
echo.
pause


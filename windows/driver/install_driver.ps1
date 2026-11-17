# ==============================================================================
# CME UF Series WinUSB Driver PowerShell Installer
# Compatible with Windows 10 & 11
# ==============================================================================

# Ensure Admin privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[*] Elevation required. Relaunching as Administrator..." -ForegroundColor Yellow
    Start-Process powershell.exe -ArgumentList ("-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"") -Verb RunAs
    exit
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$InfPath = Join-Path $ScriptDir "cme_uf5.inf"

Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host " CME UF5 / UF6 / UF7 / UF8 WinUSB Driver Installer" -ForegroundColor Cyan
Write-Host " Target OS: Windows 10 / Windows 11" -ForegroundColor Cyan
Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $InfPath)) {
    Write-Host "[-] ERROR: cme_uf5.inf not found at $InfPath" -ForegroundColor Red
    Pause
    exit 1
}

Write-Host "[*] Adding and installing driver package: $InfPath" -ForegroundColor Green
$proc = Start-Process -FilePath "pnputil.exe" -ArgumentList "/add-driver `"$InfPath`" /install" -Wait -PassThru -NoNewWindow

if ($proc.ExitCode -eq 0) {
    Write-Host ""
    Write-Host "[+] Driver successfully registered with Windows PnP!" -ForegroundColor Green
    Write-Host "[*] You can now plug in your CME UF5 keyboard." -ForegroundColor White
} else {
    Write-Host ""
    Write-Host "[!] pnputil exited with code $($proc.ExitCode)." -ForegroundColor Yellow
    Write-Host "    If Windows warns about unsigned driver, you may enable Test Signing or install via Device Manager." -ForegroundColor Gray
}

Write-Host ""
Write-Host "Press any key to close..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")


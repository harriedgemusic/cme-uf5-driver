# ==============================================================================
# CME UF Series Windows 10/11 PowerShell Installer
# ==============================================================================

param (
    [switch]$Unattended = $false
)

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[*] Elevation required. Relaunching as Administrator..." -ForegroundColor Yellow
    Start-Process powershell.exe -ArgumentList ("-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"") -Verb RunAs
    exit
}

$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$BaseDir     = Split-Path -Parent $ScriptDir
$BinDir      = Join-Path $BaseDir "bin"
$DriverDir   = Join-Path $BaseDir "driver"
$TargetDir   = Join-Path $env:ProgramFiles "CME UF5"

Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host " CME UF Series Driver & Control Center Windows Installer" -ForegroundColor Cyan
Write-Host " Compatible with Windows 10 / Windows 11 (x64 / ARM64 / x86)" -ForegroundColor Cyan
Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host ""

# 1. Create target folder
if (-not (Test-Path $TargetDir)) {
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
    Write-Host "[+] Created application directory: $TargetDir" -ForegroundColor Green
}

# 2. Install WinUSB driver
$InfPath = Join-Path $DriverDir "cme_uf5.inf"
if (Test-Path $InfPath) {
    Write-Host "[*] Installing WinUSB driver via PnPUtil..." -ForegroundColor Yellow
    $pnp = Start-Process pnputil.exe -ArgumentList "/add-driver `"$InfPath`" /install" -Wait -PassThru -NoNewWindow
    if ($pnp.ExitCode -eq 0) {
        Write-Host "[+] WinUSB driver installed successfully." -ForegroundColor Green
    } else {
        Write-Host "[!] Note: PnPUtil returned code $($pnp.ExitCode)." -ForegroundColor Gray
    }
}

# 3. Copy binaries
$binaries = @("CMEUF5Control.exe", "CMEUF5Bridge.exe", "cme-uf5-ctl.exe")
foreach ($bin in $binaries) {
    $src = Join-Path $BinDir $bin
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $TargetDir -Force
        Write-Host "[+] Installed $bin -> $TargetDir" -ForegroundColor Green
    }
}

# 4. Add to PATH for cme-uf5-ctl
$currentPath = [Environment]::GetEnvironmentVariable("PATH", "Machine")
if ($currentPath -notlike "*$TargetDir*") {
    [Environment]::SetEnvironmentVariable("PATH", "$currentPath;$TargetDir", "Machine")
    Write-Host "[+] Added $TargetDir to system PATH" -ForegroundColor Green
}

# 5. Create Desktop and Start Menu shortcuts
$wsh = New-Object -ComObject WScript.Shell

$desktopLnk = Join-Path ([Environment]::GetFolderPath('Desktop')) "CME UF5 Control.lnk"
$sc = $wsh.CreateShortcut($desktopLnk)
$sc.TargetPath = Join-Path $TargetDir "CMEUF5Control.exe"
$sc.WorkingDirectory = $TargetDir
$sc.Description = "CME UF5 Master Keyboard Control Center"
$sc.Save()

$startMenuDir = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) "CME UF5"
if (-not (Test-Path $startMenuDir)) { New-Item -ItemType Directory -Path $startMenuDir | Out-Null }
$startMenuLnk = Join-Path $startMenuDir "CME UF5 Control Center.lnk"
$sc2 = $wsh.CreateShortcut($startMenuLnk)
$sc2.TargetPath = Join-Path $TargetDir "CMEUF5Control.exe"
$sc2.WorkingDirectory = $TargetDir
$sc2.Description = "CME UF5 Master Keyboard Control Center"
$sc2.Save()

Write-Host ""
Write-Host "=====================================================================" -ForegroundColor Green
Write-Host " [+] INSTALLATION COMPLETE!" -ForegroundColor Green
Write-Host " Connect your CME UF5 keyboard and launch 'CME UF5 Control'!" -ForegroundColor White
Write-Host "=====================================================================" -ForegroundColor Green
Write-Host ""

if (-not $Unattended) {
    Write-Host "Press any key to finish..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
}


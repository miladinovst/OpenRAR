<#
.SYNOPSIS
    Per-user Windows installer for OpenRaR (no administrator rights required).
.DESCRIPTION
    Installs OpenRaR-GUI.exe, openrar.exe, openrar_sfx_stub.exe, and icon assets
    into %LOCALAPPDATA%\OpenRaR, adds it to the user PATH, creates Start Menu and
    Desktop shortcuts, registers the cascading Windows Explorer right-click menu,
    and registers OpenRaR in Windows Installed Apps for clean uninstallation.
.EXAMPLE
    .\install.ps1
    .\install.ps1 -InstallDir "C:\Tools\OpenRaR" -NoDesktop
#>
param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "OpenRaR"),
    [switch]$NoDesktop,
    [switch]$NoPath,
    [switch]$NoContextMenu
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot

function Find-SourceFile([string[]]$Candidates) {
    foreach ($c in $Candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..") -ErrorAction SilentlyContinue
$BuildDir = if ($RepoRoot) { Join-Path $RepoRoot "build" } else { $ScriptDir }

$GuiExe  = Find-SourceFile @((Join-Path $ScriptDir "OpenRaR-GUI.exe"), (Join-Path $BuildDir "OpenRaR-GUI.exe"))
$CliExe  = Find-SourceFile @((Join-Path $ScriptDir "openrar.exe"),     (Join-Path $BuildDir "openrar.exe"))
$SfxExe  = Find-SourceFile @((Join-Path $ScriptDir "openrar_sfx_stub.exe"), (Join-Path $BuildDir "openrar_sfx_stub.exe"))
$IconSrc = Find-SourceFile @((Join-Path $ScriptDir "openrar.ico"),     (if ($RepoRoot) { Join-Path $RepoRoot "assets\openrar.ico" } else { "" }))
$Readme  = Find-SourceFile @((Join-Path $ScriptDir "README.md"),       (if ($RepoRoot) { Join-Path $RepoRoot "README.md" } else { "" }))
$License = Find-SourceFile @((Join-Path $ScriptDir "LICENSE"),         (if ($RepoRoot) { Join-Path $RepoRoot "LICENSE" } else { "" }))
$Uninst  = Find-SourceFile @((Join-Path $ScriptDir "uninstall.ps1"))

if (-not $GuiExe -or -not $CliExe) {
    throw "OpenRaR binaries not found. Run .\build.ps1 first or run install.ps1 from an extracted release archive."
}

Write-Host "Installing OpenRaR to $InstallDir ..." -ForegroundColor Cyan

# Stop any running GUI instance before overwriting binaries
Stop-Process -Name "OpenRaR-GUI" -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 150

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item -Force $GuiExe (Join-Path $InstallDir "OpenRaR-GUI.exe")
Copy-Item -Force $CliExe (Join-Path $InstallDir "openrar.exe")
if ($SfxExe)  { Copy-Item -Force $SfxExe  (Join-Path $InstallDir "openrar_sfx_stub.exe") }
if ($IconSrc) { Copy-Item -Force $IconSrc (Join-Path $InstallDir "openrar.ico") }
if ($Readme)  { Copy-Item -Force $Readme  (Join-Path $InstallDir "README.md") }
if ($License) { Copy-Item -Force $License (Join-Path $InstallDir "LICENSE") }
if ($Uninst)  { Copy-Item -Force $Uninst  (Join-Path $InstallDir "uninstall.ps1") }

$InstalledGui  = Join-Path $InstallDir "OpenRaR-GUI.exe"
$InstalledCli  = Join-Path $InstallDir "openrar.exe"
$InstalledIcon = if (Test-Path (Join-Path $InstallDir "openrar.ico")) { Join-Path $InstallDir "openrar.ico" } else { "$InstalledGui,0" }

# 1. Add to user PATH
if (-not $NoPath) {
    $curPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $parts = @()
    if ($curPath) { $parts = $curPath.Split(';') | Where-Object { $_ -ne "" } }
    if ($parts -notcontains $InstallDir) {
        $newPath = (($parts + $InstallDir) -join ';')
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        Write-Host "  [+] Added $InstallDir to user PATH" -ForegroundColor Green
    }
}

# 2. Create Start Menu & optional Desktop shortcuts
$WshShell = New-Object -ComObject WScript.Shell
$StartMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\OpenRaR"
New-Item -ItemType Directory -Force -Path $StartMenuDir | Out-Null

$smLink = $WshShell.CreateShortcut((Join-Path $StartMenuDir "OpenRaR.lnk"))
$smLink.TargetPath = $InstalledGui
$smLink.WorkingDirectory = $InstallDir
$smLink.IconLocation = $InstalledIcon
$smLink.Description = "OpenRaR Open-Source Archiver"
$smLink.Save()
Write-Host "  [+] Created Start Menu shortcut" -ForegroundColor Green

if (-not $NoDesktop) {
    $DesktopDir = [Environment]::GetFolderPath("Desktop")
    $dtLink = $WshShell.CreateShortcut((Join-Path $DesktopDir "OpenRaR.lnk"))
    $dtLink.TargetPath = $InstalledGui
    $dtLink.WorkingDirectory = $InstallDir
    $dtLink.IconLocation = $InstalledIcon
    $dtLink.Description = "OpenRaR Open-Source Archiver"
    $dtLink.Save()
    Write-Host "  [+] Created Desktop shortcut" -ForegroundColor Green
}

# 3. Register cascading Windows Explorer right-click menu
if (-not $NoContextMenu) {
    & $InstalledCli install-menu
    Write-Host "  [+] Registered Windows Explorer right-click menu" -ForegroundColor Green
}

# 4. Register Uninstall entry in Windows Installed Apps (HKCU)
$UninstKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenRaR"
New-Item -Path $UninstKey -Force | Out-Null
$UninstScript = Join-Path $InstallDir "uninstall.ps1"
Set-ItemProperty -Path $UninstKey -Name "DisplayName"     -Value "OpenRaR"
Set-ItemProperty -Path $UninstKey -Name "DisplayVersion"  -Value "1.0.0"
Set-ItemProperty -Path $UninstKey -Name "Publisher"       -Value "OpenRaR Contributors"
Set-ItemProperty -Path $UninstKey -Name "DisplayIcon"     -Value $InstalledIcon
Set-ItemProperty -Path $UninstKey -Name "InstallLocation" -Value $InstallDir
Set-ItemProperty -Path $UninstKey -Name "NoModify"        -Value 1 -Type DWord
Set-ItemProperty -Path $UninstKey -Name "NoRepair"        -Value 1 -Type DWord
Set-ItemProperty -Path $UninstKey -Name "UninstallString" -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$UninstScript`" -InstallDir `"$InstallDir`""

Write-Host ""
Write-Host "OpenRaR installed successfully!" -ForegroundColor Green
Write-Host "  GUI: $InstalledGui"
Write-Host "  CLI: $InstalledCli (open a new terminal to use 'openrar')"

<#
.SYNOPSIS
    Cleanly uninstalls OpenRaR from the current user profile.
#>
param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "OpenRaR")
)

$ErrorActionPreference = "SilentlyContinue"

Write-Host "Uninstalling OpenRaR from $InstallDir ..." -ForegroundColor Cyan

Stop-Process -Name "OpenRaR-GUI" -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 150

$CliExe = Join-Path $InstallDir "openrar.exe"
if (Test-Path $CliExe) {
    & $CliExe uninstall-menu | Out-Null
    Write-Host "  [-] Removed Windows Explorer right-click menu" -ForegroundColor Yellow
}

# Remove shortcuts
$StartMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\OpenRaR"
if (Test-Path $StartMenuDir) { Remove-Item -Recurse -Force $StartMenuDir }

$DesktopLink = Join-Path ([Environment]::GetFolderPath("Desktop")) "OpenRaR.lnk"
if (Test-Path $DesktopLink) { Remove-Item -Force $DesktopLink }

# Remove from user PATH
$curPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($curPath) {
    $filtered = $curPath.Split(';') | Where-Object { $_ -ne "" -and $_.TrimEnd('\') -ne $InstallDir.TrimEnd('\') }
    [Environment]::SetEnvironmentVariable("Path", ($filtered -join ';'), "User")
}

# Remove Installed Apps registry key
$UninstKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenRaR"
if (Test-Path $UninstKey) { Remove-Item -Recurse -Force $UninstKey }

# Remove install folder (skip self-lock warning if run from inside InstallDir)
if (Test-Path $InstallDir) {
    Get-ChildItem -Path $InstallDir -Force | ForEach-Object {
        Remove-Item -Recurse -Force $_.FullName -ErrorAction SilentlyContinue
    }
    Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue
}

Write-Host "OpenRaR uninstalled." -ForegroundColor Green

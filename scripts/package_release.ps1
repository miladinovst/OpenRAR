<#
.SYNOPSIS
    Builds OpenRAR, runs all automated tests, and packages Windows release artifacts.
.DESCRIPTION
    Outputs into dist/:
      - OpenRAR-<Version>-win64.zip        (portable + install.ps1 / uninstall.ps1)
      - OpenRAR-Setup-<Version>-win64.exe  (built automatically if Inno Setup 6 ISCC.exe is found)
      - SHA256SUMS.txt
.EXAMPLE
    .\scripts\package_release.ps1
    .\scripts\package_release.ps1 -Version "1.0.0" -SkipBuild
#>
param(
    [string]$Version = "1.0.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $RepoRoot "build"
$DistDir  = Join-Path $RepoRoot "dist"
$StageDir = Join-Path $DistDir "OpenRAR-$Version-win64"

if (-not $SkipBuild) {
    Write-Host "==> Building and testing OpenRAR..." -ForegroundColor Cyan
    & (Join-Path $RepoRoot "build.ps1") -Test
    if ($LASTEXITCODE -ne 0) { throw "Build or tests failed." }
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

# Stage portable + installer bundle
$FilesToCopy = @(
    @{ Src = Join-Path $BuildDir "OpenRAR-GUI.exe";          Dst = "OpenRAR-GUI.exe" },
    @{ Src = Join-Path $BuildDir "openrar.exe";              Dst = "openrar.exe" },
    @{ Src = Join-Path $BuildDir "openrar_sfx_stub.exe";     Dst = "openrar_sfx_stub.exe" },
    @{ Src = Join-Path $RepoRoot "assets\openrar.ico";       Dst = "openrar.ico" },
    @{ Src = Join-Path $RepoRoot "assets\openrar.svg";       Dst = "openrar.svg" },
    @{ Src = Join-Path $RepoRoot "installer\windows\install.ps1";   Dst = "install.ps1" },
    @{ Src = Join-Path $RepoRoot "installer\windows\uninstall.ps1"; Dst = "uninstall.ps1" },
    @{ Src = Join-Path $RepoRoot "README.md";                Dst = "README.md" },
    @{ Src = Join-Path $RepoRoot "LICENSE";                  Dst = "LICENSE" }
)

foreach ($f in $FilesToCopy) {
    if (-not (Test-Path $f.Src)) { throw "Missing release file: $($f.Src)" }
    Copy-Item -Force $f.Src (Join-Path $StageDir $f.Dst)
}

$ZipPath = Join-Path $DistDir "OpenRAR-$Version-win64.zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipPath -CompressionLevel Optimal
Write-Host "  [+] Created $ZipPath" -ForegroundColor Green

# Build Inno Setup installer if ISCC.exe is installed
$IsccCandidates = @(
    "iscc.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
)
$Iscc = $null
foreach ($c in $IsccCandidates) {
    $cmd = Get-Command $c -ErrorAction SilentlyContinue
    if ($cmd) { $Iscc = $cmd.Source; break }
    if (Test-Path $c) { $Iscc = $c; break }
}

if ($Iscc) {
    Write-Host "==> Building Windows Setup EXE with Inno Setup..." -ForegroundColor Cyan
    & $Iscc "/DAppVersion=$Version" (Join-Path $RepoRoot "installer\windows\openrar.iss")
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }
    Write-Host "  [+] Created OpenRAR-Setup-$Version-win64.exe" -ForegroundColor Green
} else {
    Write-Host "  [i] Inno Setup 6 (ISCC.exe) not found; skipped .exe installer (portable ZIP + install.ps1 ready)." -ForegroundColor Yellow
}

# Clean up staging folder and write SHA256SUMS.txt
Remove-Item -Recurse -Force $StageDir
$SumFile = Join-Path $DistDir "SHA256SUMS.txt"
Get-ChildItem -Path $DistDir -File | Where-Object { $_.Name -ne "SHA256SUMS.txt" } | ForEach-Object {
    $h = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower()
    "$h  $($_.Name)"
} | Set-Content -Path $SumFile -Encoding ASCII

Write-Host ""
Write-Host "Release packaging complete! Artifacts in $DistDir :" -ForegroundColor Green
Get-ChildItem -Path $DistDir -File | Format-Table Name, Length -AutoSize

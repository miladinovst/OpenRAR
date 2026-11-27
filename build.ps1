# One-command Windows build script for OpenRaR (C++20)
param(
    [switch]$RunGui,
    [switch]$Test
)

$ErrorActionPreference = "Stop"

$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = (& $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
}
if (-not $vsPath) { throw "Visual Studio C++ Build Tools not found." }

$msvcVer = (Get-ChildItem "$vsPath\VC\Tools\MSVC" | Sort-Object Name -Descending | Select-Object -First 1).Name
$msvc = "$vsPath\VC\Tools\MSVC\$msvcVer"
$sdk = "C:\Program Files (x86)\Windows Kits\10"
$sdkVer = (Get-ChildItem "$sdk\Include" | Sort-Object Name -Descending | Select-Object -First 1).Name

$env:PATH = "$msvc\bin\Hostx64\x64;$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$sdk\bin\$sdkVer\x64;$env:PATH"
$env:INCLUDE = "$msvc\include;$sdk\Include\$sdkVer\ucrt;$sdk\Include\$sdkVer\um;$sdk\Include\$sdkVer\shared;$sdk\Include\$sdkVer\winrt"
$env:LIB = "$msvc\lib\x64;$sdk\Lib\$sdkVer\ucrt\x64;$sdk\Lib\$sdkVer\um\x64"

Stop-Process -Name "OpenRaR-GUI" -Force -ErrorAction SilentlyContinue
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

if ($Test) {
    & .\build\openrar_tests.exe
}
if ($RunGui) {
    Start-Process ".\build\OpenRaR-GUI.exe"
}

# Configure, build and test on Windows with the Qt 6.12.0 MinGW kit from tools/fetch_qt_windows.py.
# Usage: powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1 [-Config Release] [-QtRoot C:\Users\you\Qt] [-NoTests]
param(
    [string]$Config = "Release",
    [string]$QtRoot = "$env:USERPROFILE\Qt",
    [string]$BuildDir = "",
    [int]$Parallel = 3,
    [switch]$NoTests
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$qtPrefix = Join-Path $QtRoot "6.12.0\mingw_64"
$mingw = Join-Path $QtRoot "Tools\mingw1510_64\bin"
$pyScripts = python -c "import sysconfig; print(sysconfig.get_path('scripts', 'nt_user'))"
$env:PATH = "$mingw;$qtPrefix\bin;$pyScripts;$env:PATH"
$build = Join-Path $repo "build\windows-mingw-$($Config.ToLower())"
if ($BuildDir) { $build = Join-Path $repo $BuildDir }

cmake -S $repo -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$qtPrefix"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --parallel $Parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if (-not $NoTests) {
    ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
Write-Host "Built: $build\drawapp.exe"

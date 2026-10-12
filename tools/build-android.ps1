# Build an arm64-v8a APK with Qt 6.12.0 for Android (from: python tools/fetch_qt_windows.py --android).
# Requires: JDK 21 (Android Studio's bundled JBR works), Android SDK with platforms;android-36 and
# ndk;27.2.12479018, and the Windows host Qt (tools/fetch_qt_windows.py) for QT_HOST_PATH.
# Usage: powershell -ExecutionPolicy Bypass -File tools\build-android.ps1 [-Config Release]
param(
    [string]$Config = "Release",
    [string]$QtRoot = "$env:USERPROFILE\Qt",
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$Jdk = "C:\Program Files\Android\Android Studio\jbr",
    # androiddeployqt 6.12.0 fails with "Import found outside of import paths" when the build
    # directory contains a space, so the default is outside the repository.
    [string]$BuildDir = "$env:USERPROFILE\drawapp-build\android-arm64-$($Config.ToLower())"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$qtAndroid = Join-Path $QtRoot "6.12.0\android_arm64_v8a"
$qtHost = Join-Path $QtRoot "6.12.0\mingw_64"
$ndk = Join-Path $Sdk "ndk\27.2.12479018"
$pyScripts = python -c "import sysconfig; print(sysconfig.get_path('scripts', 'nt_user'))"
$env:JAVA_HOME = $Jdk
$env:PATH = "$Jdk\bin;$qtHost\bin;$pyScripts;$env:PATH"
$build = $BuildDir
if ($build.Contains(" ")) { throw "Android build directory must not contain spaces: $build" }

cmake -S $repo -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCMAKE_TOOLCHAIN_FILE=$qtAndroid\lib\cmake\Qt6\qt.toolchain.cmake" `
    "-DQT_HOST_PATH=$qtHost" "-DANDROID_SDK_ROOT=$Sdk" "-DANDROID_NDK_ROOT=$ndk"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --target apk --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Get-ChildItem -Recurse -Filter *.apk (Join-Path $build "android-build") | ForEach-Object { "APK: $($_.FullName)" }

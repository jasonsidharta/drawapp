# Build

## Versions (chosen and recorded on 2026-10-11)

| Component | Version | Source |
|---|---|---|
| Qt | 6.12.0 (latest release per doc.qt.io "Qt 6 releases"; 6.8 is the current LTS) | download.qt.io online repository, official binaries |
| Windows compiler | MinGW-w64 GCC 15.1.0, posix-seh-msvcrt rt_v12 rev0 | github.com/niXman/mingw-builds-binaries, SHA-256 pinned in `tools/fetch_qt_windows.py` |
| CMake / Ninja | 4.4.4 / 1.13.2 (from `pip install cmake ninja`) | PyPI |
| Android NDK | r27c, 27.2.12479018 | Android SDK manager |
| Android SDK | platform `android-36`, build-tools 36.0.0 | Android SDK manager |
| JDK | 21 (Android Studio's bundled JBR, 21.0.10) | Android Studio |

Qt 6.12 official support (doc.qt.io/qt-6/supported-platforms.html): Windows 10 1809+/11 with
MSVC 2022 or MinGW-w64 15.1; Linux x86_64/arm64 (for example Ubuntu 22.04/24.04, RHEL 9.6/10);
Android API 28–36 with NDK r27c and JDK 21; iOS 18+ built with Xcode 16+; macOS 14.4+.
Google Play requires new apps and updates to target API 36 from 31 Aug 2026.

**Toolchain mismatch found:** the Qt repository's `qt.qt6.6120.win64_mingw` package depends on
the MinGW **13.1** tool and bundles 13.1 runtime DLLs, but `config_qtbase.summary` says
"Compiler: gcc 15.1.0". We use GCC 15.1.0 and replace the runtime DLLs in Qt's `bin` folder.

## Windows (verified)

Prerequisites: Python 3.10+, Git, internet access, about 2 GB of disk.

```powershell
python -m pip install --user aqtinstall cmake ninja   # aqtinstall only for its py7zr dependency
python tools\fetch_qt_windows.py                      # Qt 6.12.0 + MinGW 15.1 into %USERPROFILE%\Qt
powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1     # configure, build, ctest
```

`aqtinstall` 3.3.0 cannot install Qt 6.12.0 (it looks for checksums at an old repository path),
so `tools/fetch_qt_windows.py` reads Qt's `Updates.xml` itself and checks the SHA-256 and SHA-1
values Qt publishes.

The build lands in `build\windows-mingw-release\`. To make a standalone folder:

```powershell
$env:PATH = "$env:USERPROFILE\Qt\Tools\mingw1510_64\bin;$env:USERPROFILE\Qt\6.12.0\mingw_64\bin;$env:PATH"
cmake --install build\windows-mingw-release --prefix dist\windows-mingw
dist\windows-mingw\bin\drawapp.exe
```

`ctest` runs the window tests (`tst_canvas`, `tst_ui`) on Qt's offscreen platform with
`QT_QPA_FONTDIR=C:/Windows/Fonts`, so they do not open windows on the desktop. Run the test
executables directly (without `QT_QPA_PLATFORM`) to watch them in real windows.

Environment variables for testing and measurement:

| Variable | Effect |
|---|---|
| `DRAWAPP_STATS=1` | Logs paint/input timings and UI loop lag every 2 s, plus `[recovery]` tick/snapshot/commit lines (add `QT_FORCE_STDERR_LOGGING=1` to see them, `QT_MESSAGE_PATTERN="%{time hh:mm:ss.zzz} %{message}"` for timestamps) |
| `DRAWAPP_AUTOSAVE_MS=<ms>` | Recovery autosave interval (default 10000, minimum 50) |
| `DRAWAPP_RECOVERY_DIR=<folder>` | Recovery folder instead of `%APPDATA%\DrawApp\recovery` |

The deploy step warns that translations are missing because `qttranslations` is not downloaded.
This is harmless for now.

## Manual check on Windows

Run `dist\windows-mingw\bin\drawapp.exe` (after the install step above), then:

1. Draw a stroke; the title shows `*Untitled`. Ctrl+Z removes it, Ctrl+Y (or Ctrl+Shift+Z) restores it.
2. Erase part of it, press Clear, then undo three times: each step comes back in order.
3. Ctrl+N with unsaved changes asks Save / Discard / Cancel. Cancel or Escape keeps the drawing.
4. Ctrl+S on an untitled drawing opens Save As; after saving, the title loses its `*`.
5. Ctrl+O reopens the file. Opening a renamed `.png` or a truncated `.drawapp` shows an error and
   keeps the current drawing.
6. Ctrl+Shift+E exports a PNG; the title keeps its `*` if there were unsaved changes.
7. Closing the window with unsaved changes asks first.
8. Layers: see "Testing layers" below.
9. Crash recovery: draw, wait more than 10 s, then end the process
   (`taskkill /F /IM drawapp.exe`). Start the app again: it offers Restore / Discard. Restore brings
   back the drawing as of the last autosave, with `*` in the title. Strokes made in the last
   seconds before the kill may be missing (see BENCHMARKS.md). Escape keeps the recovery data for
   the next start; Discard deletes it.

## Testing layers (manual, Windows)

1. Start the app. The panel on the right shows "Layers (1/42)" with "Layer 1".
2. Rename it to "Background" (Rename button or double-click the row, type, Enter). Pick a light
   color, size 256, and paint over most of the canvas.
3. Add a layer and rename it "Sketch". Draw a rough outline in blue at a small size, then drag the
   opacity slider to about 40 %. One Ctrl+Z puts the opacity back to 100 % in one step.
4. Add "Color" and paint some areas. Add "Line art" and draw outlines in black.
5. Select "Sketch", press Up until it is the top layer, then untick its check box (hidden).
   Try painting while "Sketch" is selected: nothing is drawn and a banner explains why.
6. Press Lock on "Line art", select it and try to paint: refused, with an explanation.
7. Select "Color", switch to the Eraser and erase part of it: the background shows through and
   the line art above is untouched.
8. Duplicate and Delete a layer, then undo both; undo and redo the steps above and check that the
   layer list and the canvas follow.
9. Save (Ctrl+S), New (Ctrl+N) and Open the file: same layers, order, names, visibility, opacity,
   locks and active layer. Export PNG (Ctrl+Shift+E): only visible layers, with transparency
   where nothing is painted.
10. End the process from Task Manager more than 10 s after a change and start the app again:
    Restore brings back all layers.

## Android (APK builds; not launched)

Additional prerequisites: the Android SDK at `%LOCALAPPDATA%\Android\Sdk` (Android Studio), with
`cmdline-tools`, `platforms;android-36` and `ndk;27.2.12479018`. The JDK is Android Studio's JBR.

```powershell
python tools\fetch_qt_windows.py --android    # Qt 6.12.0 for Android arm64-v8a
& "$env:LOCALAPPDATA\Android\Sdk\cmdline-tools\latest\bin\sdkmanager.bat" "ndk;27.2.12479018" "platforms;android-36"
powershell -ExecutionPolicy Bypass -File tools\build-android.ps1
```

Release output: `%USERPROFILE%\drawapp-build\android-arm64-release\android-build\drawapp.apk`
(unsigned, arm64-v8a, minSdk 28, targetSdk 36).

Debug APK for local device testing, signed with the Android SDK's development key
(`%USERPROFILE%\.android\debug.keystore`, created by Gradle; certificate "CN=Android Debug"):

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-android.ps1 -Config Debug
# -> %USERPROFILE%\drawapp-build\android-arm64-debug\android-build\build\outputs\apk\debug\android-build-debug.apk (66.9 MB)
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" install -r <that apk>
```

The debug key is for development only. Google Play needs an AAB signed with an upload key;
none is set up and no production credentials are stored in this repository.

The Android build directory **must not contain spaces**. `androiddeployqt` 6.12.0 fails with
"Import found outside of import paths" when it does, so the script builds outside this
repository by default.

## Linux (blocked here)

In WSL Ubuntu 24.04 there is no compiler or CMake, and installing packages needs the user's sudo
password. Ubuntu 24.04's packaged Qt (6.4) is older than this project's minimum (6.8), so Qt
6.12 must come from Qt's installer or repository. Expected steps (unverified):

```bash
sudo apt install build-essential cmake ninja-build python3-pip libgl-dev libegl-dev \
  libxkbcommon-dev libxkbcommon-x11-0 libfontconfig1-dev libxcb-cursor0 libxcb-icccm4 \
  libxcb-keysyms1 libxcb-shape0 libxcb-xinerama0 libxcb-randr0 libxcb-image0 libvulkan-dev
# Qt 6.12.0 linux_gcc_64 from the Qt online installer, then:
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/gcc_64
cmake --build build/linux && ctest --test-dir build/linux --output-on-failure
```

## iOS / iPadOS (blocked)

Building needs macOS with Xcode 16+ and Qt 6.12 for iOS. None is available here. The CMake
file sets bundle properties, but no iOS configure has ever run. Resolve the Qt LGPL/static
linking question (ARCHITECTURE.md) before shipping.

## Platform verification status

| Platform | Status | Evidence |
|---|---|---|
| Windows 11 x64 (25H2, Intel Core Ultra 7 155H, 200 % scaling) | **Built and launched** | `ctest` 5/5 suites (22 + 60 + 27 + 44 + 4 tests). Milestones 0–2 were driven on the desktop with real input (drawing, prompts, native dialogs, 9 forced kills). Milestone 3's layer panel was driven with synthetic input through the real Main.qml on the offscreen platform (`tst_ui`, screenshots inspected); an on-screen session for layers is still pending |
| Android arm64 | **Built but not launched** | Release (unsigned) and debug (signed with the development key, `apksigner` verified) APKs rebuilt after milestone 3; no device or emulator image available. Drawing, file dialogs, `content://` saving and recovery are unverified |
| Linux | **Blocked** | WSL needs a sudo password to install the compiler and libraries |
| iOS / iPadOS | **Blocked** | No macOS or Xcode |

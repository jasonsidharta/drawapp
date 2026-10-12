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
| `DRAWAPP_BRUSH_FILE=<file>` | Brush library file instead of `%APPDATA%\DrawApp\brushes.json` (custom presets, saved changes, favourites) |

Brush tools (not part of ctest):

```powershell
build\windows-mingw-release\brush_sheet.exe brush-sheet.png     # every preset on the standard strokes
build\windows-mingw-release\bench_core.exe 30 presets            # per-event preset timings, 1/5/10 layers
build\windows-mingw-release\bench_core.exe 30 selection          # selections and transforms incl. full canvas
build\windows-mingw-release\bench_canvas.exe 1 0 builtin.textured-paint   # real canvas with autosave, one preset
```

Run `brush_sheet` with `QT_QPA_PLATFORM=offscreen` and `QT_QPA_FONTDIR=C:/Windows/Fonts`. Inspect
the result after any change to the engine, tips or presets: gaps, stamping, jagged edges,
darkening at overlaps, seams, halos on the checkerboard.

The deploy step warns that translations are missing because `qttranslations` is not downloaded.
This is harmless for now.

## Outstanding Windows desktop check (needs an agreed idle period)

Everything below is covered by automated tests on the offscreen platform (`tst_canvas`,
`tst_ui`). What is still missing is one session with real mouse input, a real GPU surface and,
if one is available, a real pen. Run `dist\windows-mingw\bin\drawapp.exe` after the install step
above. A "heavy" drawing means 10 layers, each painted over most of the canvas with a large brush,
so that a save takes about 1 s.

1. **Layers:** add, rename, reorder, hide, lock, duplicate and delete layers; erase on one layer
   and see the layer below; undo and redo all of it. Hidden and locked layers refuse painting
   with a banner.
2. **Brush settings** (toolbar "Brush… 100%"): the preview follows every slider. At 40 %
   opacity, a stroke that crosses itself has no darker spot at the crossing, and a second stroke
   over it is darker. With smoothing 60, a shaky line comes out smooth and still ends exactly
   where the button is released.
3. **Pen (if available):** with the response slider at Soft and then Firm, the same light
   pressure gives a thicker or thinner line. "Lightest" at 100 % gives constant width. The
   status bar says whether pressure is reported.
4. **Async save:** on the heavy drawing, press Ctrl+S and keep drawing. There are no pauses, and
   the title keeps its `*`. Close the window during a save: it closes when the save has finished.
5. **Open and export:** opening a damaged file keeps the current drawing. PNG export runs in the
   background ("Exporting PNG…") and keeps the `*`.
6. **Recovery:** more than 10 s after a change, run `taskkill /F /IM drawapp.exe`. On the next
   start, Restore brings back all layers. Clicking beside the prompt does not dismiss it.
7. **Read-only file:** set the project file to read-only in Explorer and save. An error is shown,
   the drawing keeps its `*`, and the file is unchanged.

## Android (APK builds; tested on an emulator, not on a physical device)

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
# -> %USERPROFILE%\drawapp-build\android-arm64-debug\android-build\build\outputs\apk\debug\android-build-debug.apk (71.8 MB)
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" install -r <that apk>
```

The debug key is for development only. Google Play needs an AAB signed with an upload key;
none is set up and no production credentials are stored in this repository.

The Android build directory **must not contain spaces**. `androiddeployqt` 6.12.0 fails with
"Import found outside of import paths" when it does, so the script builds outside this
repository by default.

### Emulator

An x86_64 build runs on the x86_64 emulator image (the arm64 APK does not):

```powershell
python tools\fetch_qt_windows.py --android --abi x86_64
& "$env:LOCALAPPDATA\Android\Sdk\cmdline-tools\latest\bin\sdkmanager.bat" "emulator" "system-images;android-36;google_apis;x86_64"
& "$env:LOCALAPPDATA\Android\Sdk\cmdline-tools\latest\bin\avdmanager.bat" create avd -n drawapp_test_api36 -k "system-images;android-36;google_apis;x86_64" -d pixel_tablet
& "$env:LOCALAPPDATA\Android\Sdk\emulator\emulator.exe" -avd drawapp_test_api36 -gpu host -no-snapshot
powershell -ExecutionPolicy Bypass -File tools\build-android.ps1 -Config Debug -Abi x86_64
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" install -r "$env:USERPROFILE\drawapp-build\android-x86_64-debug\android-build\build\outputs\apk\debug\android-build-debug.apk"
```

Use `-gpu host`. With the default SwiftShader renderer, the canvas showed a diagonal tear that
is not in the app's image (BENCHMARKS.md, emulator observations).

**Emulator results, 2026-10-12** (Pixel Tablet AVD, Android 16 / API 36, x86_64 google_apis
image, emulator 36.6.11, `-gpu host -no-window`). Input came from `adb shell input` taps, swipes,
emulated stylus swipes, typed text and key events, and was checked from `adb exec-out screencap`
screenshots and pulled files. **This is not a physical device** and says nothing about real pen
input, pressure, performance or a phone-sized screen.

| Check | Result |
|---|---|
| Install and launch the debug APK | Works (cold start 0.8–2.8 s) |
| Drawing with touch and emulated stylus | Works ("Input: touch (no pressure, full size)" for touch) |
| Layer panel: add, delete, rename, opacity | Works. Fixed during testing: the keyboard's Enter moved focus instead of confirming the rename (now `EnterKey.type: Qt.EnterKeyDone`, verified) |
| Footer above the gesture navigation bar | Fixed during testing (SafeArea padding plus an explicit content height), verified |
| Save As through the system picker | Writes `content://…` into Downloads (30.8 KB, 2 layers) |
| Title after Save As | Fixed during testing: showed "10" (the URI's document id), now shows the file's display name, verified |
| Save to the same URI later, also after a process restart | Works (file grew to 41.7 KB, then 22.9 KB for another file) |
| "Save changes?" → Save, then New | New waited for the save to the content URI, then cleared the canvas |
| Open through the picker | Layered project reopened with both layers and the stroke on Layer 2 |
| Export PNG through the picker | 2048×1536 PNG with transparency, checked after `adb pull` |
| Home, then return (`am start`) | Same process, unsaved strokes still there |
| Back with unsaved changes | Moves the app to the background (Android 16 default for the root activity); strokes still there on return |
| Recovery after `am force-stop` (immediate SIGKILL, no shutdown code runs) | Prompt on next launch; Restore brought back 2 layers and the last stroke |
| Recovery after `am kill` while in the background (how the system reclaims memory) | Prompt names "layered.drawapp"; Restore brought back both layers and both strokes |
| Recovery after the emulator was shut down (`adb emu kill`) | Recovery files were kept; the prompt was offered on the next launch |
| Stray tap beside the recovery prompt | Fixed during testing: a tap outside dismissed it (postponing recovery). Now it only closes through its buttons or Back/Escape, verified |

Not tested on the emulator: the in-app Open conflict dialog (painting is disabled while opening,
so it cannot be reached by touch), saving to a provider that refuses writes, and real stylus
pressure or tilt.

**Milestone 5 emulator results, 2026-10-12** (same AVD, same caveats; touch input from
`adb shell input tap/swipe/motionevent`):

| Check | Result |
|---|---|
| Brush settings popup by touch | Opens from "Brush… 100%". Tapping a slider's groove sets opacity, size and smoothing; the preview updates. A tap outside closes it without painting (Undo stayed disabled) |
| Uniform opacity | An X drawn in one stroke at 50 % shows no darker crossing, on screen and in the exported PNG |
| Smoothing 61 | Corners rounded and the stroke ends at the lift-off point. A ±9 logical-px zigzag (raw `motionevent` moves) became a slight ripple |
| What the Downloads provider reports (logged by the probe) | Seekable regular file, free space from `fstatvfs`, flags 0x46/0x47 (write, rename, delete supported; thumbnails for PNG) |
| Save As to a new picker document | No prompt (empty document); `posix_fallocate` reservation **ok**; written and read back |
| Save over existing contents | Prompt "Overwrite …?" with the storage details; Cancel writes nothing. Overwrite writes, and later saves of that file in the session are not asked again |
| Save a Copy… | Picker opens; new file written (55,324 bytes); the original's SHA-256 unchanged |
| Storage completely full (filled with `fallocate` + `dd` until `df` showed 0) | "Could not keep a copy of the drawing on this device before saving (… No space left on device). The file was not changed." The destination's SHA-256 was unchanged and the drawing stayed open |
| Document deleted behind the app's back (`adb shell rm`) | "Could not save "m5c.drawapp": The file is no longer available … The file was not changed." Fixed during testing: the message showed the document id ("14") until the app kept the name itself |
| PNG export | Runs on the worker (0.47 s), reservation ok, exported image correct |
| App reinstalled while unsaved (`adb install -r` kills the process) | Restore offered on next launch, drawing back |

**Milestone 6 emulator results, 2026-10-12** (same AVD; `-no-window`, so nothing on the desktop;
touch from `adb shell input`; mouse and touch have no pressure or tilt):

| Check | Result |
|---|---|
| Active brush in the toolbar | Thumbnail and name; the selected preset persisted across app restarts and a reinstall (brush file in app data) |
| Brush panel by touch | Opens from the active brush. All 8 thumbnails rendered by the engine. Tapping a preset selects it; ☆ toggles the favourite. "More settings…" shows the advanced controls with the preset's switches |
| Scratchpad | A swipe draws with Clean Ink; nothing reaches the document (Undo stayed disabled) |
| Tap outside the panel | Closes it without painting |
| Textured Paint, Blend / Smudge on the canvas | Bristle streaks; the smudge drags paint into transparency |
| Soft Airbrush tap vs 1.5 s hold | Faint dot vs dark dot: buildup over time while held still |
| Fixed during testing | "▾"/"▸" showed as missing-glyph boxes in Android's default font; replaced by text ("More settings…", "Fewer settings") |

Not checked on the emulator: pressure and tilt (it has no pen), and performance.

**Milestone 8 emulator results, 2026-10-12** (same AVD, `-no-window`; touch from
`adb shell input`):

| Check | Result |
|---|---|
| Select tool and its bar | The bar sits at the bottom of the canvas and wraps onto two rows in portrait; buttons are 40 px tall |
| Rectangle selection by touch | Outline drawn as a two-tone line on the drawing |
| Transform by touch | Box, eight handles, rotation knob and pivot shown. Dragging the knob rotated the selection (67° shown in the angle field); Flip H; Apply. The selection outline followed the rotated pixels and was cut off at the canvas edge |
| Copy, Paste | "Pasted" layer added above, at the original position |
| Not checked | Pen input (the emulator has none), performance, the system clipboard |

**Milestone 9 emulator results, 2026-10-12** (same AVD, `-no-window`; touch from
`adb shell input`; x86_64 debug APK of the milestone 9 code):

| Check | Result |
|---|---|
| Brush library from the milestone 8 build (format 1) | Read by the new build (format 2); presets and the selected brush kept |
| Brush panel → "Brush Studio…" | Studio opens full-window in portrait (wide layout at 800 dp) |
| Categories, switches | Category list and "Follow sensors" respond to taps; the curve editor appears |
| Curve editor by touch | Dragging the end point changed the curve and the preview stroke |
| Try-out pad by touch | Draws with the edited copy; full size (touch has no pressure, so the pressure curve does not apply, as intended) |
| Cancel | Preset unchanged (no unsaved marker, Save disabled) |
| Fixed during testing | "↺" and "✕" were missing glyphs in Android's font (now "Reset", "Remove"); the Studio's top row was under the status bar (now inside the safe area); "% of size" was cut off |
| Not checked | Pen pressure and tilt, `.drawbrush` pickers, the twelve artist workflows (Windows only), performance |

Not reachable on the emulator: the provider's own free-space check (private and shared storage
share one partition, so the local copy fails first, which is also safe); a write that stops part
way (covered by `tst_canvas` with a fault-injecting provider); revoking a URI grant without
deleting the file; cloud or stream-only providers.

What is and is not guaranteed for local files and for provider documents is in ARCHITECTURE.md,
"Save guarantees".

### Physical Android device (arm64): install and test

Nothing below has been run yet; no device was available.

1. On the device, enable Developer options (tap Build number 7 times), turn on **USB debugging**
   and connect it by USB. Accept the computer's key on the device.
2. Build and install the debug APK:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\build-android.ps1 -Config Debug
   & "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" devices      # the device must be listed as "device"
   & "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" install -r "$env:USERPROFILE\drawapp-build\android-arm64-debug\android-build\build\outputs\apk\debug\android-build-debug.apk"
   ```
3. Collect the log while testing:
   `adb logcat -s default:I Qt:I` (the `[file]` lines show what the storage provider reported
   and whether space reservation worked).
4. Check, and note the device model and Android version:
   - **Pen** (if it has one): the status bar says "Input: pen, pressure 0.xx". Light-to-firm
     strokes taper. The response slider (Soft/Firm) and "Lightest" change that visibly. The
     pen's eraser end erases. A palm resting while drawing does not paint.
   - **Finger**: draws at full size; two fingers pan and zoom without leaving marks.
   - **Brushes**: the panel is usable with a finger; opacity is uniform within a Solid Round
     stroke; smoothing ends at the lift-off point. With the pen, per preset:
     - Sketch Pencil shades lighter and grainier with light pressure and wider when tilted (if the
       pen reports tilt);
     - Clean Ink tapers with pressure, without forced tails;
     - Soft Airbrush builds up while held still;
     - Textured Paint turns with the stroke or the pen's tilt;
     - Blend / Smudge pushes colour, and its strength follows pressure.

     Try the scratchpad. If Krita is installed on the same device, compare its Pencil, Ink and
     Airbrush presets with ours by feel, and note what differs (not done yet: no pen hardware).
   - **Files**: Save As to Downloads and to a cloud provider (for example Drive). Save again (the
     overwrite prompt appears), choose Save a Copy once and Overwrite once. Open and Export PNG.
   - **Lifecycle**: Home and return; `adb shell am kill org.drawapp.prototype` while in the
     background, then relaunch: Restore offered.
   - **Speed**: draw quickly with a 128 px brush on 10 layers; note any visible lag (not
     measurable with these tools).

## Linux (blocked here)

## Linux (blocked here)

Exact blockers, checked 2026-10-12 in WSL Ubuntu 24.04:
- There is no C++ compiler, CMake, Ninja or OpenGL/xcb development packages. Installing them
  needs the owner's sudo password. That password is not requested or stored here, and the
  install was not bypassed (for example with `wsl -u root`).
- Ubuntu 24.04's packaged Qt (6.4) is older than this project's minimum (6.8).

**Step 1: run this yourself** in the WSL Ubuntu 24.04 terminal (it asks for your password):

```bash
sudo apt update && sudo apt install -y build-essential cmake ninja-build python3-venv \
  libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0 libfontconfig1-dev libfreetype-dev \
  libx11-xcb1 libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-shape0 libxcb-xinerama0 \
  libxcb-randr0 libxcb-image0 libxcb-render-util0 libxcb-xkb1 libvulkan-dev
```

**Step 2** (no sudo; can be run by the agent afterwards). `tools/fetch_qt_windows.py --linux`
downloads Qt 6.12.0 `linux_gcc_64` (qtbase, qtdeclarative, qtsvg, icu) and checks each
archive's SHA-1. It must run on Linux, because the archives contain symlinks. Its `--dry-run`
ran successfully from Windows; the steps below are unverified past that:

```bash
python3 -m venv ~/drawapp-venv && ~/drawapp-venv/bin/pip install py7zr
~/drawapp-venv/bin/python tools/fetch_qt_windows.py --linux          # -> ~/Qt/6.12.0/gcc_64
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/gcc_64
cmake --build build/linux && QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --output-on-failure
```

The window tests need `QT_QPA_PLATFORM=offscreen` on Linux, because the ctest environment
settings in `tests/CMakeLists.txt` are Windows-only.

## iOS / iPadOS (blocked)

Requirements, none of which is available here:
- a Mac with macOS 14.4 or later and Xcode 16 or later (Qt 6.12's iOS requirement);
- Qt 6.12 for iOS (installer component `ios`) plus the macOS host Qt;
- an Apple ID for device signing (a free account can sign for one's own device for 7 days);
- an iPad with Apple Pencil for pen testing.

The CMake file sets bundle properties, but no iOS configure has ever run. The storage code is
written for Android only: iOS would need its own document-picker path, which is not written.
Resolve the Qt LGPL/static linking question (ARCHITECTURE.md) before shipping.

## Platform verification status

| Platform | Status | Evidence |
|---|---|---|
| Windows 11 x64 (25H2, Intel Core Ultra 7 155H, 200 % scaling) | **Built and launched** | `ctest` 8/8 suites, milestone 9 (tst_core 34, tst_brush 37, tst_brushengine 28, tst_selection 34, tst_document 63, tst_recovery 28, tst_canvas 100, tst_ui 13; QtTest totals including init/cleanup and data rows). Milestones 0–2 were driven on the desktop with real input. Milestones 3–8 (layers, async files, brush presets and panel, selections and transforms, overwrite prompt, provider failures with a fake provider) were driven with synthetic input through the real CanvasItem and Main.qml on the offscreen platform, with screenshots and the brush test sheet inspected. **One on-screen session is still pending** (short checklist above), because the desktop was in use. Pen pressure was tested with synthetic tablet events only |
| Android x86_64, emulator | **Launched and exercised on an emulator** | Pixel Tablet AVD, Android 16, `-gpu host` (results tables above): drawing, layers, brush settings, uniform opacity, smoothing, picker Save/Open/Export, overwrite prompt, Save a Copy, full storage, deleted document, background/return, recovery after `am force-stop`, `am kill` and reinstall; milestone 6 brush panel, presets, scratchpad and airbrush by touch; milestone 8 selection, transform, copy and paste by touch; milestone 9 Brush Studio by touch |
| Android arm64 | **Built; not run on a physical device** | Debug APK (development key) rebuilt for milestone 9. Same source as the emulator build. Pen pressure and tilt, brush feel, palm rejection, cloud providers and performance on real hardware are unverified (instructions above) |
| Linux | **Blocked** | Needs the owner to run one `sudo apt install` command (above); everything after it is prepared |
| iOS / iPadOS | **Blocked** | Needs a Mac with Xcode 16+ (above) |

# Architecture

State: milestone 0 prototype. Everything described under "Current" exists in the code; anything
else is labelled *planned*.

## Stack

- C++17, Qt 6.12.0 (Qt Quick, Qt Quick Controls with the Fusion style, Qt Quick Dialogs), CMake ≥ 3.22, Ninja.
- Windows toolchain: MinGW-w64 GCC 15.1.0 (the compiler Qt 6.12.0's MinGW binaries were built with).
- Android toolchain: NDK r27c (27.2.12479018, Clang 18), JDK 21, compile/target SDK 36, min SDK 28.
- No third-party libraries beyond Qt.

Why Qt 6 with Qt Quick/QML: one C++ codebase for Windows, Linux, Android and iOS, with a GPU
scene graph and pen/touch input on all four. We found no blocker. The Windows and Android builds
work; iOS and Linux are untested (see BUILD.md).

## Components (current)

```
src/core/   (static lib "drawcore", depends on QtGui only, no Qt Quick)
  ViewTransform    view = doc * zoom + pan; zoom about an anchor; fit/center; zoom clamp [0.05, 32]
  Pressure.h       effectivePressure(kind, deviceReportsPressure, raw) -> 0..1
  StrokeSpacer     input samples -> evenly spaced dabs, distance carried across samples
  RasterDocument   one QImage, ARGB32 premultiplied, fixed 2048x1536 (12 MiB), starts transparent
  ViewRenderer     paints an exposed view rect: workspace, checkerboard, document through the view
src/app/
  CanvasItem       QQuickPaintedItem; input handling, stroke state, partial repaints
  Main.qml         window, toolbar, status bar, color dialog, clear confirmation, shortcuts
  main.cpp         application setup
tests/             tst_core (headless), tst_canvas (real window, synthetic input), bench_core
```

## Data flow

1. **Input** reaches `CanvasItem`:
   - Mouse: `mousePress/Move/Release`. Left button draws; middle button, or Space + left, pans.
   - Pen: a window-level event filter on the `QQuickWindow`, because `QQuickItem` has no
     tablet-event handler. Press must land inside the item. The event is accepted, so Qt does
     not synthesize mouse events from it.
   - Touch: `touchEvent`. One finger draws; two fingers pan and zoom together.
   - Wheel: zooms about the cursor (one notch = ×1.25); a pixel-delta-only wheel (touchpad) pans.
2. **Pressure** goes through `effectivePressure`. It is used only for a pen whose
   `QPointingDevice` reports the `Pressure` capability. Mouse, touch (even if a touchscreen
   reports pressure) and pens without pressure use 1.0.
3. **Coordinates**: positions are converted with `ViewTransform::viewToDoc` before anything else.
   Document pixels never depend on the view.
4. **Spacing**: `StrokeSpacer` emits a dab every `max(0.5 px, 0.15 × last dab diameter)` along the
   straight line between samples, interpolating pressure linearly.
   `diameter = size × (0.1 + 0.9 × pressure)`, at least 1 px. A press always emits one dab, so a
   click leaves a dot.
5. **Raster**: `RasterDocument::stampDabs` draws anti-aliased circles with one `QPainter` per input
   event (source-over for the brush, destination-out for the eraser) and returns the changed rect.
6. **Repaint**: the changed rect is mapped to view coordinates and passed to `update(rect)`.
   `paint()` renders only the painter's clip rect through `ViewRenderer`. Zoom, pan and resize
   call `update()` for the whole item.

Brush size, color and mode are captured when a stroke starts, so UI changes affect only the next
stroke. The pen's eraser end erases without changing the selected tool.

## Decisions

| Decision | Reason |
|---|---|
| CPU raster prototype in one `QImage` | Simplest useful start; measured to be fast enough at this size (BENCHMARKS.md) |
| `QQuickPaintedItem` with partial `update(rect)` | No custom scene-graph code yet; avoids full-canvas work per input event |
| Fixed 2048 × 1536 canvas, transparent background | Spec asked for a fixed, documented size; transparency makes erasing visible |
| Opaque brush colors only | Overlapping dabs would build up translucent colors within a stroke |
| Mouse ignored for 400 ms after any pen event | Windows can send OS-synthesized mouse events for pen input |
| Touch never uses pressure | Requirement: touch must not be treated as pressure pen input |
| Nearest-neighbour above 100 % zoom, smooth below | Artists see real pixels when zoomed in |
| MinGW 15.1 from MinGW-Builds (GitHub), SHA-256 pinned | Qt's repository metadata still points at MinGW 13.1, but the Qt binaries were built with 15.1 |

## Limitations (known)

- **Not a final painting engine.** No tiles, no worker threads, no GPU brush. One big `QImage`.
- `QQuickPaintedItem` renders through an image that the scene graph uploads as a texture. We
  have not measured upload cost or end-to-end input latency.
- No stroke smoothing, prediction, pressure curve, tilt or rotation.
- Fast pen motion is approximated by straight segments between samples (visible as corners on
  very fast curves).
- No undo, no saving, single layer.
- High-DPI: rendering was sharp at 200 % scaling in screenshots; other scale factors are untested.
- Touch: no palm rejection beyond ignoring touch while (or 400 ms after) the pen is active. A
  second finger ends the current stroke, and marks already made stay.
- Pen buttons: any non-tip button with the tip down pans. Not tested on hardware.

## Future requirements (noted, not built)

Layers and blend modes; tiled storage and partial texture uploads; asynchronous stroke
processing if measurements require it; undo with bounded memory; project format and crash
recovery; the document model may hold both raster layers and editable strokes.

## Licensing notes

- Qt is used under LGPLv3 (open-source binaries). Windows and Android link Qt dynamically.
  **Open decision:** iOS apps link Qt statically, and LGPLv3 compliance there needs a deliberate
  approach (for example providing relinkable object files) or a commercial Qt license. Decide
  before any iOS release.
- The MinGW runtime DLLs shipped with the Windows build are GPL with the GCC runtime library
  exception.
- No Krita code is used. See `research/krita-input-and-canvas.md` for the provenance note.

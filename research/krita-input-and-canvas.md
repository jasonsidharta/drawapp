# Krita reference: input → brush marks, and canvas zoom/pan

Bounded research for milestone 0. This covers only the input path, dab spacing and view
transform. It is not a study of Krita as a whole.

## Reference checkout

| | |
|---|---|
| Revision | `25840ee8949d9c845f077bbc8188743d1364a9d6` (master, committed 2026-10-11 02:19 UTC, "GIT_SILENT Sync po/docbooks with svn") |
| Official repo | https://invent.kde.org/graphics/krita.git; `git ls-remote` returned the same hash for `refs/heads/master` on 2026-10-11 |
| Local checkout | Cloned from the GitHub mirror `https://github.com/kde/krita` into `C:\Users\Asus\OneDrive\Documents\cp lagi\krita`, outside this repository. It is read-only for us and has no local changes. |
| License | Mostly GPL-2.0-or-later / GPL-3 (per-file SPDX headers). Nothing from it is copied into this project. |

Krita's README sets a moratorium on AI-assisted development *of Krita* (contributions to Krita).
Reading the source to inform an independent application is a different activity, but the
project owner should be aware of the policy.

## Provenance note (read this)

This research was done by an AI agent, and the same session then wrote this project's code. It
is therefore **not** a strict clean-room separation. Mitigations:

- No Krita code, identifiers, structure or comments were copied or translated.
- Our implementations follow requirements written in our own terms (see ARCHITECTURE.md). The
  techniques involved are generic and widely documented: linear dab interpolation with a
  carried-over distance, an affine view transform with an anchor point, and filtering pen
  events at the window.
- `StrokeSpacer` is the piece closest to something read here (`KisDistanceInformation`'s
  isotropic spacing). It is about 30 lines implementing a standard technique. If stricter
  provenance is needed later, have a person who has not read Krita re-implement it from the
  behaviour spec in ARCHITECTURE.md.

## Source paths and symbols examined

| Path | Symbols | Why |
|---|---|---|
| `libs/ui/input/kis_input_manager.cpp` | `KisInputManager::eventFilter` (TabletPress/Move/Release, TouchBegin/Update/End/Cancel cases, about lines 599–760) | Routing of tablet, touch and mouse events |
| `libs/ui/tool/kis_painting_information_builder.cpp` | `startStroke`, `continueStroke`, `createPaintingInformation`, `pressureToCurve` | Pointer event → paint information |
| `libs/ui/tool/kis_tool_freehand_helper.cpp` | `initPaint`, `initPaintImpl`, `paint`, `endPaint`, `paintLine`, `paintBezierCurve`, smoothing branches | Stroke lifecycle and smoothing |
| `libs/image/brushengine/kis_paintop_utils.h` | `KisPaintOpUtils::paintLine`, `paintFan` | Dab placement along a segment |
| `libs/image/kis_distance_information.cpp` | `getNextPointPosition`, `getNextPointPositionIsotropic`, `getNextPointPositionAnisotropic`, `getNextPointPositionTimed`, `resetAccumulators` | Spacing across input samples |
| `libs/ui/canvas/kis_coordinates_converter.h` | `KisCoordinatesConverter` (image/document/flake/widget/viewport transforms, `setZoom` with still point, `preferredTransformationCenter`, `snapToDevicePixel`) | View transform |
| `libs/ui/input/kis_zoom_action.cpp` | zoom modes, `makeWidgetStillPoint(eventPos)` | Zoom anchored at the cursor |
| `libs/image/tiles3/kis_tile_data_interface.h` | `__TILE_DATA_WIDTH/HEIGHT = 64` | Raster storage granularity |
| `libs/ui/opengl/kis_opengl_image_textures.h`, `kis_texture_tile.h`, `libs/ui/canvas/kis_qpainter_canvas.h` | file names only | The canvas has OpenGL (tiled textures) and QPainter backends. Not studied further. |

## Verified behaviour (read in source at the revision above)

1. **Tablet events are handled at the input-manager level, and Windows needs extra mouse
   suppression.** On `TabletPress` Krita accepts the event and, on Windows, calls
   `eventEater.eatOneMousePress()`, because "Windows is the only platform that synthesizes mouse
   events for the tablet on OS-level, that is, even when we accept the event"
   (`kis_input_manager.cpp`, about line 620). On Linux without a Qt patch it marks the event
   unaccepted and also eats one mouse press.
2. **Touch is accepted unconditionally on the canvas** to disable system-wide gestures, and it
   goes to a separate shortcut matcher (`touchBeginEvent`/`touchUpdateEvent`). Touch is a
   different path from tablet; it is not treated as pressure input.
3. **Pressure goes through a configurable curve**: `pressureToCurve()` samples a cubic curve at
   1025 points and interpolates linearly. The flag `m_pressureDisabled` is misnamed. It is read
   from the resource `DisablePressure`, whose action `disable_pressure` is labelled "Use Pen
   Pressure" in `krita/krita.action` and starts checked, so a value of `true` means *use*
   pressure (`createPaintingInformation`, line 131).
4. **Dab spacing carries distance across input segments.** `getNextPointPositionIsotropic`
   keeps an accumulated distance. If the remaining distance to the next dab fits within the
   current segment it returns a fraction `t` and resets the accumulator; otherwise it adds the
   segment length and returns −1. `paintLine` loops on this, interpolating paint information
   (position, pressure, time) with `KisPaintInformation::mix`. Time-based spacing (airbrush)
   competes with distance spacing, and the smaller `t` wins. Anisotropic spacing solves an
   ellipse equation for rotated or elliptical brushes.
5. **A click without movement still makes a mark**: `endPaint()` calls `paintAt` when nothing
   was painted.
6. **Painting is asynchronous.** The freehand helper turns segments into jobs
   (`strokesFacade->addJob`), executed by the image's stroke system, not in the input handler.
   Smoothing modes (none, simple, weighted, stabilizer, pixel-perfect) sit between input and
   `paintLine`. The stabilizer uses a timer-driven sampler.
7. **Zoom keeps a "still point".** Zoom actions create a still point from the event position, so
   the document point under the cursor stays put. The converter notes that pixel snapping can
   offset it by up to `0.5·√2 / devicePixelRatio`, and it keeps a separate
   `preferredTransformationCenter` so repeated zooms don't drift.
8. **Raster data is tiled** (64×64-pixel tiles), and the GPU canvas uploads tiled textures.

## Inferences (not verified by running Krita)

- Asynchronous stroke jobs plus tiling are what let Krita keep input responsive on large images.
  We have measured that our synchronous CPU path is fast at 2048×1536 (BENCHMARKS.md); this
  would need revisiting for much larger canvases or expensive brushes.
- Mouse suppression after pen input is needed on Windows. We implemented a time window
  (400 ms) instead of "eat exactly one press". This has only been tested with synthetic events,
  not real Windows Ink hardware.

## Unresolved questions

- How Krita turns tablet events into mouse-compatible tool events on Android and iOS (not read).
- Whether Krita applies event compression or prediction for low-latency pen input (only
  `compressMoveEventCommon` and `tabletLatencyTracker` were seen, not studied).
- How the OpenGL canvas updates partial textures. This matters for our renderer roadmap.

## Tradeoffs for our smaller application

| Krita | Us, now | Why |
|---|---|---|
| Asynchronous stroke jobs, worker threads | Synchronous stamping in the input handler | Measured median 0.005–0.009 ms per input event; a queue is not justified yet |
| 64×64 tiles, sparse storage | One `QImage` (12 MiB at 2048×1536) | Fixed small canvas in this milestone; revisit for large canvases and layers |
| Five smoothing modes, pressure curve | None; linear pressure with a 10 % minimum size | Out of scope; curve and smoothing are candidate next features |
| Isotropic, anisotropic and timed spacing | Isotropic distance spacing only | Round brush only |
| Still point plus preferred center to avoid drift | Exact anchor math in floating point, no pixel snapping | No rotation and no snapping, so there's no drift to correct; tested in `tst_core` |
| Rotation, mirroring, wrap-around | Not implemented | Deferred (ROADMAP) |

# Roadmap

## Milestone 0: drawing prototype (current)

Scope: window and toolbar; one fixed raster canvas (2048 × 1536); round brush with size and
color; eraser to transparency; mouse, pen and touch input; pressure-controlled size when valid;
zoom and pan; clear; checks for coordinates and raster operations.

Acceptance on the host platform (Windows 11 x64):

| # | Criterion | Status | How verified |
|---|---|---|---|
| 1 | Configures and compiles with documented commands | Done | `tools/build-windows.ps1`, clean build |
| 2 | Application launches | Done | Deployed build launched with a clean PATH; screenshot |
| 3 | Brush draws continuous marks | Done | `tst_core::strokeIsContinuousInRaster`, `tst_canvas::mouseStrokeIsContinuous` (sparse input); real-mouse screenshot |
| 4 | Size and color change subsequent marks | Done | `tst_canvas::sizeAndColorAffectNextStroke` |
| 5 | Erasing reveals transparency | Done | `tst_core::eraseMakesTransparent`, `tst_canvas::eraserRevealsTransparency` (document alpha and on-screen checkerboard) |
| 6 | Zoom and pan keep drawing coordinates correct | Done | `tst_core` view tests; `tst_canvas::zoomAndPanPreserveCoordinates` (mark appears under the cursor on screen); real wheel and middle-drag |
| 7 | Resizing the window keeps the artwork | Done | `tst_canvas::resizePreservesArtwork` (pixel-identical document over 3 sizes) |
| 8 | Mouse works without pressure | Done | Tests and real OS mouse input |
| 9 | Pen pressure | **Untested on hardware.** Synthetic `QTabletEvent`s pass (`penPressureControlsSize`, `penWithoutPressureUsesFullSize`, `penEraserTipErases`, `mouseRightAfterPenIsIgnored`). No pen hardware was used. |
| 10 | Clear removes artwork | Done | `tst_canvas::clearRemovesArtwork` |
| 11 | UI stays usable during ordinary drawing | Partly | Short automated session; input 0.06 ms/event, paint 0.14 ms avg. No long manual session yet |

Platform status: see BUILD.md (Windows built and launched; Android built, not launched; Linux and
iOS blocked).

## Milestone 1: hardware input validation (next; smallest useful step)

- Run on a Windows pen device (Windows Ink) and an Android tablet with a stylus, plus finger touch.
- Record pressure range, event rate, OS-synthesized mouse behaviour and palm contacts. Adjust the
  pen/mouse/touch separation from the measurements.
- Add a simple pressure curve (gamma) setting if raw pressure feels wrong.
- Measure end-to-end latency on both devices; record in BENCHMARKS.md.
- Unblock Linux (WSL packages) and run the same tests.

Acceptance: pressure verified on at least one Windows and one Android pen device; touch never
produces pressure; no stray marks from palm or synthesized mouse in a 10-minute session.

## Milestone 2: undo and saving

Bounded-memory undo (tile- or region-based snapshots) and a project format with atomic writes
plus PNG export. Acceptance: kill the app mid-stroke and lose at most the current stroke.

## Milestone 3: layers

Multiple raster layers with opacity, visibility and normal/multiply/screen blend. This is likely
the point where we need tiled storage and partial texture uploads; decide from measurements.

## Later (unordered)

Canvas rotation and mirroring; smoothing/stabilizer; brush presets; selection and transform;
iOS build (needs macOS and a licensing decision); Play Store AAB signing and release pipeline.
Deferred features are listed in PRODUCT.md.

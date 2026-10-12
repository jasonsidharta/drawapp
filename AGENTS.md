# Instructions for coding agents

Read PRODUCT.md, ARCHITECTURE.md, ROADMAP.md and BUILD.md before changing code. Keep them true:
update them in the same change as the code, and label planned work separately from done work.

## Rules

- Independent application. Do not copy or translate code from Krita or other GPL projects.
  Research notes go in `research/` with revision, paths, verified facts and inferences kept
  separate. Any Krita checkout stays outside this repository and is read-only.
- Scope is set by ROADMAP.md. Don't add frameworks, plugin systems or abstractions for
  hypothetical needs.
- `src/core` depends on QtGui only (no Qt Quick) and is testable headless. Keep document pixels
  independent of the view transform.
- Never report a platform as verified without running it there. Never claim speed or memory
  improvements without numbers in BENCHMARKS.md.
- Touch must never be treated as pressure input. Pressure is used only when the pen device
  reports it (`effectivePressure`).

## Build and test (Windows)

```powershell
python tools\fetch_qt_windows.py          # once
powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1
```

All tests must pass before handing off: `tst_core`, `tst_brush`, `tst_brushengine`, `tst_tiles`, `tst_fileformat`, `tst_selection`, `tst_document`, `tst_recovery` (headless),
`tst_canvas` and `tst_ui` (windows; under ctest they render offscreen so they do not take over the
user's desktop). They save screenshots to `build\windows-mingw-release\tests\tst_*.png`; look at
them after rendering or QML changes. Do not drive the real desktop with synthetic OS input unless
the user is not using the machine. Android: `tools\build-android.ps1 [-Config Debug] [-Abi x86_64]`
(build directory must not contain spaces; `x86_64` for the emulator, see BUILD.md). Emulator
results are not physical-device results; label them as such.

## Version control

Local Git repository on branch `main`, no remote. Commit only when the project owner asks; never
push. Never commit build outputs, APKs/AABs, keystores or other credentials (see `.gitignore`).

## Undo and file invariants

- Every artwork change goes through `CanvasItem::commitEdit` with a `TileSnapshot` captured
  *before* the change. View state (zoom, pan) never enters history.
- A stroke must become exactly one undo step however it ends (`endStroke`).
- Changing the meaning of `.drawapp` bytes requires a new format version and keeping the reader
  for older versions. Validate before allocating; never replace the open document on failure.
- File writes go through `ProjectFile::writeFileSafely`.

## Layer invariants

- Layers are addressed by stable id everywhere (undo commands, QML, files); never by index across
  edits. Ids are never reused within a document.
- Every change to layer pixels or properties refreshes the composite for the changed area and goes
  through an `UndoCommand`; selecting the active layer is not an edit.
- Painting (brush, eraser, clear) must check `activeLayerBlockReason()`.
- Editor limits and file-reader limits come from `DocumentLimits`; keep them shared.
- Workers and saved snapshots get `DocumentData` copies (copy-on-write images), never live layers.

## Crash recovery invariants

- Recovery never writes the user's project path; only `RecoveryManager` touches the recovery folder.
- Worker code receives a private `QImage` copy and metadata only; it must not reach `CanvasItem`
  or `RasterDocument`. Every commit or delete of the recovery file happens under
  `m_commitMutex`, and `discard()` must bump the generation.
- Anything that changes pixels must call `markPixelsChanged()`. Anything that makes the current
  pixels saved or deliberately abandoned must call `discardRecoveryFile()`; failures and
  cancellations must not.
- Do not claim the autosave interval is the maximum loss; report measured windows (BENCHMARKS.md).

## Async save/open invariants

- Save and open go through `ProjectIo` (one worker, one operation at a time). The worker gets a
  `DocumentData` snapshot or a path, never `CanvasItem` or the live document.
- A finished save marks as saved only the `stateId` recorded when its snapshot was taken, and
  changes the path only on success. A failed save changes nothing.
- An opened document replaces the current one only after success and only if the document did
  not change meanwhile; otherwise ask (`openConflict`). Never drop either version silently.
- New/Open/Close after a requested save wait for its success. Every new file action must check
  `fileBusy`. Test races with `ProjectIo::setBeforeFinishHookForTesting`.

## Save safety invariants (milestone 5)

- Every project write goes through `SafeSave`: encode, `ProjectFile::verify`, then either an
  atomic local write or the provider path. Never write a `content://` document any other way
  (`ProjectFile::writeFileSafely` refuses them).
- Provider path order: probe → overwrite confirmation → encode/verify → local copy written **and
  read back** → space check → reserved write → read-back compare → release the copy. Do not
  touch the destination before the local copy succeeded.
- Do not describe a provider save as atomic, and do not claim more than ARCHITECTURE.md "Save
  guarantees" says. Test provider behaviour with `tests/FakeProvider.h`.
- While a save runs, autosave must not delete the recovery file (it may be the save's copy).

## Brush invariants

- Each stroke gets one `StrokeEngine` (smoother, spacer, renderer), created in `startStroke` from
  the settings at that moment; never read live settings mid-stroke.
- The engine recomputes pixels from the stroke's undo `TileSnapshot`; anything that changes the
  layer mid-stroke must end the stroke first (as undo, save and layer actions already do).
  Escape cancels by swapping that snapshot back.
- Pressure stays behind `effectivePressure`: mouse and touch are 1.0, whatever the mappings say.
  Tilt is used only when the device reports it. Smoothing is distance-based; do not add timers or
  time-based filters to it (the airbrush timer adds dabs; it does not filter input).
- `endStroke` completes the smoothed tail; keep it the only place a stroke ends.
- `StrokeEngine` is the only brush renderer (canvas, scratchpad, thumbnails, brush sheet). Presets
  differ in `BrushSettings` data only; do not add per-preset rendering paths.
- Opacity is applied once (the coverage ceiling); flow is per pass and normalised by spacing and
  the tip's pass length. Airbrush buildup comes from elapsed time, never from the event count.
- Tips and textures are generated by our code (`BrushTips.cpp`). Do not add brush images or
  presets copied from other applications.
- The temporary size never changes a saved preset; only an explicit Save does.
- After changing the engine, tips or presets, render `brush_sheet` and look at it.
- Every brush property is declared once in `BrushProperties` (registry). A property may be
  marked implemented only with a scenario in `tst_brushengine::everyImplementedPropertyChangesRendering`
  that shows a rendering change. Regenerate `docs/brush-settings-specification.md` with
  `DRAWAPP_WRITE_SPEC=1 tst_brushengine` after registry changes.
- Sensors without device data are left out of dynamics (`StrokeSample::hasPressure`, `hasTilt`);
  never feed a default value into a curve. Simulated pressure in tests and previews is labelled.
- Every random choice comes from `hash(seed, dab index, salt)` with its own salt; never use a
  global RNG in the engine.
- Rows rendered on worker threads get plain pointers into images detached on the calling thread;
  never call `QImage::scanLine()` from a worker.
- Read files with `ProjectFile::readBounded`, never `QIODevice::read(limit)` (it reserves the
  limit).
- Rendering changes that alter `tests/golden/stroke-*.png` must be looked at and re-recorded
  deliberately (`DRAWAPP_WRITE_GOLDEN=1`).

## Selection and transform invariants

- "No selection" and an empty selection are different states. Never turn an empty result into
  "no selection"; every edit through a selection must refuse when it is empty.
- The selection is applied once per pixel (at compositing, or as `before + (work − before) × s`
  for smudge); never multiply it per dab.
- Selection changes go through `setSelectionWithUndo`; they must not change the saved state.
  Selections are not written to project or recovery files.
- A transform preview never writes to the document. Render previews from the session's cached
  source only. Apply is one `UndoCommand` (pixels with the selection); Cancel only drops images.
- Anything that edits the document or replaces it calls `finishPendingEdits()` (applies an
  open transform) or, for replacement, cancels it first.
- Full repaints use `updateAll()`: `update()` without a rectangle is lost if a partial update
  follows before the next frame.

## Conventions

C++17, Qt types, `m_` members, one class per file pair (namespaces like `ProjectFile` excepted).
Comments only for non-obvious reasons. Brush/stroke parameters are captured at stroke start.
User-facing errors are returned as strings from `CanvasItem` file methods and shown by QML.

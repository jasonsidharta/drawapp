# Roadmap

## Milestone 0: drawing prototype (done on Windows)

One raster canvas (2048 × 1536), round brush and eraser, mouse/pen/touch input, zoom and pan,
clear. Pen pressure verified only with synthetic tablet events. Commit `bb3e9a2`.

## Milestone 1: undo/redo and explicit save/open (done on Windows)

Tile-snapshot undo/redo under a 128 MiB budget; versioned `.drawapp`; atomic saving; New/Open/
Save/Save As with unsaved-changes prompts; PNG export. Commit `8ccad84`.

## Milestone 2: crash recovery (done on Windows)

Per-session recovery files in the app data folder, 10 s autosave, worker-thread writes with
generation-gated atomic commits, Restore/Discard on start, verified with forced kills. The loss
window is the interval plus write time, not "only the current stroke". Mobile recovery is
unverified. Commit `a98ec27`.

## Milestone 3: raster layers (done on Windows)

Commit `784b2c9`. The on-screen Windows session and the Android run listed below were carried
over to milestone 4. Android has now run on an emulator; the on-screen Windows session is still
pending.

| Criterion | Status | How verified |
|---|---|---|
| Layer model: stable id, name, raster, visibility, opacity, lock; ordered stack; active layer | Done | `tst_document` layer tests |
| Normal compositing with opacity; transparency kept; erasing an upper layer reveals lower ones | Done | `compositeOrderOpacityAndVisibility`, `compositeKeepsTransparency`, `erasingUpperLayerRevealsLowerLayer`, `strokesAffectOnlyTheActiveLayer` |
| Panel: add, select, rename, duplicate, delete, up/down, visibility, lock, opacity | Done | `tst_ui` drives the real Main.qml panel (offscreen) building a Background/Sketch/Color/Line art illustration; screenshots inspected |
| At least one layer kept; predictable active layer after delete and undo | Done | `keepsAtLeastOneLayerAndSelectingIsNotAnEdit`, `deletingActiveLayerSelectsTheLayerBelow`, `activeLayerAfterDeleteAndUndo` |
| Brush/eraser only on the active layer; hidden/locked layers refuse painting with an explanation | Done | `strokesAffectOnlyTheActiveLayer`, `hiddenAndLockedLayersRejectPainting` (mouse and pen), `tst_ui` banner text |
| Clear Active Layer replaces Clear | Done | toolbar; refused on hidden/locked layers |
| Undo/redo of all layer operations; one opacity drag = one step; selection not an edit | Done | `layerOperationsAreUndoableOneStepEach`, `opacityDragEndingAtItsStartIsNotAnEdit`, `tst_ui` slider drag |
| Undo after deletion and reordering targets the right layer | Done | `undoAfterReorderTargetsTheRightLayer`, `undoAfterDeletionRestoresLayerAndPixels` |
| History budget counts retained layer images | Done | `layerCommandsCountRetainedImages`, `deletedLayerIsCountedInHistory` |
| Layered project format v2; v1 still opens as one layer; validation before allocation | Done | `layeredRoundTripPreservesEverything`, `version1ProjectLoadsAsOneLayer`, `rejectsDamagedLayeredFiles` (13 cases), `layeredSaveAndOpenPreserveStructure`, `olderSingleLayerProjectOpensAsOneLayer` |
| PNG export = visible composite | Done | `pngExportOfLayersMatchesVisibleComposite`, `pngExportIsTheVisibleComposite`, `tst_ui` |
| Recovery preserves layers; immutable snapshots; v1 recovery handled explicitly | Done | `layeredRecoveryRestoresStructureAndPixels`, `snapshotIsIsolatedFromLaterEdits`, `legacySingleLayerRecoveryIsReadAsOneLayer`, `legacyRecoveryFileIsOfferedAsOneLayer`; stale-write tests from milestone 2 still pass |
| Measurements for 1/5/10 layers and against the single-layer build | Done | BENCHMARKS.md |
| Android rebuilt | Built, not launched | debug APK signed with the development key |

Not verified for this milestone: an on-screen session with real mouse input on the Windows
desktop (postponed because the desktop was in use; the panel was verified with synthetic input
on the offscreen platform), Android at runtime.

## Milestone 4: responsive file operations and device validation (done; Windows on-screen session pending)

| Criterion | Status | How verified |
|---|---|---|
| Save runs on a worker from an immutable snapshot (`ProjectIo`); the file is replaced atomically | Done | `ProjectFile::save` via `QSaveFile`, unchanged; `bench_canvas` (UI thread ≈ 0.1 ms per save request) |
| Edits made after the snapshot stay unsaved | Done | `editsDuringSaveStayUnsaved` |
| A failed save keeps the previous file, the unsaved state and the path (Save As changes the path only on success) | Done | `failedSaveKeepsFileDirtyStateAndPath`, `failedSaveCancelsNew` (UI) |
| Concurrent saves never write older data over newer | Done | Saves are serialized on one worker; a Save during a save to the same file is queued and taken from a newer snapshot. Save As to another file is refused while busy. `repeatedSaveWritesTheNewestData` |
| New/Open/Close wait for a requested save; failure or cancel keeps the document | Done | `newWaitsForASuccessfulSave`, `failedSaveCancelsNew`, `closeWaitsForTheRunningSave` (UI); Android: Save then New on a content URI |
| Recovery for newer edits is kept after a save | Done | `recoveryKeepsEditsNewerThanTheSave`, `stalledSaveDuringAutosaveIsHarmless` |
| Open decodes into a separate document, replacing the current one only after success and the unsaved-changes decision | Done | `openReplacesOnlyAfterSuccess`, `failedOpenKeepsCurrentDocument` |
| Edits during loading are handled explicitly | Done | Painting and the layer panel are disabled while opening. If the document still changes (from code), the user chooses "Open the file" or "Keep my drawing" (`editsDuringOpenAreNotSilentlyLost`) |
| Busy state shown; conflicting operations prevented | Done | Footer busy indicator and text; menu items disabled; `tst_ui` screenshot `saving_busy` |
| First edit after an autosave measured separately; largest UI stall; 1/5/10 layers split into snapshot, worker, UI stall and total | Done | BENCHMARKS.md, milestone 4 (CPU time, offscreen; not pen latency) |
| Android runtime: drawing, layer panel, picker Save/Open/Export, background/return, recovery after process termination, layered reopen | Done **on an emulator** (Android 16 x86_64), not on a physical device | BUILD.md emulator results. Four issues found and fixed there: hidden footer, Enter in rename, title "10" after Save As, stray tap dismissing the recovery prompt (`strayPressKeepsTheRecoveryPrompt`) |
| Windows on-screen checklist | Written, **not run** | BUILD.md "Testing layers" and "Testing file operations". Desktop automation needs an agreed idle period |
| Linux / iOS | Blocked | Linux: sudo password for packages (Qt fetch prepared). iOS: no macOS/Xcode |

## Milestone 5: hardware validation (next)

- Run the Windows on-screen checklists (layers, file operations) with real mouse and pen input.
- Windows pen device (Windows Ink) and a physical Android tablet with stylus, plus finger touch:
  pressure range, event rate, synthesized mouse events, palm contacts, and the milestone 4
  Android checks repeated on hardware.
- End-to-end latency measurement (needs hardware or a high-speed camera).
- Unblock Linux (WSL packages) and run the same tests.

## Later (unordered)

Background export and incremental recovery writes (only changed layers); more blend modes,
groups, clipping, masks; drag-and-drop layer reordering; canvas rotation and mirroring;
smoothing/stabilizer; brush presets; selection and transform; iOS build (needs macOS and a
licensing decision); Play Store AAB with an upload key and release pipeline.

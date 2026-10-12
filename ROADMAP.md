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

## Milestone 3: raster layers (done on Windows; not committed yet)

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

## Milestone 4: hardware and platform validation (next)

- Windows pen device (Windows Ink) and an Android tablet with stylus, plus finger touch:
  pressure range, event rate, synthesized mouse events, palm contacts.
- First Android launch: drawing, layer panel on a small screen, file dialogs via the storage
  access framework, saving to `content://`, recovery after the OS kills the app in the background.
- On-screen Windows session for the layer panel and end-to-end latency measurement.
- Unblock Linux (WSL packages) and run the same tests.

## Later (unordered)

Background saving/export and incremental recovery writes (only changed layers); more blend modes,
groups, clipping, masks; drag-and-drop layer reordering; canvas rotation and mirroring;
smoothing/stabilizer; brush presets; selection and transform; iOS build (needs macOS and a
licensing decision); Play Store AAB with an upload key and release pipeline.

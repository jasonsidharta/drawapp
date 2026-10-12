# Roadmap

## Milestone 0: drawing prototype (done on Windows)

Window and toolbar; one raster canvas (2048 × 1536); round brush with size and color; eraser to
transparency; mouse, pen and touch input; pressure-controlled size when valid; zoom and pan;
clear. All host criteria passed on Windows 11 except pen pressure on real hardware (synthetic
tablet events only). Details: git history `bb3e9a2` and `tests/tst_canvas.cpp`.

## Milestone 1: undo/redo and explicit save/open (done on Windows)

| Criterion | Status | How verified |
|---|---|---|
| Brush, eraser and clear are each one undo step; redo works | Done | `tst_document` (core), `tst_canvas::strokeEraserAndClearUndoRedo`; manual Ctrl+Z / Ctrl+Y in the deployed app |
| New edit after undo drops redo | Done | `newEditInvalidatesRedo`, `newStrokeAfterUndoDropsRedo` |
| Interrupted strokes behave consistently | Done | `interruptedStrokesBecomeOneStep` (second finger; undo mid-stroke) |
| View changes stay out of history | Done | `viewChangesAreNotUndoable` |
| Memory budget for undo + redo, oldest evicted first | Done | `budgetEvictsOldestUndoEntries`, `budgetCountsRedoEntries`, `editLargerThanBudgetIsNotUndoable` |
| Versioned project format; size, colors, alpha preserved | Done | `roundTripPreservesSizeColorsAndAlpha`, `roundTripThroughFile`, `openedDocumentKeepsItsSize` |
| Damaged/oversized/newer files rejected without losing the open document | Done | `rejectsDamagedFiles` (18 cases), `rejectsMissingAndOversizedFiles`, `failedOpenKeepsCurrentDocument`; manual open of a truncated file |
| Failed save keeps the last valid file and the unsaved state | Done | `failedSaveKeepsPreviousFile`, `failedSaveKeepsDocumentUnsaved` |
| New / Open / Save / Save As, unsaved-changes prompt, prompt on close | Done | `saveOpenAndUnsavedState`; manual: prompt on New and on window close, Cancel/Escape keeps the document, Save from the prompt then continues, Discard closes |
| PNG export at document resolution with transparency, independent of view, not a save | Done | `pngExportMatchesDocumentPixels`, `exportIgnoresViewAndKeepsProjectState`; manual export checked (2048×1536, transparent corner) |
| Android rebuilt; debug APK signed with a development key | Done (build only) | `apksigner verify`: "Android Debug" certificate. **Not launched** on a device or emulator |

Correction to the earlier plan: it said this milestone would mean "kill the app mid-stroke and
lose at most the current stroke". That was wrong. Atomic saving only protects the last saved
file; surviving a crash needs its own recovery design and tests (milestone 2).

## Milestone 2: crash recovery (next; smallest useful step)

- Periodic autosave of the open document to a recovery file in the app's own data folder
  (`QStandardPaths::AppDataLocation`), off the UI thread, only when there are unsaved changes.
- On start, if a recovery file exists from a session that did not exit cleanly, offer to restore it.
- Clean exit and explicit save remove or update the recovery file.

Acceptance: kill the process (Task Manager / `taskkill /F`) at random points during drawing and
saving; on restart, recover the document as of the last autosave (bounded data loss, stated in
seconds), never a damaged document; the user's own saved files are never touched by recovery.

## Milestone 3: hardware input validation

- Windows pen device (Windows Ink) and an Android tablet with stylus, plus finger touch.
- Pressure range, event rate, synthesized mouse events, palm contacts; adjust separation.
- Pressure curve (gamma) setting if needed; end-to-end latency measurements.
- First Android launch: drawing, file dialogs via the storage access framework, saving to
  `content://` locations.
- Unblock Linux (WSL packages) and run the same tests.

## Milestone 4: layers

Multiple raster layers with opacity, visibility and normal/multiply/screen blend; a format
version 2 with a reader for version 1. Likely the point where tiled storage and partial texture
uploads are needed; decide from measurements.

## Later (unordered)

Background saving/export; canvas rotation and mirroring; smoothing/stabilizer; brush presets;
selection and transform; iOS build (needs macOS and a licensing decision); Play Store AAB with an
upload key and release pipeline. Deferred features are listed in PRODUCT.md.

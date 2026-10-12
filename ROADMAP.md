# Roadmap

## Milestone 0: drawing prototype (done on Windows)

Window and toolbar; one raster canvas (2048 × 1536); round brush with size and color; eraser to
transparency; mouse, pen and touch input; pressure-controlled size when valid; zoom and pan;
clear. All host criteria passed on Windows 11 except pen pressure on real hardware (synthetic
tablet events only). Commit `bb3e9a2`.

## Milestone 1: undo/redo and explicit save/open (done on Windows)

Tile-snapshot undo/redo (stroke, eraser stroke, clear) under a 128 MiB budget; versioned
`.drawapp` format with validation; atomic saving; New/Open/Save/Save As with unsaved-changes
prompts; PNG export. Verified by `tst_document`, `tst_canvas` and a manual GUI session. Android
rebuilt with a development-signed debug APK (not launched). Commit `8ccad84`.

Correction recorded there: atomic saving alone does not protect unsaved strokes after a crash;
that is milestone 2.

## Milestone 2: crash recovery (done on Windows)

| Criterion | Status | How verified |
|---|---|---|
| Autosave modified documents to a separate recovery file in the app data folder | Done | `autosaveFollowsUnsavedChanges`; real app wrote `%APPDATA%\DrawApp\recovery\session-*.drawrecovery` |
| Configurable interval, 10 s default | Done | `autosaveInterval` / `DRAWAPP_AUTOSAVE_MS`; real app ticks measured 10 000–10 001 ms apart |
| UI-thread snapshot, worker encode/write, worker never touches the canvas | Done | Design (private `QImage` copy); snapshot 2.1–3.7 ms measured |
| One write at a time; stale writes cannot replace newer data or recreate cleaned-up files | Done | `onlyOneWriteAtATime`, `staleWriteDoesNotRecreateDiscardedFile`, `staleWriteCannotReplaceNewerDocument`, `writeStillRunningWhenDocumentIsReplacedIsDropped` (worker held on a semaphore) |
| Atomic recovery writes; interrupted write keeps the previous recovery | Done | `failedWriteKeepsPreviousRecovery`; 8 forced kills during worst-case writes, every recovery file valid by an independent checker; the kill that left a temporary file was cleaned up on the next start |
| Restore or Discard on start; restored work stays unsaved | Done | `restoreAfterCrash`, `discardRecoveryAfterCrash`; real app: prompt, Restore (title `*manual1.drawapp`), Discard via the button |
| Save / New / Open / Discard / exit handled; cancelled or failed actions keep recovery | Done | `newOpenAndFailedSaveHandleRecovery`, `shutdownKeepsOrRemovesRecovery`, `autosaveFollowsUnsavedChanges`; real normal exit left an empty recovery folder |
| Corrupt recovery files do not block startup | Done | `corruptFilesAreRejected` (10 cases), `damagedOrphansAreSetAside`, `corruptRecoveryDoesNotBlockStartup`; real app showed a warning, moved the file to `.damaged`, kept working |
| User's project file never written by recovery | Done | `projectFileIsNeverTouchedByAutosave`; real kills: SHA-256 of `manual1.drawapp` and `noise.drawapp` unchanged |
| Measured loss window | Done | BENCHMARKS.md: ≤ 9.3 s change-to-recovery observed at 10 s; stroke made 1.7 s before a kill was lost, earlier strokes restored |

Not verified: OS crash or power loss (only process kills), Android/iOS/Linux behaviour.

## Milestone 3: hardware input validation (next)

- Windows pen device (Windows Ink) and an Android tablet with stylus, plus finger touch.
- Pressure range, event rate, synthesized mouse events, palm contacts; adjust separation.
- Pressure curve (gamma) setting if needed; end-to-end latency measurements.
- First Android launch: drawing, file dialogs via the storage access framework, saving to
  `content://` locations, recovery folder in app storage, recovery after the OS kills the app in
  the background.
- Unblock Linux (WSL packages) and run the same tests.

## Milestone 4: layers

Multiple raster layers with opacity, visibility and normal/multiply/screen blend; a format
version 2 with a reader for version 1 (project and recovery files). Likely the point where tiled
storage and partial texture uploads are needed; decide from measurements.

## Later (unordered)

Background saving/export; recovery write cost reduction (dirty tiles only, faster compression);
canvas rotation and mirroring; smoothing/stabilizer; brush presets; selection and transform; iOS
build (needs macOS and a licensing decision); Play Store AAB with an upload key and release
pipeline. Deferred features are listed in PRODUCT.md.

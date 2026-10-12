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

## Milestone 5: Android save safety and brush control (done; device validation pending)

| Criterion | Status | How verified |
|---|---|---|
| Project encoded and verified, and a local copy kept and read back in private storage, before a provider document is touched | Done | `ProjectFile::verify`; `providerWriteInterruptedKeepsTheDrawingAndARecoverableCopy` checks that the recovery file held the exact state when the write began; `writeProjectKeepsTheSavedStateRecoverable` |
| Staging not called atomic replacement; guarantees documented per destination type | Done | ARCHITECTURE.md "Save guarantees" |
| Safe replacement where supported | Local files: atomic (`QSaveFile`). Provider documents: none is available with a single-document grant. A folder grant would allow sibling-and-rename; not built | Provider capabilities probed and logged (emulator: seekable file, flags write/rename/delete) |
| Overwrite risk explained before an explicit overwrite; clear Save Copy path | Done | `providerOverwriteIsConfirmedFirst`, `overwriteOfAProviderDocumentIsConfirmed` (UI); emulator: Cancel, Overwrite and Save a Copy (original's hash unchanged) |
| Local copy kept until the destination is written and read back; failure keeps the drawing and recovery data | Done | `providerFailuresBeforeWritingLeaveTheDocumentUntouched`, `providerWriteInterrupted…`, `providerSaveWithoutRecoveryUsesAStagingCopy` |
| Insufficient space, revoked access, interrupted writes | Done | Fake provider tests for all three; emulator: full storage (refused before writing, hash unchanged) and deleted document. `posix_fallocate` reservation worked on the emulator's provider |
| Large reads and writes off the UI thread | Done | Save, open and now PNG export run on the worker (`exportToAProviderDocument`, `tst_ui` export) |
| Brush opacity, uniform within a stroke; strokes build up | Done | `strokeOpacityIsUniformWithinAStroke`, `separateStrokesBuildUpOpacity`, `translucentStrokesAreUniformAndOneUndoStepEach`; emulator screenshot and exported PNG |
| Pressure-to-size response with live preview, existing pressure policy reused | Done | `pressureResponseCurve`, `penPressureResponseSettings` (synthetic tablet events), `BrushPreview` in the popup (replaced by the brush panel's scratchpad in milestone 6) |
| Smoothing: Off + strength, independent of event rate, clean tail at pen-up, usable without pressure | Done | `smoothingDoesNotDependOnEventRate` (core and canvas), `smoothedStrokeEndsWhereThePenIsLifted`, `penLiftOffEndsTheLine`, `interruptedTouchStrokeIsCompleted`, `dotsAndSharpTurns` |
| Compact brush settings usable with mouse and touch | Done | `brushSettingsPopup` (`tst_ui`, now `brushPanel`), emulator by touch; 32 px slider rows |
| Settings captured at stroke start; one undo step per stroke; layers, erasing, saving and recovery | Done | `brushSettingsAreCapturedAtStrokeStart`, `translucentStrokeOnAPaintedLayer` (eraser too), `translucentPaintingWhileSavingAndAutosaving`, `compositorUndoRestoresExactly` |
| Smoothing overhead and first-stroke-after-autosave stall measured | Done, on a loaded machine | BENCHMARKS.md milestone 5 (CPU time, not pen latency) |
| Physical pen pressure, palm rejection, tablet performance | **Not validated** | Simulated input only; instructions in BUILD.md |
| Windows on-screen session | **Pending** | Short checklist in BUILD.md; needs an agreed idle period |
| Linux / iOS | Blocked | Linux: one `sudo apt install` command for the owner (BUILD.md). iOS: Mac with Xcode 16+ |

## Milestone 6: artist brush system (done; drawing feel unverified on hardware)

| Criterion | Status | How verified |
|---|---|---|
| Research: Krita, PhotoCraft and libmypaint read for concepts, revisions recorded; pencil, ink and smudge traced | Done | `research/brush-system.md` |
| Settings, input samples, stroke processing and UI separated | Done | `BrushSettings`, `StrokeSample`, `StrokeEngine`, `BrushLibrary`/`BrushPanel.qml` (ARCHITECTURE.md) |
| Size, hardness, spacing, opacity, flow; pressure to size, opacity and flow; computed and sampled tips; texture scale and strength; uniform vs build-up | Done | `tst_brush` (settings, mappings, `uniformNeverDarkensAndOpacityAppliesOnce`, `buildUpOnePassDepositsFlow`, `buildUpApproachesButNeverPassesCeiling`, texture tests) |
| Opacity applied once; flow per pass; airbrush buildup from elapsed time, not event count | Done | `pressureOpacityLightensWithoutDoubleOpacity`, `airbrushDependsOnElapsedTimeNotEvents`, `airbrushAdvancesWithoutInputAndCapsStalls`, `airbrushBuildsUpWhileHeldStill` (canvas) |
| Distance spacing with interpolated input; resampling limits documented | Done | `splittingInputGivesSamePixels`, `noGapsOnFastStrokes`, `noStampBandsAlongAStroke`; ARCHITECTURE.md "Spacing" |
| Deterministic per-stroke randomness; settings captured at stroke start | Done | `deterministicReplay`; `brushSettingsAreCapturedAtStrokeStart` |
| Dots, interrupted input, pen-up and cancellation | Done | `dotIsOneFullPass`, `directionalDotStillDraws`, `interruptedTouchStrokeIsCompleted`, `penLiftOffEndsTheLine`, `escapeCancelsTheStrokeInProgress`, `cancelRestoresExactly` |
| Tilt only when reported, with a fallback | Done (synthetic) | `tipFollowsDirectionOrTilt`, `tiltScalesOnlyWhenReported`, `penTiltIsUsedOnlyWhenReported` (synthetic tablet events) |
| Original presets: Sketch Pencil, Clean Ink, Solid Round, Soft Airbrush, Textured Paint, Blend/Smudge, Hard and Soft Eraser; one shared renderer | Done | `builtInPresetsAreValidAndDistinct`, `everyPresetDrawsWithAndWithoutPressure`; brush test sheet |
| Smudge with strength, pickup, active-layer sampling and defined transparency behaviour | Done | `smudgeCarriesColour`, `smudgeTransparencyRules`, `smudgeIsOneUndoStepOnTheActiveLayer` |
| Original tips and textures with provenance | Done | Generated by `BrushTips.cpp`; ARCHITECTURE.md provenance |
| Preset chooser with real thumbnails, favourites, size/opacity controls, basic + advanced settings, live scratchpad, save/duplicate/reset, versioned validated storage | Done | `brushPanel` (`tst_ui`, wide and phone width), `thumbnailsAreDrawnByTheEngine`, `scratchpadNeverTouchesTheDocument`, `presetStoreRoundTrip`, `presetStoreValidation`, `customPresetsSaveRenameRevertDelete`, `unusableBrushFilesAreLeftUntouched` |
| A temporary size never alters a saved preset | Done | `temporarySizeNeverChangesASavedPreset` |
| Active layer, locks and hidden layers, one undo step, dirty state, history budget, save and recovery during strokes, project compatibility | Done | `everyPresetIsOneExactUndoStepOnTheActiveLayer`, `lockedAndHiddenLayersRefuseEveryPreset`, `presetStrokesWhileSavingAndAutosaving`, `airbrushStrokeInProgressIsRecovered`; project format unchanged |
| Selection clipping | Integration point only (no selection feature exists) | `clipMaskLimitsTheStroke`, `strokeClipMaskLimitsEveryKindOfBrush` |
| Bounded buffers, no full-canvas copy per event; strokes without pointer events | Done | `buffersStayBounded`; `CanvasItem::drawStroke` used by the integration tests |
| Brush test sheet inspected; problems fixed | Done | `brush_sheet`. Fixed: pale bristle strokes (flow normalised by tip pass length), dashed pencil (finer paper grain), mesh-like canvas grain. Automated: `noSeamsAtCoverageTileBorders`, `noHalosOnTransparency`, `noStampBandsAlongAStroke` |
| Benchmarks: small and large brushes, 1/5/10 layers, memory, first stroke after autosave | Done (CPU time) | BENCHMARKS.md milestone 6 |
| Windows suites; Android rebuilt and preset UI checked | Done (emulator) | ctest 6/6; emulator by touch. Fixed: arrow glyphs missing in Android's font |
| Physical pen evaluation; comparison with Krita on the same hardware | **Not done: no pen hardware available**. Drawing feel is unverified | Instructions in BUILD.md |

## Milestone 8: selections and basic transforms (done; hardware feel and Krita comparison not run)

| Criterion | Status | How verified |
|---|---|---|
| Research: Krita and PhotoCraft selection and transform code, versions and files recorded | Done | `research/selections-transforms.md` (code reading; neither app could be run here) |
| Document-space soft selection separate from pixels and visibility | Done | `Selection`; `selectionChangesNeverTouchArtwork` |
| Rectangle, ellipse, freehand lasso; Replace/Add/Subtract/Intersect; All, Deselect, Invert, Reselect; anti-aliased | Done | `tst_selection` shape and mode tests (incl. `addingShapesThatShareAnEdgeLeavesNoSeam`), `selectionToolsUnderZoomAndPan` (mouse, modifiers, lasso, click to deselect, Reselect, undo) |
| Outline readable under zoom and pan | Done | Two-tone outline; screenshots `tst_canvas_paint_in_selection`, workflow screenshots. Fixed: a full repaint was being swallowed by a following partial one |
| No selection vs explicitly empty | Done | `noSelectionAndEmptySelectionAreDifferent`, `emptySelectionNeverPaintsEverywhere` |
| Selection undo; not saved in files | Done | `selectionUndoDoesNotMakeTheDocumentUnsaved`; ARCHITECTURE.md (format unchanged) |
| Every preset through the selection; soft edges applied once; smudge samples outside, writes inside | Done | `selectionLimitsEveryPreset`, `paintAndEraseApplyCoverageOnce`, `smudgeWritesOnlyInsideAndAppliesTheEdgeOnce`, `eraserAndSmudgeStopAtTheSelectionEdge`, `paintingIsClippedToTheSelectionUnderZoomAndPan` |
| Move, scale, rotate, flip; handles, pivot, keep ratio, numeric fields, Apply/Cancel, Enter/Esc, touch-sized controls | Done | `tst_selection` transform tests, `transformByHandlesApplyUndoRedo`, the `tst_ui` workflow (handles, Alt, numeric angle, Flip, Enter) and the emulator by touch |
| Preview from the source, no repeated resampling, premultiplied without fringes, non-destructive preview, one undo step, exact Cancel | Done | `identityAndCancelAreExact`, `transparentEdgesKeepTheirColour`, `movesAndFlipsStaySharp`, `transformCancelRestoresExactlyAndUndoCancels` |
| Selection after Apply, pixels beyond the canvas, Save/Open/Close/recovery during a preview defined | Done | `selectionFollowsTheTransformedPixels`, `pixelsMovedOffTheCanvasAreClipped`, `savingAndRecoveryDuringAPreview`; ARCHITECTURE.md |
| Copy, Cut, Paste to a new layer, with and without a selection; undo and dirty state | Done (internal clipboard) | `copyCutAndPaste`; workflow task 4. System clipboard is offered but interchange with other apps is untested |
| Artist tasks 1–7 in our app | Done (synthetic input, offscreen) | `artistTasksWithSelectionsAndTransforms`. Found and fixed: dragging the middle of a small box moved the pivot; corner scaling needed Alt (from centre). Found, not fixed: transforms are one layer at a time |
| The same tasks in Krita; PhotoCraft | **Not run**: Krita is not installed here and PhotoCraft needs a Rust toolchain | Manual checklist in `research/selections-transforms.md` |
| Benchmarks incl. full canvas; no allocations from unchecked scales | Done | BENCHMARKS.md milestone 8; `scaleIsClampedAndWorkIsBoundedByTheCanvas` |
| Windows suites; Android rebuilt and selection/transform exercised on the emulator | Done (emulator) | ctest 7/7; BUILD.md milestone 8 emulator results |
| Physical device | **Not done** | No device; milestone 7 |

## Milestone 9: professional painting engine, Brush Studio and multi-layer editing (done with stated gaps; pen hardware and Krita comparison not run)

| Criterion (milestone 9 section 15) | Status | How verified |
|---|---|---|
| Research: Krita, PhotoCraft, Procreate (documented), decisions, feature parity | Done (source study and documentation; none of the apps run) | `research/milestone9-*.md` |
| Multi-layer workflow: select several layers; transform, copy, cut, paste, clear them together; aligned; one undo step; atomic Apply; locked/hidden refused | Done | `tst_canvas` / `tst_selection` multi-layer tests; workflows 1, 2, 9–12 |
| Existing selections and transforms not regressed | Done | milestone 8 suites and `artistTasksWithSelectionsAndTransforms` pass |
| Polygon lasso, magic wand, feather, grow/shrink, outline transform | Done | `tst_selection`, `tst_canvas` (`polygonLassoByClicks`, `magicWandAndModifyCommands`, …) |
| Modular brush architecture (one engine, stamps, blends, dynamics, registry) | Done | `docs/brush-engine-architecture.md`; `tst_brushengine` |
| Advanced properties change rendering | Done for 94 of the 120 designed properties; 26 planned | `everyImplementedPropertyChangesRendering` |
| Settings documented individually | Done (generated, checked current) | `docs/brush-settings-specification.md`, `specificationIsCurrent` |
| Sensor framework: pressure, tilt X/Y/amount/direction, direction, speed, acceleration, distance, fade, time, random; curves; combination | Done (simulated input) | `dynamics*` tests |
| Wet paint | Prototype only | `wetPaintLoadsDepositsAndDrags`; limits in `docs/milestone9-known-limitations.md` |
| Spray and hatching | Subset | `sprayAndHatching`, golden images |
| Bristle, clone, sketch engines | **Not done** | feature parity document |
| Brush Studio usable with actual input | Done on Windows (synthetic mouse/keyboard) and the Android emulator (touch) | `tst_ui::brushStudio`, workflows 6–8; `docs/milestone9-testing.md` |
| Presets survive save/load and version checks; v1 migration; unknown keys kept; malformed files refused | Done | `presetFileGolden`, `libraryVersion1Migrates`, `malformedPresetFuzz` |
| Krita `.kpp` / Procreate import | **Not done** (investigated; no compatibility claimed) | `docs/brush-preset-format.md` |
| Benchmarks run and reported | Done (Windows CPU times) | `docs/milestone9-performance.md`, BENCHMARKS.md |
| 4096 × 4096 × 20 workload | **Refused by design** (512 MiB limit); measured at 8 layers | same |
| 60 fps transform previews on large multi-layer documents | **Not reached** at 4096² (4–7 fps); 30–40 fps at 2048 × 1536 × 9 | same |
| Windows automated tests | Done: 8 suites, 337 test cases, 0 failures | `docs/milestone9-testing.md` |
| Android builds and interaction | Done (emulator, x86_64; arm64 APK built, not run) | BUILD.md |
| Artist workflow tests | Done (automated, synthetic input, simulated pressure) | `docs/milestone9-artist-workflows.md` |
| Sanitizers | **Not run** (not available with MinGW) | `docs/milestone9-testing.md` |
| Unresolved data-loss or critical painting bug | None known. Two found and fixed: preview thread race, 1 GiB read reservation | commits; tests |
| Physical pen, palm rejection, tablet performance | **Not done** (no hardware) | milestone 7 |
| Krita and PhotoCraft comparison | **Not run** (not installed / no Rust toolchain); manual procedure written | `docs/milestone9-performance.md` |

## Milestone 10: sparse tiled storage, memory budget and asynchronous rendering (in progress)

Plan and order in the milestone 10 specification; research in `research/m10-*.md`. Done so far
(local commits): baseline reproduced; research; `MemoryBudget` and `TiledImage` (64-pixel
copy-on-write tiles, chosen by measurement); layers, composite, brushes, undo records, selection
edits, transforms, clipboard and recovery snapshots on tiled storage; project format 3 (sparse
tiles; versions 1 and 2 still read); a view pyramid for zoomed-out views; Apply on a worker thread
with progress, cancellation, generations and atomic commit; memory-aware undo (budget from the
memory limit, eviction under pressure, confirmation before non-undoable actions), a memory
report and refusal messages; compatibility with files written by milestones 1 and 9, recovery
and fuzzing (`docs/m10-file-compatibility.md`). Remaining: a reduced-resolution transform preview,
benchmarks of all workloads, Android validation, the artist workflows and the reports.

## Milestone 7: hardware validation (outstanding: no pen hardware yet)

- Run the short Windows on-screen checklist with real mouse input (and a pen if available).
- A physical Android tablet with stylus:
  - pen pressure range and response settings, palm contacts, brush settings by finger;
  - the brush presets' feel (pressure and tilt), side by side with Krita's presets on the same
    device;
  - saving to Downloads and to a cloud provider (overwrite prompt, Save a Copy), lifecycle and
    recovery;
  - repeat the milestone 4–6 emulator checks on hardware.
- End-to-end latency measurement (needs hardware or a high-speed camera), including how the
  smoothing's spatial lag feels.
- Linux once the packages are installed; iOS when a Mac is available.

## Later (unordered)

Folder-based saving with sibling-and-rename on providers that allow it; incremental recovery
writes (only changed layers); the 26 planned brush properties (`docs/brush-settings-specification.md`);
bristle, clone and sketch engines; a real wet-media model; more layer blend modes, groups, clipping, masks; drag-and-drop layer reordering; canvas rotation
and mirroring; perspective/warp transforms; iOS build (needs macOS and a licensing decision);
Play Store AAB with an upload key and release pipeline.

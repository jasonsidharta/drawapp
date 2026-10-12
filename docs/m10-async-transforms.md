# Milestone 10: transform Apply in the background

Code: `src/core/TransformApply.h/.cpp`, `src/core/TransformSession.*` (render with cancellation),
`src/app/CanvasItemSelection.cpp` (`applyTransformWith`, `commitApply`, `cancelApply`),
`src/app/Main.qml` (progress and Stop). Rules: `research/m10-concurrency-design.md`. Tests:
`tst_canvas` (Apply section).

## Behaviour

- **Small transforms** (changed pixels × layers below 4 Mpx) apply at once, as before.
- **Large transforms** apply on a worker thread. The toolbar shows "Applying… N %" with a progress
  bar and **Stop**. The preview stays on screen until the result is committed.
- **While applying**, painting, selections, layer actions, undo and redo, file operations, tool
  changes and new transforms are refused with "The transform is still being applied. Wait for it
  to finish, or cancel it (Esc)." Pan and zoom work.
- **Stop** (or Esc) cancels: the document is unchanged and the preview stays open; Cancel again
  closes it, Apply tries again.
- **Implicit applies** (switching tools with a transform open) still apply at once; if a
  background Apply is running they wait for its result first.
- **Out of memory** on the worker: nothing changes, a notice says so, the preview stays open.
- **Cannot be undone**: if the undo record would be larger than the whole undo history, Apply asks
  first (`docs/m10-undo-architecture.md`).

## How it works

1. On the UI thread: the session's `workerCopy()` (shares every cache, does not own the memory
   reservation), a copy of each layer (shares its tiles), each layer's version, the changed
   rectangle. The UI thread's evictors are asked for room for the new tiles.
2. On the worker (`TransformApply::run`): for each layer, the result starts as the copy of the
   layer; `TransformSession::render` writes the changed area band by band (four tile rows, rows
   of each band in parallel on the transform row pool), copying each touched tile first because
   it is shared with the live layer. The cancel flag is checked between bands and before each
   layer; progress counts rows.
3. Delivery: the result is stored under a mutex and a queued call reaches the UI thread. It is
   delivered only if its **generation** is still current (cancel and restart bump it).
4. Commit on the UI thread: if every layer's version is unchanged, the old tiles of the changed
   area are captured for undo (references), the layer is replaced by the result (pointer
   copies), the selection follows the pixels, one group undo step is pushed and the composite
   refreshed. This step cannot fail half-way.
5. Destroying the canvas cancels the job and joins the thread; the worker owns copies of
   everything it reads.

## Tests

| Test | What it checks |
|---|---|
| `backgroundApplyEqualsImmediateApply` | same layers as applying at once; one undo step that restores them |
| `editsWaitWhileApplying` | strokes, undo, layer actions, selections, transform edits, tool changes, saving and a new transform are refused with the message; zoom works; nothing changed |
| `cancelApplyAtEveryStage` (4 rows) | Stop before the first layer, between layers, after the worker finished but before delivery, and with Esc: document, history and composite unchanged, preview open, a later Apply works |
| `failedBackgroundApplyChangesNothing` | memory failure on the worker |
| `implicitApplyWaitsForTheResult` | tool switch applies at once |
| `destroyingTheCanvasDuringApplyIsSafe` | canvas destroyed with the worker held at a gate |
| `staleResultsAreDiscarded` | a superseded job delivers nothing; worker and immediate results equal; the live layer untouched |
| `applyJobStress` | 150 rounds of start / cancel at random times / supersede / finish now, while the UI thread reads the live layer; no change, equal results, nothing left allocated (passed five runs in a row) |

## Not done

- **Reduced-resolution interactive preview** (rendering the drag preview at a pyramid level, as
  Krita's level-of-detail transform). The preview still renders at full resolution over the
  changed area; its cost is measured in `docs/m10-performance.md`.
- Implicit applies still block the UI thread for large transforms.
- No pause or partial commit; a cancelled Apply starts from the beginning.

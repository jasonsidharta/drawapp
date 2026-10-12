# Milestone 10: the memory budget

Code: `src/core/MemoryBudget.h/.cpp`. Tests: `tst_tiles` (budget and eviction), `tst_document`
(opening under pressure), `tst_canvas` (undo eviction, export refusal, confirmations), `tst_ui`
(the dialogs).

## What replaced the 512 MiB limit

Milestone 9 refused any document whose layers would exceed 512 MiB **if they were dense**
(42 layers at 2048 × 1536, 8 at 4096², 2 at 8192²), whatever was painted. Milestone 10 counts what
is actually allocated:

- `MemoryBudget` is a process-wide account of pixel memory in five **categories**: Document
  (layer tiles, also when only undo history holds them), Composite, Cache (view pyramid and its
  drawing buffer), Preview (transform caches and merged previews) and Temporary (short-lived
  dense buffers: flattening for export, region copies).
- **Limit**: half of physical memory on desktop, a quarter on Android
  (`MemoryBudget::defaultLimit`), overridable with the environment variable
  `DRAWAPP_MEMORY_LIMIT_MB` (testing and constrained machines). On the development machine
  (15.4 GB) the limit is 7870 MiB.
- Every tile allocation and every large buffer asks `tryReserve(bytes, category)` first. If the
  total would pass the limit, the **evictors** registered by the calling thread run, in order:
  view caches first (order 0), then the undo history (order 10, oldest steps first). If there is
  still not enough, the reservation fails and the caller fails cleanly (`docs/m10-tile-storage.md`,
  "Failures").
- Evictors run only on the thread that registered them (the UI thread), so a worker never frees
  memory the UI thread is using. A background Apply asks the UI thread's evictors for room before
  it starts.
- The layer count is fixed at 64 at any canvas size; canvases are still at most 16384 per side.

## What the artist sees

- **Memory usage** (File menu): the limit, use by category, the undo history and the peak. It
  counts pixel data only; the program itself, fonts and the window need more (about 75 MiB
  private at start-up, `docs/m10-performance.md`).
- A **notice** when old undo steps were given up to make room, when a stroke was taken back,
  when an export or a transform does not fit, and when the view could not be fully updated.
- A **question** before an action whose undo record would not fit in the undo history (Apply,
  Clear, Delete layer), offering to continue without undo or cancel
  (`docs/m10-undo-architecture.md`).

## What is not counted

- Process overhead: code, Qt, fonts, QML, the window's own buffers (the painted item's texture).
- `QImage` buffers made outside the budget by Qt itself (for example while encoding PNG).
- The selection mask (dense, one byte per pixel). It is small at ordinary sizes but 256 MiB at
  16384².

So the budget limits DrawApp's pixel data, not the process size; `docs/m10-performance.md`
reports both.

## Choices and alternatives

- Krita uses a memory limit with swapping of tiles to disk; DrawApp does not swap (decided in
  `research/m10-rendering-architecture.md`): on Android the app can be killed rather than paged,
  and a swap file needs its own recovery story. Refusing cleanly with a message was chosen
  instead.
- PhotoCraft returns an allocation error from every tile access (`AllocationError`); DrawApp does
  the same through `nullptr` from `writableTileData` and `false` from operations.

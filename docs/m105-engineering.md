# Milestone 10.5 engineering record

Starting revision: `dcde3838a68d1fce905cd9d6e90d18f79b56bd23`, branch
`m10-preview`, clean working tree. Work is confined to `DrawApp-Codex`.
Builds and generated artifacts use `build/codex-m105-*`; no build or emulator
owned by the other worktree is changed. Historical milestone 10 measurements
remain in their original documents.

## Findings verified against the starting revision

| Function | Classification | Failure condition / expected behavior |
|---|---|---|
| `CanvasItem::commitApply` | Confirmed | Snapshot capture, vector growth, history insertion and composite refresh can fail after a live layer changes. Prepare all state before committing. |
| `TransformSession::begin` | Confirmed | Manual reservation has no destructor owner; exceptions bypass `end`. Worker copies depend on parent lifetime. Cache buffers must own their charges. |
| `TransformSession::begin` | Confirmed | `row.data() - rect.left()` forms an out-of-array pointer for a nonzero crop origin. Use relative indexing. |
| `TransformSession::transformedSelection` | Confirmed | Null allocation is not checked before storage access; a failed mask can become no selection. Refuse without changing the selection. |
| `TransformApply::start/runGated` | Confirmed | Allocation exceptions escape the thread entry point. Return a failure result and release partial output. |
| `TiledImage::copy` | Confirmed accounting gap | Reservation is released before allocation; multiple surviving dense copies bypass the limit. Charge storage until its last reader releases it. |
| `ViewPyramid::pixels` | Confirmed | Scratch-growth reservation may evict the same scratch, invalidating the delta calculation. Allocate against a stable ownership state. |
| `CanvasItem::applyTransformWith` | Confirmed | Unconditional `evict(output size)` can discard history even when output fits. Failed or cancelled preparation must preserve history. |
| `UndoHistory::shed` | Confirmed | Logical history bytes include shared tiles; they are not necessarily freed. Use reclaimable ownership and actual released bytes. |

## Status

Correctness gate passed on Windows: all 10 CTest suites, 65.91 seconds,
MinGW GCC 15.1.0 / Qt 6.12.0 Release, `build/codex-m105-release`.
Brush golden files are unchanged. The transform preview screenshot was inspected.
Android and performance validation remain pending.

## Correctness implementation

- `BudgetImage` ties dense-buffer charges to QImage storage cleanup. Read-only
  worker copies retain the same charge without double release. Transform source,
  base, reduction, mask and band buffers use it; dense tiled-image copies do too.
  A caller making a mutable detached copy must allocate it through this helper.
- Apply prepares a private LayeredDocument, selection, tile snapshots, group command
  and refreshed composite. History insertion allocates before changing either
  history branch. Publishing the document is a compile-time-checked noexcept move.
- Transform preparation only evicts reconstructible caches and only for the
  estimated shortfall. It deliberately refuses when preserving history leaves
  insufficient room. This conservative mitigation avoids irreversible history loss
  for operations later cancelled or refused; it does not promise maximum admission.
- General history eviction counts only exclusively owned snapshot tiles. Shared
  tiles, layer-presence images and selection masks are conservatively excluded
  from the preflight reclaim estimate. Multi-entry shared ownership may therefore
  refuse an allocation that more elaborate analysis could admit.
- Worker render exceptions become failed results; partial results are dropped.
  Queue allocation failure leaves a result available to the UI progress poll.
- Scratch growth drops the old buffer before reserving the replacement, so its
  own evictor cannot invalidate a reservation delta. Scratch views remain borrowed
  until the next call, as specified by ViewPyramid's API.
- Transform row bands are 128 rows for more frequent cancellation checks.

New regression coverage: `tst_tiles::denseCopyKeepsItsReservation`,
`transformCacheLifetimeAndFailures` (20 allocation positions plus surviving worker
copy), `selectionAllocationFailureIsExplicit`,
`pyramidGrowthDuringEvictionBalancesBudget`, `sharedUndoCannotFundReservations`;
`tst_canvas::applyPreparationIsAtomic` (five preparation boundaries, unchanged
pixels/composite/selection/history followed by successful undo/redo), and
`workerExceptionsBecomeFailures`. Existing tests cover background cancellation,
supersession, destruction, blocked edits, save/reopen and format compatibility.

The review findings were confirmed; none of the eight named areas was dismissed
as a false positive. OS-wide allocator exhaustion and race-detector coverage remain
separate limitations from deterministic budget and transaction checkpoint tests.

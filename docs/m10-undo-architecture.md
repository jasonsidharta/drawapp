# Milestone 10: undo on tiled storage

Code: `src/core/UndoHistory.*`, `src/core/TileSnapshot.*`, `src/app/CanvasItem.cpp`
(`commitPixels`, `pushCommand`, `confirmIfNotUndoable`, the undo evictor). Tests: `tst_document`
(budget, blank-tile undo), `tst_canvas` (eviction, confirmations), `tst_ui` (dialog).

## Records

Unchanged command types (pixels, layer presence, properties, move, selection, groups); what
changed is what a pixel record holds:

| | Milestone 9 | Milestone 10 |
|---|---|---|
| Pixel record | copies of the 64 × 64 tiles an edit touched | references to the tiles as they were |
| Cost of capturing | a 16 KiB copy per touched tile, even if it was empty | none; the layer copies a tile when it writes to it |
| Stroke over unpainted canvas | 16 KiB per touched tile | nothing ("was transparent") |
| Undo / redo | swaps pixels | exchanges references |
| Deleted layer kept by history | 12 MiB at 2048 × 1536 whatever was painted | its painted tiles |
| Unchanged tiles | dropped by comparing pixels | dropped if still shared, else compared |

Erased tiles are released after a stroke before unchanged tiles are dropped, so erasing to
transparent frees memory and the undo record keeps the erased pixels.

## Budget

- **Size**: a quarter of the memory limit, between 32 MiB and 2 GiB
  (`UndoHistory::budgetForLimit`): 1968 MiB on the development machine (7870 MiB limit), about
  256 MiB on a phone with a 1 GiB limit. Milestone 9 used a fixed 128 MiB.
- **Counting**: the tiles each entry keeps alive; a tile the layer still shares is not counted.
  Recomputed after every push, undo and redo; over budget, the oldest undo entries go first, then
  the farthest redo entries (milestone 1 rule).
- **Memory pressure**: the history is an evictor of `MemoryBudget` that runs after the view
  caches; it gives up the oldest steps and the canvas says so ("Memory is running low: the N
  oldest undo step(s) were removed to make room.").

## Non-undoable actions

An edit larger than the whole budget cannot be kept. Where the cost is known before acting, the
artist is asked first:

| Action | Estimate | Behaviour |
|---|---|---|
| Apply (button or Enter) | painted tiles of the transformed layers in the changed area | dialog "This cannot be undone": Continue / Cancel |
| Clear (selection or layer) | painted tiles of the selected layers in the area | same |
| Delete layer | the layer's painted tiles | same |
| Strokes, implicit applies | not known in advance | applied, then a notice says it cannot be undone (milestone 9 behaviour) |

The estimate is an upper bound (it counts every painted tile in the area), so the question can
appear for an edit that would just fit; it never fails to appear for one that would not.

## Known limits

- History lives in memory only (no disk swap); under pressure old steps are dropped, not saved.
- Selection-only steps keep a dense selection mask copy (one byte per pixel).

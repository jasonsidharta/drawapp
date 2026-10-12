# Milestone 10: sparse tiled storage

Code: `src/core/TiledImage.h/.cpp`, `src/core/TileSnapshot.*`, `src/core/StrokeSurface.h`,
`src/core/DocumentData.cpp` (compositing). Research: `research/m10-krita-tile-engine.md`,
`research/m10-photocraft-memory.md`, `research/m10-rendering-architecture.md`. Tests:
`tests/tst_tiles.cpp`, `tst_brushengine::tiledLayersPaintExactlyLikeDenseImages`.

## Model

- A **layer** is a `TiledImage` of the canvas size, divided into 64 × 64 tiles of premultiplied
  ARGB32 (16 KiB each). A **missing tile is transparent** and takes no memory; tiles are
  allocated the first time something non-transparent is written to them.
- The tile table is a dense grid (`QList<TileRef>`), implicitly shared, so copying a layer copies
  one pointer. Each tile (`TileBlock`) carries an atomic owner count. A writer that finds a tile
  shared copies that tile first (copy-on-write per tile); a writer that finds the table shared
  detaches the table first (pointer copies).
- Every tile is allocated through `MemoryBudget::tryReserve` in its category (document layers,
  composite, view caches, previews). If the budget refuses, the write fails and the operation
  that asked fails without changing anything (see "Failures").
- Tiles erased to fully transparent are released after a stroke (`pruneTransparent`), and by
  clears and transforms, so erasing gives memory back.
- Each tile has a **stamp**, a process-unique number renewed whenever the tile is handed out for
  writing. Caches derived from tiles (the view pyramid) compare stamps instead of pixels.

## Why a dense grid of 64-pixel tiles

Measured with `bench_tiles` (`docs/benchmarks/m10-bench_tiles.md`), 64 / 128 / 256 pixels:

| | 64 | 128 | 256 |
|---|---|---|---|
| 3000 small dabs, median | 3.86 ms | 3.94 ms | 3.97 ms |
| 10 full layers composited | 17.3 ms | 15.9 ms | 18.6 ms |
| 40 strokes on 16384² (dense: 1024 MiB) | **334 MiB** | 468 MiB | 592 MiB |
| undo copy for one stroke on a full layer | **1.9 MiB** | 3.2 MiB | 5.3 MiB |
| a 1-pixel diagonal across 2048 × 1536 | **0.75 MiB** | 1.5 MiB | 3.0 MiB |

Times are within about 10 % of each other; memory is not, so 64 (as Krita). A hash map of tiles
(Krita) was not needed: the largest canvas, 16384², has 65 536 tiles, so the grid is 512 KiB of
pointers per layer and every lookup is an index.

## How each part uses it

| Part | On tiles |
|---|---|
| Brushes (`StrokeEngine`) | paint a `StrokeSurface`: row pointers valid to the end of a 64-pixel segment (as libmypaint's tile requests). The same code paints dense images (scratchpad, thumbnails, brush sheet). Exactly the same pixels on both, tested for every built-in brush across seams, on transparency and with a soft selection; 16 golden images of strokes along tile seams. |
| Undo (`TileSnapshot`) | keeps **references** to the old tiles, not copies. Capture is free; the layer copies a tile when it first writes to it, so only changed tiles exist twice. A stroke over unpainted tiles records "was transparent": no memory. Undo and redo exchange references. |
| Composite | per tile: the visible layers' tiles drawn with `QPainter` and their opacity (the milestone 9 operations, so the same pixels); a tile no visible layer covers is released. |
| View | `ViewPyramid` (see `ARCHITECTURE.md`, "View"). |
| Transforms | the session keeps the layers as snapshots (no copy); dense caches only over the source rectangle; Apply writes into copies of the layers and commits by exchanging tile references. |
| Selection edits, clipboard, magic wand | per tile, reading through the tiled accessors. |
| Recovery | the snapshot shares tiles with the live document; the UI thread's next writes copy only the tiles they touch. |
| Files | format 3 stores only painted tiles (`docs/m10-file-compatibility.md`). |

## Thread safety

Rules in `research/m10-concurrency-design.md`: only the UI thread writes live tiles; workers get
copies and read tiles that, being shared, are never written; uniqueness is checked with an
acquire load of the owner count. Tested by `tst_tiles` (concurrent readers while a writer edits
copies, isolated writers) and the background Apply tests. Sanitizers: see
`docs/m10-test-results.md`.

## Failures

Every allocation can fail. What happens:

| Operation | Result when a tile cannot be had |
|---|---|
| Stroke | stopped and taken back whole, with a notice |
| Clear, Paste, transform begin and Apply | nothing changes, with a notice; the transform preview stays open |
| Opening a project | refused with a message, nothing kept |
| Composite update | the view may be stale in that area; a notice says so; the document is unchanged |
| View pyramid | a coarser level is drawn, or only the checkerboard |

## Limits

- At most 16384 pixels per side and 64 layers (unchanged); memory is bounded by `MemoryBudget`,
  not by a fixed document size.
- The selection mask is still dense: one byte per canvas pixel (256 MiB at 16384²).
- Tiles are not compressed in memory and are never swapped to disk (Krita does both). A fully
  painted layer uses the same 4 bytes per pixel as before.

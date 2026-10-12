# Krita tile engine (milestone 10 research)

Krita is GPL; it was read for concepts only. No code was copied, and DrawApp's implementation is
written independently.

## Sources and revisions

| Revision | How it was read |
|---|---|
| `69a4ea48fa88c136c1166eb59dddf894c149d9f8` ("Add pin to timeline icon"), upstream `HEAD` on 2026-10-12 per `git ls-remote https://invent.kde.org/graphics/krita.git` | `git fetch --depth=1` into the blobless partial clone used in milestone 9; files read with `git show 69a4ea4:<path>`. **All paths and line numbers below are at this revision** |
| `e579e3a4` (milestone 9 research) | The milestone 9 clone. Not re-read file by file: the tile engine was not part of milestone 9 |
| `25840ee8` (milestone 6 research) | Local checkout; `libs/image/tiles3/` has the same file list |

Nothing was built or run: Krita is not installed on this machine.

**Observed** = read in the source at the given location. **Inference** = our interpretation,
marked as such.

## 1. Tile-based storage and tile size

- **Observed.** `libs/image/tiles3/kis_tile_data_interface.h:24-25` defines
  `__TILE_DATA_WIDTH 64` and `__TILE_DATA_HEIGHT 64`. Every paint device is stored as 64 × 64
  tiles, whatever its colour space; the bytes per tile scale with `m_pixelSize` (line 305).
- **Relevance.** Krita settled on 64 × 64 for paint devices. DrawApp's undo and stroke code already
  uses 64 × 64 (`TileSnapshot::TileSize`, `StrokeEngine::Tile`). Milestone 10 benchmarks 64, 128
  and 256 before choosing (`docs/m10-tile-storage.md`).

## 2. Sparse allocation and the default tile

- **Observed.** `kis_tiled_data_manager.h:105-121` (`getTile`).
  - A writable request calls `m_hashTable->getTileLazy(col, row, newTile)`, which creates the
    tile on demand, and then notifies the extent manager (`m_extentManager.notifyTileAdded`).
  - A read-only request calls `getReadOnlyTileLazy`, which returns a tile backed by the shared
    **default tile data** without inserting anything.
  - `setDefaultPixel` / `defaultPixel()` (lines 80-82) define what unallocated space reads as.
- **Observed.** `kis_tiled_data_manager.cc:263`, `KisTiledDataManager::purge(const QRect&)`. It compares every tile in an area with the default tile data
  (`memcmp(defaultData, tile->data(), tileDataSize)`) and deletes equal tiles from the hash table
  (`m_hashTable->deleteTile`, `notifyTileRemoved`). The caller decides when it is safe to purge.
- **Relevance.**
  - Adopted: unallocated tiles read as the default (transparent).
  - Adopted: writing allocates only the touched tile.
  - Adopted: reclaiming fully transparent tiles is an explicit operation run at safe points,
    after a stroke or a transform, not on every write.

## 3. Copy-on-write and tile locking

- **Observed.** `kis_tile_data_interface.h:114-137`.
  - `acquire()` / `release()` maintain `m_usersCount` (line 296: "How many tiles/mementoes use
    this tiledata through COW?").
  - `ref()` / `deref()` maintain a separate shared-pointer count `m_refCount` (line 301).
  - `clone()` (line 142) duplicates tile data, possibly from a pre-made clone stack
    (`m_clonesStack`, filled by `KisTileDataPooler` in the background, lines 221-224).
- **Observed.** `kis_tile.cc:221`, `KisTile::lockForWrite()` (`lockForRead()` is at line 208).
  - If `lazyCopying()` (more than one user), it takes `m_COWMutex` and re-checks; if still
    shared, it clones the data, `acquire()`s the clone, swaps it in and
    `safeReleaseOldTileData(old)`.
  - It registers the change with the memento manager (`mm->registerTileChange(this)`).
  - `lockForRead()` only blocks swapping (`blockSwapping()`).
- **Relevance.**
  - Adopted (in our own form): one atomic user count per tile, detach-on-write when the count is
    above one, and the uniqueness check made with acquire semantics.
  - Not adopted: the clone pool, swapping and per-tile read/write locks. DrawApp has a single
    writer thread (the UI thread) per live document. Background work only reads immutable
    snapshots, so writers never contend (`research/m10-concurrency-design.md`).

## 4. Hash table and lookup

- **Observed.** `kis_tile_hash_table2.h`: a lock-free hash table (used when
  `USE_LOCK_FREE_HASH_TABLE` is defined; see `kis_memento_manager.h:24-34`). It exists because
  several threads write different parts of one device concurrently in Krita's multithreaded
  strokes.
- **Relevance (inference).** DrawApp's documents are bounded:
  - at most 16 384 × 16 384, so 256 × 256 tile slots of 64 px (65 536 pointers, 512 KiB per
    layer);
  - a dense pointer grid gives O(1) lookup without hashing.

  We adopt a grid and reject the hash table. Infinite canvases are not a DrawApp feature.

## 5. Undo storage: mementos

- **Observed.** `kis_memento_manager.h`, `registerTileChange` (comment at the top of the class).
  - "This function is called by a tile, when it gets new tile-data through COW. The Memento
    Manager wraps this tile-data into KisMementoItem … When commit() comes, KisMementoItem grabs
    tileData->m_usersCount and since that moment it is a rightful co-owner of the tileData and
    COW participant. It means that tileData won't be ever changed since then."
  - `commit()`, `rollback(KisMementoSP)` and `rollforward` operate on the hash table
    (`kis_tiled_data_manager.h:134-175`). Undo restores the previous tile *pointers*; the old
    default pixel is saved too.
- **Relevance.** Undo history in DrawApp will hold **references** to the old tiles instead of
  copies:
  - capturing a tile before a change costs a pointer;
  - the first write then copies the tile once, as copy-on-write;
  - undo and redo exchange pointers.

  This matches Krita's model without the memento manager's global registration. Each DrawApp edit
  already knows the area it touches (`TileSnapshot::capture`). Adopted:
  `docs/m10-undo-architecture.md`.

## 6. Memory budget, swapping and eviction

- **Observed.** `libs/image/kis_image_config.cpp:172-193`.
  - `tilesHardLimit = totalRAM × memoryHardLimitPercent × (1 − memoryPoolLimitPercent)`.
  - `tilesSoftLimit = tilesHardLimit × memorySoftLimitPercent`.
  - Defaults (lines 195-225): hard limit **50 %** of RAM, soft limit **2 %** of the hard limit,
    pool **0 %**.
  - `maxSwapSize` 4096 MiB (line 141); swap slabs of 64 MiB (line 152).
- **Observed.** `tiles3/swap/kis_tile_data_swapper.cpp:94-130`. Above the soft limit tiles are
  swapped out to disk, and above the "emergency threshold" (line 95) regular threads must wait.
  `swapOutFirst` (lines 154 and 178) picks which tiles go first by age.
- **Relevance.**
  - Adopted: a budget defined as a share of physical RAM. DrawApp's default is 50 % of physical
    memory, capped by the address space on 32-bit targets.
  - Adopted: "evict reconstructible data before refusing".
  - Not adopted: swapping to disk. It is a large subsystem, and on Android the app may be killed
    in the background anyway. DrawApp refuses an allocation that would exceed the budget, with a
    message, after evicting caches (`docs/m10-memory-budget.md`).

## 7. Projection and dirty-region propagation

- **Observed.** `libs/image/kis_simple_update_queue.cpp:65-69` and around line 292. Update
  requests are split into patches of `updatePatchWidth × updatePatchHeight`, 512 × 512 by
  default (`kis_image_config.cpp:89-110`), and similar requests are merged (`tryMergeJob`,
  line 197).
- **Observed.** `kis_async_merger.cpp:172-240` (`startMerge`). For a dirty rect, each layer's
  `projectionPlane()->recalculate(applyRect, …)` composites into the parent projection. This
  runs on the updater threads, not the GUI thread.
- **Relevance.**
  - Adopted: the composite (projection) is kept per tile, and an edit invalidates only the tiles
    it touched.
  - Not adopted: the asynchronous scheduler for painting updates. Measured composite costs for a
    stroke event are below 0.1 ms at 2048 × 1536 × 10 (`docs/benchmarks/m10-baseline-bench_m9.md`),
    so splitting them across threads would add latency, not remove it. Large rebuilds (layer
    visibility on 4096² × 20) are the exception; see `research/m10-performance-hypotheses.md`, H5.

## 8. Canvas display and levels of detail

- **Observed.** `libs/ui/opengl/kis_opengl_image_textures.cpp:114-216`. The OpenGL canvas keeps
  the image as GPU texture tiles of `textureSize` (default 256: `libs/ui/kis_config.cc:1135`,
  `readEntry("textureSize", 256)`), updated per dirty rect through `updateCache(rect, …)`
  (line 274).
- **Observed.** `plugins/tools/tool_transform2/strokes/inplace_transform_stroke_strategy.cpp:270-284`
  (`calculatePreferredLevelOfDetail`).
  - The transform preview runs at a reduced level of detail: `zoom = max(1, maxDimension / 2000)`
    and `lod = ceil(log2(zoom))`. A 4096-wide source is previewed at LoD 2 (1/4 resolution).
  - The full-resolution result is computed separately (`previewLevelOfDetail` at lines 581-641,
    `KisLodTransform`).
- **Relevance.**
  - Adopted: a resolution pyramid of the composite for zoomed-out display.
  - Adopted: reduced-resolution transform previews tied to the view's resolution. Our rule follows
    the zoom rather than a fixed 2000 px, because the preview only needs screen resolution.
  - Not adopted: an OpenGL texture canvas. DrawApp draws with QPainter into a `QQuickPaintedItem`,
    and moving to a texture canvas is outside this milestone (`research/m10-rendering-architecture.md`).

## 9. Transform Apply

- **Observed.** `inplace_transform_stroke_strategy.cpp`.
  - Jobs are queued as `KisStrokeJobData::CONCURRENT`, `SEQUENTIAL` and `BARRIER` (lines 202,
    444-445).
  - The transform runs in the stroke system's worker threads; the GUI only receives updates.
  - `cancelStrokeCallback()` (line 662) undoes all commands (`undoAllCommands`, line 713), and the
    stroke records undo commands for each node.
  - `kis_transform_worker.cc` performs the actual resampling per paint device.
- **Relevance.**
  - Adopted: Apply computed on a worker from immutable inputs, with cancellation, and only the
    finished result committed on the UI thread.
  - Our commit is an O(tiles) pointer swap, so there is nothing to undo on cancel
    (`docs/m10-async-transforms.md`).

## 10. Selection masks and pixel formats

- **Observed.** `libs/image/kis_pixel_selection.cpp:56`. A pixel selection is a `KisPaintDevice`
  in `KoColorSpaceRegistry::instance()->alpha8()`, so it is tiled and sparse like any layer.
- **Observed.** Tile size in bytes is `WIDTH × HEIGHT × pixelSize`
  (`kis_tiled_data_manager.cc`, `purge`: `tileDataSize = HEIGHT * WIDTH * pixelSize()`). The
  tile engine is format-agnostic, and colour spaces live above it.
- **Relevance.**
  - Adopted: tiles carry a byte size per pixel, so formats other than 8-bit premultiplied RGBA
    can be added later without changing the tile engine.
  - **Not adopted in milestone 10:** DrawApp selections stay a dense 8-bit mask the size of the
    document. On a 16 384² canvas that is 256 MiB, counted by the budget
    (`docs/m10-known-limitations.md`).

## 11. Dab caching

- **Observed.** `plugins/paintops/libpaintop/kis_dab_cache.cpp:59-61, 125, 197`. Dabs are reused
  when their parameters repeat (`fetchDabGenerationInfo`), with a separate original kept for
  texture and sharpness options.
- **Relevance (inference).** DrawApp's dabs are evaluated per pixel into a coverage map
  (milestones 6–9). The per-event cost is already small at typical sizes. A dab cache helps
  mainly with identical repeated stamps, which jitter and dynamics prevent. Not adopted in
  milestone 10; recorded as a hypothesis to measure (H8).

## 12. Large documents

- **Inference.** Krita handles large documents through four mechanisms working together:
  sparse 64 px tiles, COW, swapping beyond a soft limit, and LoD previews. DrawApp milestone 10
  adopts:
  - sparse tiles;
  - COW;
  - budgeted allocation with refusal instead of swapping;
  - a display pyramid and reduced-resolution previews.

  Swapping and multithreaded painting are deliberately left out (see the hypotheses document for
  the reasons and the measurements that would change that).

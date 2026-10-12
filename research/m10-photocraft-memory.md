# PhotoCraft memory and tiles (milestone 10 research)

Static source analysis only. PhotoCraft is Rust, and no Rust toolchain is installed on this
machine, so nothing was compiled or run, and no runtime behaviour is claimed.

## Revisions

| Revision | Notes |
|---|---|
| `d43db3e153c76aefce9b32cef9099e458eaf17a8` ("Release 0.8.2: web build back under the wasm size gate"), upstream `HEAD` on 2026-10-12 per `git ls-remote https://github.com/storytold/photocraft` | Fetched (`--depth=1`) into the milestone 9 clone. **All paths below are at this revision** |
| `8b216216` | Milestone 9 research |
| `20cb95bc` | Milestone 8 research (local checkout) |

## 1. Copy-on-write tile storage

- **Observed.** `crates/raster/src/lib.rs`, module comment, lines 1-6:
  - "A [`Surface`] is an infinite plane of 256×256 tiles.";
  - "Missing tiles read as the surface's default pixel";
  - "Tiles are `Arc`-shared, so cloning a surface is O(tiles) and mutation copies only the
    touched tiles. That makes undo snapshots, background jobs and autosave cheap."
- **Observed.** `crates/geom/src/lib.rs:13`: `pub const TILE_SIZE: i32 = 256;`.
- **Observed.** `crates/raster/src/lib.rs`:
  - `struct Surface { format, default_pixel, tiles: BTreeMap<TileCoord, Arc<Tile>> }` (line 87);
  - `tile_mut` (line 173): `entry(c).or_insert_with(... Tile::filled ...)` then `Arc::make_mut`,
    so a missing tile is allocated and a shared one is copied;
  - `prune` (line 263) drops tiles that are entirely the default pixel.
- **Relevance.**
  - Adopted: the same three ideas, in C++ with our own reference count:
    - immutable shared tiles;
    - allocation on first write;
    - explicit pruning.
  - Different: a dense grid instead of a `BTreeMap` (bounded canvases, O(1) lookup), and a tile
    size chosen by our own measurement instead of 256 (`docs/m10-tile-storage.md`).

## 2. Fallible allocation

- **Observed.** `crates/raster/src/lib.rs`:
  - `enum AllocationError { SizeOverflow, NotEnoughMemory { bytes } }` (line 22);
  - `Tile::try_filled` and `Tile::try_clone` use `Vec::try_reserve_exact`;
  - `try_tile_mut` (line 182) reserves storage "before changing the surface, and a shared tile is
    copied only after its replacement has been allocated";
  - `try_read_region` / `try_to_interleaved` reserve output buffers fallibly.
- **Relevance.** Adopted:
  - every tile allocation in DrawApp can fail and returns null;
  - a write that cannot get its tile leaves the surface unchanged;
  - region copies check sizes before allocating.

  This is the behaviour the milestone requires for allocation failures. Our failures come from
  the memory budget and are therefore testable deterministically.

## 3. Ownership and counting shared memory once

- **Observed.** `crates/ops/src/tiles.rs:1-3`: "Tile references owned by history snapshots … Count
  occurrences (including aliases within one document), so removing a state releases a tile only
  after its last reference."
- **Observed.** Lines 39-58 (`Tiles::add` / `remove`). The key is `Arc::as_ptr(tile) as usize`;
  bytes are added on a pointer's first occurrence and removed when its count drops to zero.
- **Observed.** `crates/ops/src/lib.rs:8, 43-48, 212-217`:
  - history memory is "bounded by `max_states` plus an approximate byte budget";
  - `max_bytes` covers "the current document plus the tiles only history holds";
  - `trim` evicts the oldest states.
- **Relevance.** Adopted for DrawApp's memory report:
  - every tile allocation is counted once, globally, when it is created and destroyed;
  - history counts only tiles that the current document no longer uses.

  This answers the milestone's "avoid double-counting shared allocations"
  (`docs/m10-memory-budget.md`).

## 4. GPU composition

- **Observed.** `crates/gpu/src/lib.rs`, module comment.
  - Surfaces are resident on the GPU in pages, and "a tile is re-uploaded only when its `Arc`
    changed (a brush stroke uploads just the touched 256² tiles; undo swaps pointers back and
    uploads only what differs)".
  - "Pages over a byte budget are evicted least recently used first."
  - Execution runs in "1024² RGBA32F accumulators", and unsupported cases fall back to the CPU
    compositor (`photocraft-compose`).
- **Relevance (inference).**
  - The pointer-identity change detection works on the CPU side too. DrawApp's view cache keys
    tiles by version so it can skip unchanged tiles.
  - GPU composition itself is not adopted. DrawApp has Normal blending only, and the measured CPU
    composite costs (`docs/m10-performance.md`) do not show a bottleneck that a GPU would remove
    without a texture-based canvas. A wgpu-scale GPU path would be a separate milestone, with its
    own fallback and testing burden.

## 5. Cancellation of long operations

- **Observed.** `crates/raster/src/interrupt.rs:1-8`.
  - "Heavy algorithms take an [`Interrupt`] and check [`Interrupt::cancelled`] once per tile, row
    band or iteration, returning early … They may also report [`Interrupt::progress`]."
  - `Interrupt::NONE` gives synchronous callers the old behaviour.
- **Relevance.** Adopted: DrawApp's background transform checks a cancellation flag once per row
  band and reports progress through an atomic counter (`docs/m10-async-transforms.md`).

## 6. Empty selection (re-checked at this revision)

- **Observed.** `crates/algo/src/selection.rs:156`: combining selections ends with
  `(s.tile_count() > 0).then_some(s)`. An empty result becomes `None`, i.e. no selection.
- **Observed.** `crates/engine/src/selection_cmds.rs:32-34`: `point_in_selection` returns
  `doc.selection.as_ref().is_none_or(|selection| selection.sample_channel(x, y, 0) > 0.0)`.
  With no selection, every point is in the selection.
- **Conclusion (unchanged since milestone 9).** Subtracting everything from a selection makes
  "everything selected" in PhotoCraft. **DrawApp must keep its separate "empty" state.** An empty
  `TiledImage`-backed mask would also be "no tiles", so the distinction cannot rely on tile
  counts. DrawApp's `Selection` keeps its explicit three-state model and its dense mask
  (`src/core/Selection.h`).
- Also different: PhotoCraft's Add is `p.max(q(v))` (`selection.rs:150`). DrawApp keeps
  its saturating sum (milestone 8 decision).

## 7. Snapshots, persistence and background jobs

- **Inference** from the module comment (§1) and `ops/src/tiles.rs`. PhotoCraft's undo states,
  autosave and background jobs all hold `Surface` clones that share tiles. DrawApp's recovery
  snapshots (`DocumentData`) and worker saves already copy `QImage`s, which share pixels until
  written; milestone 10 keeps that semantics with per-tile sharing.
- Persistence (`crates/format`, `.pcraft`): not re-studied. DrawApp's own format decision is in
  `docs/m10-file-compatibility.md`.

## What we do not take

Rust-specific structures (`BTreeMap`, rayon), the GPU compositor, Photoshop blend semantics, and
the 256 px tile size without measurement.

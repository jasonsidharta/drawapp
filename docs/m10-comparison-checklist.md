# Milestone 10: comparison with other applications

**Status: the side-by-side comparison was not run.** Krita, PhotoCraft, GIMP and MyPaint are not
installed on the development machine (checked 2026-10-12: Program Files, Program Files (x86) and
the user's Programs folder), and installing applications was outside what this milestone was
allowed to do without asking. Nothing below claims that DrawApp is faster, lighter or more
capable than any of them.

What exists instead:
1. **Source-level comparison** of the storage and memory designs (read, not run):
   `research/m10-krita-tile-engine.md` (Krita at 25840ee8 and e579e3a4),
   `research/m10-photocraft-memory.md` (PhotoCraft at 20cb95bc and 8b216216),
   `research/m10-gegl-skia-comparison.md`. Summary below.
2. **A procedure** for running the comparison on one machine, so the numbers would be
   comparable.

## Design comparison (from source reading)

| | DrawApp milestone 10 | Krita (source) | PhotoCraft (source) |
|---|---|---|---|
| Tile size | 64 × 64, chosen by measurement | 64 × 64 | 256 × 256 |
| Tile lookup | dense grid per layer | lock-free hash table | map of `Arc` tiles |
| Copy-on-write | per tile, atomic owner count | per tile (`KisTile::lockForWrite`) | `Arc::make_mut` per tile |
| Memory limit | pixel-data budget, half of RAM (quarter on Android) | configurable limit with swap to disk | allocation errors returned to callers |
| Under pressure | drop view caches, then oldest undo; refuse with a message | swap tiles to disk, compress | operation fails with an error |
| Undo | tile references in a byte budget | mementos per tile | snapshots by tile pointer |
| Zoomed-out view | pyramid of 2× reductions, lazily rebuilt by tile stamps | mipmapped textures (OpenGL) | GPU residency |
| Transform preview | full resolution over the changed area | reduced resolution (level of detail) | not studied |
| Transform apply | worker thread, cancellable, atomic commit | strokes in the background | not studied |
| Empty selection vs no selection | distinguished | distinguished | empty collapses to none (`selection.rs:156`) |

## Procedure (for when the applications are available)

Same machine, release builds, no other heavy processes, each measurement five times.

1. **Blank large canvas**: create 16384 × 16384 with one layer; record private memory
   (Process Explorer "Private Bytes") after 10 s idle.
2. **Sparse sketch**: on that canvas draw the same 6 long strokes per layer on 4 layers (use the
   `bench_m9 sparse16384` stroke positions); record private memory and file size after saving
   in each application's native format.
3. **Dense workload**: 4096 × 4096, 20 layers filled with the `bench_m9` dab pattern (import the
   layers as PNGs written by DrawApp so content is identical); record memory, save and open time.
4. **Transform**: select all layers but the background, scale 120 % and rotate 15°; record the
   time from Apply until the canvas accepts input, with a stopwatch screen recording at 60 fps.
5. **Zoom out**: fit the 16384² sketch on screen; record the time to the first complete frame
   (screen recording).
6. Report each application's version, settings (memory limit, swap, tile cache) and every raw
   number; do not summarise as faster or slower without all runs.

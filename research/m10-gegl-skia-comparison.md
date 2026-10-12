# GEGL, libmypaint, Skia and OpenRaster (milestone 10 research)

Secondary references, read only for techniques that fit DrawApp's architecture. No dependency
is added.

## Revisions

| Project | Revision | How it was read |
|---|---|---|
| GEGL | `5a15f6d2b5e5b432d288d0d800600d547eda58bb` (2026-10-11), `https://gitlab.gnome.org/GNOME/gegl.git` | shallow blobless clone, `git show HEAD:<path>` |
| Skia | `ec07879aef00c858c79d55a7e85ac0a9e376a0a8` (2026-10-12), `https://skia.googlesource.com/skia.git` | shallow blobless clone |
| libmypaint | `d5a88fb` (local checkout since milestone 6) | working tree |
| OpenRaster | baseline specification pages `file-layout-spec.html` and `layer-stack-spec.html` (read 2026-10-12) | web |

## GEGL (GIMP's buffer engine)

- **Observed.** `gegl/buffer/gegl-buffer-config.c:145-166`:
  - default tile size `"tile-width"` / `"tile-height"` **128**;
  - default `"tile-cache-size"` **512 × 1024 × 1024** bytes;
  - swapping is configured by `"swap"` and `"swap-compression"` (lines 68-72).
- **Observed.** `gegl/buffer/gegl-tile.c:119-142` (`gegl_tile_dup`). A duplicated tile shares the
  source's data and increments a clone counter (`g_atomic_int_inc (gegl_tile_n_clones (tile))`).
  `gegl_tile_unclone` (line 187) copies only when that counter is above 1, which is
  copy-on-write.
- **Observed.** `gegl/buffer/gegl-tile-handler-empty.c:48-63`. Requests for missing tiles return
  `gegl_tile_handler_dup_tile(…, empty->tile, …)`: every empty tile is a COW duplicate of **one**
  shared empty tile.
- **Observed.** `gegl/buffer/gegl-tile-handler-zoom.c` exists: lower-resolution levels are
  produced on demand from the level below.
- **Relevance.**
  - GEGL independently confirms COW tiles with an atomic clone count.
  - GEGL independently confirms empty space as a shared default.
  - GEGL independently confirms a zoom pyramid built lazily, which is what DrawApp's view needs.
  - The tile-size default differs from Krita's (128 vs 64), so we measure it rather than copy
    either (`docs/m10-tile-storage.md`).
  - Not adopted: GEGL's graph-based processing. DrawApp has no non-destructive filter graph.

## libmypaint

- **Observed.** `mypaint-config.h:5`: `#define MYPAINT_TILE_SIZE 64`.
- **Observed.** `mypaint-tiled-surface.h:18-74`. The brush engine accesses a host surface through
  `tile_request_start` / `tile_request_end` with `tx, ty` and a `readonly` flag. The host decides
  how tiles are stored; the brush only asks for one tile at a time.
- **Relevance.** This matches DrawApp's plan: `StrokeEngine` reaches the layer through a narrow
  surface interface (row pointers inside one tile, read-only or writable) instead of a whole
  `QImage`, so the same engine draws into dense images (scratchpad, thumbnails) and tiled layers.

## Skia

- **Observed.** `src/gpu/TiledTextureUtils.h:41-47` (comments on `ImageDrawMode`). When a large
  image is drawn in tiles, Skia has to handle filtering at tile edges explicitly, by restricting
  sources to the image content or using a decal mode. Otherwise neighbouring tiles' pixels are
  sampled wrongly.
- **Observed.** `src/image/SkTiledImageUtils.cpp` exists as the CPU entry point for drawing big
  images in tiles.
- **Relevance.** Drawing each tile separately with bilinear filtering produces seams. DrawApp's
  view therefore assembles the visible area (with a one-pixel margin) from tiles into one image
  before scaling it, instead of drawing tiles one by one (`research/m10-rendering-architecture.md`).

## OpenRaster

- **Observed.** In the layer stack specification, layers have signed integer `x` and `y`
  offsets ("They are the horizontal and vertical offset in pixels … default value of 0"). Layer
  content "may have extents which can be smaller or larger" than the image and is cropped to
  `(0,0,w,h)` for display.
- **Observed.** In the file layout specification, a zip has `mimetype` (stored, first),
  `stack.xml`, `mergedimage.png` and `Thumbnails/thumbnail.png`; layer data is PNG files under
  `data/`.
- **Relevance (inference).** OpenRaster stores each layer cropped to its content with an offset:
  a sparse representation at file level.
  - DrawApp's own format revision (v3) stores occupied tiles instead, for the same reason.
  - OpenRaster import or export is **not** implemented in milestone 10. The format is a candidate
    for interchange later, and the tile layout maps onto it, since a cropped PNG can be made from
    the occupied bounds.

## Summary of decisions

| Technique | Source | Decision |
|---|---|---|
| COW tiles with an atomic user count | Krita, GEGL, PhotoCraft | adopt |
| Missing tile = shared default (transparent) | Krita, GEGL, PhotoCraft | adopt (null pointer) |
| Prune tiles equal to the default | Krita `purge`, PhotoCraft `prune` | adopt, at safe points |
| Tile size | Krita 64, libmypaint 64, GEGL 128, PhotoCraft 256 | measure 64/128/256 |
| Zoom pyramid for display | GEGL zoom handler, Krita LoD | adopt for the view |
| Assemble before filtering (no seams) | Skia tiled drawing | adopt |
| Swapping to disk | Krita, GEGL | reject for now (budget + refusal) |
| Lock-free hash / multithreaded writers | Krita | reject (single writer thread) |
| GPU compositor | PhotoCraft | reject for this milestone (no measured need) |
| Graph processing | GEGL | reject (no filter graph) |

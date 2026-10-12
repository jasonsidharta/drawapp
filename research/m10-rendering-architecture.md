# Rendering architecture for milestone 10 (design, from the research)

The intended architecture, derived from `m10-krita-tile-engine.md`, `m10-photocraft-memory.md`
and `m10-gegl-skia-comparison.md`. What was actually built, and how it measured, is in
`docs/m10-tile-storage.md` and `docs/m10-performance.md`.

## Starting point (milestone 9, read in the code)

| Part | Milestone 9 implementation | Problem at scale |
|---|---|---|
| Layer pixels | `LayerData::image`, a canvas-sized premultiplied `QImage` (`src/core/DocumentData.h`) | an empty 8192² layer costs 256 MiB; the 512 MiB document limit exists because of this |
| Composite | `LayeredDocument::m_composite`, a canvas-sized `QImage`, refreshed per rect with `QPainter` (`compositeLayers`) | one more full canvas; a 16 384² composite alone is 1 GiB |
| View | `ViewRenderer::render` draws the dense composite with one `QPainter::drawImage`, smooth when zoomed out | needs the dense composite; zoomed-out views filter full-resolution pixels |
| Undo | `TileSnapshot`: copies of the 64 × 64 tiles an edit touches | copies pixels even when the edit only needs the old ones kept |
| Transform preview | canvas-sized preview `QImage`s and a dense preview composite | memory per layer; 4–7 fps at 4096² |
| Transform Apply | renders every layer's region on the UI thread, then copies them in | blocks the UI for 2.3–3.2 s at 7 × 4096² |
| Files | `.drawapp` v2: each layer one `qCompress`ed dense block | a 16 384² layer needs a 1 GiB buffer just to encode |

## Target

### Coordinate spaces (kept distinct)

- **Document space**: pixels of the canvas, (0, 0) at the top left. Layers are canvas-sized, so
  layer space equals document space; there are no layer offsets in DrawApp.
- **Tile space**: `(column, row) = (x >> log2(T), y >> log2(T))` for tile size T. Negative or
  out-of-canvas coordinates belong to no tile and read as transparent.
- **Pyramid level L**: document space divided by 2^L, used for display and reduced-resolution
  previews.
- **View space**: item pixels; `ViewTransform` maps document ↔ view. It is never stored in the
  document.
- **Transform preview space**: document space at full resolution, or pyramid level L while
  dragging.

### Storage: `TiledImage`

- A dense grid of nullable, reference-counted tiles of T × T pixels. Null reads as transparent.
- The grid is implicitly shared, so a snapshot costs O(1). A tile is shared until written and
  copied on the first write.
- A write to a missing tile allocates just that tile. Tiles that become fully transparent are
  pruned at safe points.

### Composite (projection cache)

- The composite is itself a `TiledImage`. Refreshing a rect recomputes only the composite tiles
  in that rect.
- A composite tile under which no visible layer has a tile stays null. The composite of an empty
  16 384² document costs nothing.
- Blending stays `QPainter` source-over with layer opacity, per tile, so pixels match milestone 9
  exactly.

### View

- A pyramid of the composite: level L tiles are box-filtered from level L − 1. They are built
  lazily for what the view needs and invalidated by the same dirty rects as the composite.
  Pyramid tiles are a reconstructible cache: the first thing evicted under memory pressure.
- To paint, the view picks the level with zoom × 2^L in (0.5, 1] and assembles the visible area
  plus a one-pixel margin into one image. That image is drawn with the view transform: smooth
  when shrinking, nearest when magnifying, as before.
- Assembling first avoids the edge-filtering seams that per-tile drawing has (Skia,
  `TiledTextureUtils.h`).
- The assembled image is about the viewport's size, whatever the canvas size.

### Brushes

- `StrokeEngine` keeps its algorithms. It reaches pixels through a narrow surface interface: a
  pixel read, a read-only row pointer and a writable row pointer, both inside one 64-pixel tile
  segment, plus "capture before writing". There is one implementation for dense `QImage`s
  (scratchpad, thumbnails, tests, brush sheet) and one for `TiledImage` layers. This follows
  libmypaint's tile-request interface.
- The stroke's undo snapshot holds references to the pre-stroke tiles. Composite-from-original
  works exactly as in milestones 6–9 because it reads those references.

### Transforms

- Begin copies the selected layers' `TiledImage`s, which is O(1), and extracts dense sources only
  over the source rectangle.
- Previews write into preview `TiledImage`s that share untouched tiles with the layers.
  Interactive drags render at the view's pyramid level (as Krita's LoD preview); full resolution
  is rendered after the drag.
- Apply runs on a worker (`research/m10-concurrency-design.md`). It produces new layer images
  that share unchanged tiles with the old ones. Commit swaps pointers on the UI thread and records
  one undo command whose snapshot holds the old tiles.

### Files

- `.drawapp` v3 stores each layer as its occupied tiles, each compressed separately. Encoding
  never needs a whole dense layer.
- v1 and v2 stay readable (`docs/m10-file-compatibility.md`).

## Not in this milestone

- GPU canvas or compositor (no measured need: `m10-performance-hypotheses.md`).
- Swapping to disk.
- Tiled selection masks.
- Pixel formats other than 8-bit premultiplied ARGB. The tile carries bytes per pixel so they can
  be added.
- Layer offsets or infinite canvases.

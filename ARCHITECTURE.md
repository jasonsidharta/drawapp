# Architecture

State: milestone 9 (modular brush engine with sensor dynamics, Brush Studio, multi-layer
transforms and more selection tools) on top of milestone 8 (selections and basic transforms),
milestone 6 (artist brush system) and milestones 5 (Android save safety), 4 (background
file operations), 3 (raster layers), 2 (crash recovery), 1 (undo/redo, project save/open, PNG
export) and the milestone 0 prototype. Everything described here exists in
the code unless labelled *planned*.

## Stack

- C++17, Qt 6.12.0 (Qt Quick, Qt Quick Controls with the Fusion style, Qt Quick Dialogs, QtCore
  QML types), CMake ≥ 3.22, Ninja.
- Windows toolchain: MinGW-w64 GCC 15.1.0 (the compiler Qt 6.12.0's MinGW binaries were built with).
- Android toolchain: NDK r27c (27.2.12479018, Clang 18), JDK 21, compile/target SDK 36, min SDK 28.
- No third-party libraries beyond Qt. Compression is Qt's zlib (`qCompress`); checksums use
  `QCryptographicHash`.

Why Qt 6 with Qt Quick/QML: one C++ codebase for Windows, Linux, Android and iOS, with a GPU
scene graph, pen/touch input and native file dialogs on all four. No blocker found so far.

## Components

```
src/core/   (static lib "drawcore", depends on QtGui only, no Qt Quick)
  MemoryBudget     process-wide byte accounting by category (document, cache, composite,
                   preview, temporary) against one limit; evictors for reconstructible memory
  TiledImage       a sparse image of 64x64 copy-on-write tiles (missing tile = transparent);
                   copies share tiles until written (milestone 10)
  DocumentData     plain values: LayerData {id, name, image (TiledImage), visible, opacity,
                   locked}, DocumentData {size, layers, activeLayerId}; compositeLayers()
                   (dense and tiled); shared limits
  LayeredDocument  the open document: ordered layers (index 0 = bottom), active layer, id
                   allocation, cached composite of the visible layers
  RasterDocument   raster operations on one image or tiled layer: stampDabs (test fixtures), clear
  BrushSettings    brush and preset data (plain values); PressureMapping; settings version 2
  BrushDynamics    sensors, response curves, ParameterDynamics, CompiledDynamics (milestone 9)
  BrushProperties  the property registry: every brush property's metadata and accessors; drives
                   JSON, validation, equality, the Brush Studio and the generated specification
  BrushTips        the generated tip (flat bristle) and textures (paper, canvas); imported tips
  BrushPresets     the built-in presets
  BrushPresetStore the artist's brush library file (custom presets, saved changes, favourites)
  BrushPresetFile  one preset as a .drawbrush file (import/export)
  StrokeSmoother   distance-based ("pull string") smoothing of input samples
  StrokeSpacer     StrokeSample (position, pressure, tilt, time); distance-spaced dabs with the
                   input interpolated; PressureResponse (firmness -> gamma)
  StrokeEngine     the one brush renderer: input filters, smoothing, path sensors, taper,
                   spacing, airbrush timing, dynamics, dab parameters, stamps (tip, spray,
                   hatching), coverage with blend modes (paint, erase) or a carried patch (smudge,
                   wet paint), texture, selection mask; undo tiles. Paints a StrokeSurface
                   (StrokeSurface.h): a dense QImage or a tiled layer, 64-pixel row segments
  Selection        document-space selection coverage (none / active / empty), shapes, modes,
                   invert, magic wand, grow/shrink/feather, outline
  TransformSession move/scale/rotate/flip of one or several layers' (selected) pixels with one
                   geometry: cached sources and bases, previews, handles, the selection after Apply
  TileSnapshot     the 64x64 tiles an edit touched, as they were: copies (dense images) or
                   shared tile references (tiled layers); swap = undo/redo
  UndoHistory      UndoCommand (pixels / layer presence / properties / move) + linear history
                   under one byte budget; state ids for "unsaved"
  ProjectFile      .drawapp v3 (layered, sparse tiles) encode/decode/verify, v1 and v2 readers,
                   local safe writes
  SafeSave         writing projects and exports: local files atomically, provider documents
                   with a local copy, probing, space reservation and read-back
  DocumentProvider destination access for content:// documents (Android: ContentResolver via
                   JNI in AndroidDocumentProvider.cpp; desktop: QFile stand-in; tests: a fake)
  ProjectIo        one worker thread for save, open and PNG export
  RecoveryFile     .drawrecovery v2 (metadata + embedded layered project), v1 reader
  RecoveryManager  session lock, orphan discovery, one-at-a-time worker writes with stale checks
  ViewTransform, Pressure.h, ViewRenderer   (view and input policy; unchanged)
src/app/    (static QML module "drawapp_ui", URI DrawApp, used by the app and tst_ui)
  CanvasItem       QQuickPaintedItem; input, strokes, layer commands, history, files, recovery;
                   selections, transforms and the clipboard in CanvasItemSelection.cpp
  BrushLibrary     presets and the paint/eraser working brushes (canvas.brushes in QML)
  BrushStudio      the Brush Studio model: one preset edited on a copy (canvas.brushes.studio)
  BrushThumbnails  image provider: preset thumbnails drawn by StrokeEngine
  BrushScratchpad  try-out surface on the same engine, settings and input handling
  Main.qml         window, toolbar (active brush, size, opacity), File menu, layer panel, dialogs
  BrushPanel.qml   preset list, scratchpad, basic and advanced brush settings
  BrushStudioPage.qml, CurveEditor.qml   the Brush Studio (loaded when first opened)
  main.cpp         application setup (in the drawapp executable)
tests/             tst_core, tst_brush, tst_brushengine, tst_tiles, tst_selection, tst_document, tst_recovery
                   (headless); tst_canvas, tst_ui (window, offscreen under ctest); FakeProvider.h;
                   golden/ (reference strokes and preset files); bench_core, bench_canvas,
                   bench_m9, bench_tiles; brush_sheet (renders the brush test sheet)
```

## Layers

- **Model**: each layer has a stable id (never reused in a document, saved in files), a name, a
  full-canvas premultiplied ARGB32 image, visibility, opacity (0–1) and a lock. The document has
  an ordered layer list and an active layer. All layers have the canvas size; canvas sizes are
  unchanged from earlier milestones (new documents 2048 × 1536).
- **Storage** (milestone 10): each layer is a `TiledImage` of the canvas size made of 64 x 64
  tiles; a tile that was never painted (or was erased to transparent and pruned) does not exist
  and reads as transparent. A copy of a layer (recovery snapshot, transform preview, undo
  record) shares its tiles; whoever writes to a shared tile copies that tile first. Every tile
  is allocated through `MemoryBudget`; an operation that cannot get a tile fails cleanly (see
  `docs/m10-tile-storage.md`).
- **Compositing**: Normal (source-over) only. `compositeLayers` works tile by tile on the
  composite: for each tile, the visible layers' tiles are drawn bottom to top with
  `QPainter::setOpacity` (as before, so pixels are unchanged); where no visible layer has a tile
  the composite tile is released. `LayeredDocument` keeps the composite as a tiled image too.
  Brush/eraser events refresh only the dabs' rect;
  visibility, opacity, order, add/delete/duplicate refresh the whole canvas. The view draws the
  cache exactly as before (renderer unchanged), so the checkerboard appears behind the whole
  stack, and erasing an upper layer shows the layers below.
- **Painting**: brush and eraser affect only the active layer, chosen when the stroke starts.
  Strokes, and Clear Layer, are refused on hidden or locked layers with a notice explaining why
  (the canvas emits `notice`, QML shows a banner). The lock blocks pixel edits only; renaming,
  hiding, reordering and deleting a locked layer are allowed.
- **Panel** (Main.qml): list (top layer first) with visibility check box, name, opacity and lock;
  Add (above the active layer), Duplicate, Delete, Up, Down, Rename (also double-click), and an
  opacity slider for the active layer. At least one layer is always kept. At most 64 layers at
  any canvas size (up to 16384 x 16384): since milestone 10 memory depends on what is painted and
  is checked when tiles are allocated, not estimated from the canvas size.
- **Active layer rules**: selecting a layer is not an edit (no history entry, not "modified").
  New and duplicated layers become active. Deleting the active layer selects the layer below
  (or, for the bottom layer, the new bottom). Undo/redo that re-inserts a layer selects it; other
  undo/redo steps keep the selection unless the selected layer disappears.

## Drawing data flow

1. **Input** reaches `CanvasItem`: mouse (left draws; middle or Space + left pans), pen (window
   event filter, because `QQuickItem` has no tablet handler), touch (one finger draws, two pan and
   zoom), wheel (zoom about the cursor).
2. **Pressure** only for pens reporting the `Pressure` capability; everything else uses 1.0, so
   mouse and touch always draw at full size whatever the pressure settings.
3. **Coordinates** go through `viewToDoc`; document pixels never depend on the view.
4. **Settings** are captured when the stroke starts: the working brush of the tool (or of the
   eraser for a pen's eraser end), colour, layer, a per-stroke random seed, and the optional clip
   mask. Changing them mid-stroke affects the next stroke.
5. **Samples**: each event becomes a `StrokeSample` in document pixels with pressure, tilt (only
   when the pen reports it) and a time from a per-stroke clock. `CanvasItem::drawStroke` feeds the
   same path from samples, without pointer events.
6. **Smoothing** (`StrokeSmoother`, optional): the brush point hangs on a string of length
   `smoothing / 100 × 40` view pixels (converted to document pixels at stroke start) and moves only
   when the pointer pulls the string tight. Input segments are walked in 0.5 px steps, so the
   result depends on the path, not on the event rate or speed. When the stroke ends, the line is
   completed straight to the last input position (the release position, not its pressure).
7. **Brush rendering** (`StrokeEngine`, next section): spaced dabs, airbrush dabs from elapsed
   time, coverage or smudging; changed pixels are recomputed from the stroke's undo `TileSnapshot`.
   Escape cancels the stroke by swapping that snapshot back.
8. **Composite and repaint**: the composite is refreshed for the changed rect, which is then
   repainted. One stroke is one undo step (the `TileSnapshot`), labelled Brush, Eraser or Smudge
   stroke.

## Brush system (milestone 6)

Research notes: `research/brush-system.md` (Krita, PhotoCraft and libmypaint read for concepts
only; no code, presets or assets taken).

**Separation.** `BrushSettings` is data only (no colour, no stroke state). `StrokeSample` is input
in document coordinates. `StrokeEngine` processes and renders a stroke. `BrushLibrary`,
`BrushPanel.qml` and the toolbar are the UI. Every preset, the scratchpad and the thumbnails use
the same `StrokeEngine`; there is no per-preset engine.

**Dab parameters.** Per dab, from the interpolated sample:
- diameter = size × pressure→size × tilt factor (at least 1 px);
- ceiling C = opacity × pressure→opacity × tilt factor, rounded to a whole 8-bit level;
- pass flow = flow × pressure→flow;
- angle = base angle (+ travel direction, or pen azimuth when tilt is reported and at least 15°,
  if "turn with the stroke") + deterministic jitter.

A pressure mapping is `minimum + (1 − minimum) × p^gamma`. Input without pressure arrives as 1.0,
so mouse and touch draw at full size, opacity and flow. Tilt factors interpolate from 1 (upright)
to the setting at 60°. Without tilt data they stay 1 and "turn with the stroke" uses the travel
direction.

**Opacity, flow and accumulation.** Each stroke keeps a coverage S per pixel (16-bit, sparse
64 × 64 tiles, only where the stroke goes):
- *Uniform*: `S = max(S, v·C)`. Overlaps and self-crossings never darken, and the stroke reaches
  its opacity at once (flow is not used).
- *Build-up*: `S += v·f·(C − S)` with `f = 1 − (1 − pass flow)^(step / L)`. L is the tip's pass
  length: its integral along its densest line, as a fraction of the diameter, which is 1 for a
  hard round tip. One pass along the stroke's centre therefore deposits "flow" of the way to the
  opacity, whatever the spacing, hardness or tip image. Repeated passes approach C and never pass
  it. Rounding is stochastic and seeded, so nothing stalls below C.

Opacity is the stroke's ceiling and flow how fast it gets there. Neither is applied twice:
composite strength is `k = S` (times texture and mask). Separate strokes still build up, because
the next stroke's "before" includes the previous one.

**Composite.** `k` is dithered to 8 bits with a fixed per-pixel threshold, so whole levels stay
exact (a 50 % uniform stroke is exactly 128). Then:
- paint: `before` under `(colour, k)`, premultiplied;
- erase: `before × (1 − k)`.

Only pixels touched by this call are recomputed, once each.

**Tips.**
- *Round*: computed. The core is solid up to the hardness, then the falloff `(1 − t²)²` runs to
  the rim. The edge is anti-aliased by distance. Radii under 3 px are area-sampled 4 × 4.
  Roundness and angle make ellipses.
- *Sampled*: grayscale image from a mip chain, bilinear; the image's x axis is the travel
  direction.

**Spacing.** Dabs are placed every `max(0.5 px, spacing × diameter)` of travel along the smoothed
path, with pressure, tilt and time interpolated. Splitting the same path into more events gives
the same dabs.

Resampling limits:
- Input is linear between events, so curvature between events is lost. Smoothing then rounds
  corners by design.
- Pressure and tilt change linearly between events. Rapid changes between two events are not
  recovered.
- Time is interpolated linearly. Airbrush dabs while moving use the last sample's position.

**Texture.** Grayscale tiles in document space, so the grain stays put along and across strokes
and does not move with the dabs:
- *Multiply*: `k × (1 − strength(1 − T))`.
- *Tooth*: `opacity × max(0, S/opacity − strength(1 − T))`. Light marks catch only the tops of
  the grain; heavy ones fill it.

**Airbrush.** Dabs are added from elapsed time at the rate setting, never from the number of
events: the canvas runs a 16 ms timer while the pen is held. Distance dabs restart the clock. A
stall adds at most 24 dabs per call. An airbrush always builds up.

**Smudge (blending, not paint simulation).** A carried patch of premultiplied colour, the size of
the largest dab, attached to the brush:
- each dab lays the patch down with `strength × tip`, then the patch takes on the pixels it passed
  at the pickup rate;
- both rates are per pass, normalised like flow;
- the first dab only loads the patch, so a click without movement changes nothing;
- it works only on the active layer.

Transparency rules: transparent pixels carry no colour, so dragging paint into transparency
spreads it with its own alpha and no fringe, and dragging transparency into paint thins the paint.
Beyond the layer edge the nearest edge pixel is loaded and nothing is picked up, so edges stay
clean.

**Determinism and life cycle.**
- *Seed and replay*: the seed (stroke count) and dab index drive jitter and rounding; the same
  samples and seed give identical pixels (`deterministicReplay`).
- *Dots*: a stroke that never moves becomes a dot with one full pass of paint.
- *Tips that follow the travel direction*: the first dab waits until the direction is known; a
  dot uses the base angle.
- *Ending a stroke*: pen-up, lost grab, a second finger or a save/undo mid-stroke all complete the
  stroke as one undo step. Escape cancels it completely.

**Selection.** Not built. `StrokeEngine::setClipMask` / `CanvasItem::setStrokeClipMask` limit
paint, erase and smudge to a grayscale mask (tested), as the integration point.

**Memory and work.**
- Per stroke: 8 KiB per touched 64 × 64 coverage tile, the undo tiles (as before), and a smudge
  patch of at most (size + 4)² × 16 bytes.
- No full-canvas buffer and no full-canvas copy per event. The first edit after a snapshot copies
  the shared layer once (measured).

**Presets** (`BrushPresets`, original definitions):

| Preset | Character |
|---|---|
| Sketch Pencil | 5 px, slightly soft, build-up, pressure to size and opacity, paper tooth, wider and lighter with tilt |
| Clean Ink | 8 px, hard anti-aliased edge, pressure to size, smoothing 30; ends where the pen ends (no forced taper) |
| Solid Round | 12 px, hard, uniform: the milestone 5 brush and the default |
| Soft Airbrush | 90 px, no hardness, build-up over time (airbrush, 40/s), pressure to flow |
| Textured Paint | 40 px flat bristle tip turning with the stroke, build-up, pressure to size and flow, light canvas grain |
| Blend / Smudge | 36 px smudge; pressure sets the strength |
| Hard Eraser | 24 px, hard |
| Soft Eraser | 70 px, soft, build-up, pressure to flow |

**Library and storage.**
- *Working brushes*: `BrushLibrary` keeps two, paint and eraser. Selecting an eraser preset
  switches to the eraser tool and other presets switch to the brush.
- *Session edits*: edits change a per-preset copy for this session. *Save* writes them into the
  preset; a built-in is saved as changes that *Reset to default* removes. *Save as new* creates a
  custom preset; custom presets can be renamed, reverted and deleted.
- *Temporary size*: never marks a preset modified, and is saved only by an explicit Save.
- *Brush file*: `brushes.json` in the app data folder, or `DRAWAPP_BRUSH_FILE`. It holds custom
  presets, changes to built-ins, favourites and the selected presets. It is versioned
  (`"format": "drawapp-brushes", "version": 1`), validated on load (bad entries are skipped with
  a message), written atomically with `ProjectFile::writeFileSafely`, and coalesced (400 ms).
- *Unusable files*: a damaged file, or one from a newer version, is never overwritten; the
  built-ins still work.
- *Project files*: brush settings are not stored in projects, so the project format is unchanged.

**Interface.**
- *Toolbar*: the active brush (its rendered stroke and name), size and opacity.
- *Brush panel* (popup, closed while drawing):
  - preset list with thumbnails drawn by the engine, favourites and an "unsaved edits" marker;
  - a scratchpad on the same engine, with pen pressure and tilt;
  - basic settings, plus "More settings…" for the rest;
  - two columns on wide windows, one scrolling column on phones; 32–40 px touch targets.
- *Glyphs*: plain-text labels where Android's default font lacks a glyph (arrow triangles).

**Brush tips and textures: provenance.** All of them are generated by `BrushTips.cpp` from fixed
seeds when first used. There are no image files, and nothing is derived from another
application's assets:
- *bristle*: 34 elliptical hairs with hashed position, width and load, saturated;
- *paper*: tileable value-noise fibres and about 2 px grain with a contrast curve;
- *canvas*: an 8 px over/under weave with noise.

The hash is splitmix64. The round tip is computed per dab.

## Brush system: milestone 9 additions

Full description: `docs/brush-engine-architecture.md`. Property reference (generated):
`docs/brush-settings-specification.md`. Preset files: `docs/brush-preset-format.md`. Research
and decisions: `research/milestone9-*.md`.

- **Still one renderer.** Engines are combinations of `blend` and `stamp`:
  - paint, erase, smudge and wet paint (a bounded prototype);
  - tip, spray and hatching stamps for paint and erase.

  Everything in the milestone 6 section above still holds. Milestone 6 presets render as before
  (golden images).
- **Sensor dynamics** (`BrushDynamics`). 12 parameters can follow 13 sensors through monotone
  cubic curves, with five combination rules. A sensor the device does not report (pressure from
  mouse or touch, tilt, speed without times) is left out; nothing is invented.
  `StrokeSample::hasPressure` carries this from `CanvasItem` and the scratchpad.
- **Property registry** (`BrushProperties`). It covers the 120-property design target: 94
  implemented, each with a test that it changes rendering, and 26 planned with no controls.
- **New per-dab features.** Scatter, count, size/opacity/flow/roundness/angle/colour jitters,
  flips, density, imported tips, taper (the end is held back until pen-up), texture options
  (rotation, contrast, brightness, invert, each dab, moving grain, random offset), blend modes
  (multiply, screen, behind, inside), smudge dulling/colour rate/colour only, and stabiliser
  options (pressure smoothing, jitter filter, finish at the pen).
- **Brush Studio** (`BrushStudio`, `BrushStudioPage.qml`). Edits a copy of one preset; Done
  stores it and Cancel drops it. Categories, search, numeric entry, curve editor, preview (with
  simulated pressure, labelled) and a try-out pad. Reset per setting or for the whole brush,
  Duplicate, Rename, and `.drawbrush` import/export.
- **Library format 2.** Version 1 files read unchanged. Unknown keys are kept, and unknown
  tip/texture ids are kept with a warning.
- **Speed.** The milestone 6 rasteriser loop and a direct grain lookup are used when no new
  per-pixel feature is active, so ordinary presets run at the milestone 6 speed (BENCHMARKS.md).

## Selections, transforms and the clipboard (milestone 8)

Research notes: `research/selections-transforms.md` (Krita and PhotoCraft read for concepts only).

**The selection model** (`Selection`). Coverage 0..255 per document pixel, in its own
Grayscale8 image the size of the document. It is held by `LayeredDocument`, apart from the
layers, so it never changes pixels or visibility. It has three states:

| State | Meaning | How you get it |
|---|---|---|
| none | every pixel is editable | the default; Deselect; a click with the plain Select tool; a new or opened document |
| active | 255 editable, 0 protected, between = soft edge | any selection shape or Select All |
| empty | active, but nothing is selected: **no** pixel is editable | e.g. subtracting everything |

An empty result never turns into "none" by itself. Painting, Clear, Copy, Cut and Transform
refuse with "Nothing is selected" and point to Deselect.

**Shapes and modes.**
- *Shapes:* rectangle, ellipse and freehand lasso, filled with `QPainter` anti-aliasing into an
  Alpha8 mask whose bytes are the coverage. A lasso that crosses itself is filled with the
  nonzero winding rule, so everything the artist circled is inside.
- *Replace:* the shape becomes the selection.
- *Add:* `min(1, a + b)`, a saturating sum, as Krita does. Shapes that share an anti-aliased edge
  join without a half-transparent seam; a max would leave one.
- *Subtract:* `max(0, a − b)`.
- *Intersect:* `min(a, b)`.
- *Combining with "none":* treated as nothing selected.
- *Modifiers:* Shift adds, Alt subtracts and Shift+Alt intersects, read from the pointer event
  that starts the shape. The bar's mode buttons do the same for touch.
- *Other commands:* Select All, Deselect, Invert (`255 − a`; disabled without a selection) and
  Reselect (the last non-empty selection before it changed).

**Undo and files.**
- Every selection change is one undo step, so an accidental lasso can be undone. A change that
  leaves the selection as it was records nothing.
- Selection steps never make the document "unsaved". `UndoHistory::stateId` skips them, because
  selections are **not saved**: neither project files nor recovery files contain them, so the
  `.drawapp` format is unchanged. A reopened or recovered drawing starts with no selection.

**Outline.** The boundary at 50 % coverage, computed once per change as runs along pixel edges.
It is drawn in view coordinates as a white line under black dashes (cosmetic, one screen pixel),
so it reads on light and dark artwork at any zoom. It is not animated.

**Painting through a selection.**
- *Paint and erase:* the brush engine gets the mask as its clip and applies it once, when each
  pixel is composited from the stroke's coverage (`k = S × texture × selection`). A 50 % edge
  therefore gives at most half of the stroke, however many dabs or passes the stroke makes; it
  never darkens towards a hard edge.
- *Smudge:* it reads the layer, so it works on an unclipped stroke-local copy of the tiles it
  touches. It writes `before + (work − before) × selection` once. Smudge can therefore pick up
  colour from outside the selection, but it writes only inside.
- *Unchanged rules:* layer locks, hidden layers and one undo step per stroke.

**Transforms** (`TransformSession`; the Transform tool or Ctrl+T).
- *What moves:* with a selection, the selected pixels of the active layer; without one, the whole
  active layer's content (cropped to its non-transparent bounds).
- *Begin caches the source once:*
  - `source = layer × s`;
  - `base = layer − source` (exactly complementary, rounding included);
  - 2× reductions of the source for prefiltering large reductions.
- *Preview:* every preview is rendered from that source, never from an earlier preview:
  - into a separate preview layer and preview composite (shallow copies of the document's
    layers with the active one swapped);
  - the view shows that composite, and the document is not touched;
  - while dragging: bilinear; on release, numeric edits and Apply: Catmull–Rom bicubic.
- *Resampling:*
  - colour is resampled premultiplied, so transparent pixels carry no colour into edges;
  - overshoot past opaque scales colour with alpha, so edges get no light or saturated fringe;
  - colour is clamped to alpha;
  - a whole-pixel move or flip (no scale, no rotation) is a pixel copy (moves snap to whole
    pixels), so ink stays exactly sharp;
  - rendering is split across a dedicated thread pool, with the UI thread taking part.
- *Geometry:*
  - eight scale handles: corners scale both axes, edges one. "Keep ratio" is on by default and
    Shift inverts it while dragging; Alt scales from the centre;
  - a rotation knob above the top edge, and dragging outside the box rotates (Shift snaps to 15°);
  - a movable pivot. Its target is small, and smaller on small boxes, so dragging the middle of a
    small selection moves it;
  - inside the box moves;
  - numeric X/Y (box centre), W/H (pixels, kept proportional with Keep ratio), angle;
  - Flip H/V around the pivot, and Reset.
- *Limits:* scale is clamped to at most 32× and at least 1 px; non-finite input is ignored.
  Rendering covers only (source ∪ target) ∩ canvas, so no buffer depends on the scale.
- *Apply* (Enter or the button): renders the result once more from the source into the layer,
  inside a tile snapshot. It is one undo step: the pixels together with the selection, which
  **follows the transformed pixels** (the source's mask, transformed bilinearly), so shading the
  moved part works at once. An unchanged box records nothing.
- *Cancel* (Esc, the button, or Undo while transforming) drops the preview. The document was
  never changed, so it is exactly as before.
- *Pixels beyond the canvas:* while the preview is open, the source is kept whole, so you can
  move it off the canvas and back without loss. On Apply, pixels outside the canvas are **cut
  off**, because layers are canvas-sized. The transform is destructive once applied, except for
  undo.
- *Other actions during a preview:*
  - layer actions (select another layer, add, delete, reorder, properties, opacity, Clear) and
    switching tools apply the transform first;
  - Save, Save As, Export and Open are refused with "Apply or cancel the transform first";
  - New and any document replacement cancel it;
  - autosave and recovery write the document as it was before the transform (the preview is
    never in it), and include the result after Apply;
  - closing the window discards the preview, and the unsaved-changes prompt is as before.
- *Memory while transforming:* a copy of the layer and of the composite (24 MiB at
  2048 × 1536), the base (12 MiB, only with a selection) and the source with its reductions
  (about 1.33 × the source).

**Copy, Cut and Paste.**
- *Copy:* takes the active layer's pixels × selection (soft edges become partial alpha), trimmed
  to the visible bounds, with their position. Without a selection it takes the whole layer.
- *Cut:* copies, then leaves `layer × (1 − selection)`, one undo step ("Cut"), so pasting the
  copy back in place recomposes the original (exactly for hard edges).
- *Paste:* puts the clipboard on a new layer above the active one, at its original position (one
  undo step, "Paste"). It is centred when it came from a canvas of another size or would land off
  the canvas.
- *Clipboard:* internal. Copy also offers the image to the system clipboard, and Paste uses a
  system image (centred, at its own pixel size) when the internal clipboard is empty.
  Interchange with other applications has not been tested (the tests run on the offscreen
  platform).
- *Delete:* clears the selected pixels; Clear Layer does the same when there is a selection.

**Milestone 9 additions** (research: `research/milestone9-*.md`, decisions D7 and D8).
- *Several layers.*
  - Ctrl/Shift+click, or a long press, adds a layer row to the layer selection. A plain click
    selects one layer. Layer selection is view state, not history.
  - Transform, Copy, Cut and Clear act on all selected layers:
    - one `TransformSession` with one geometry and one part per layer, so the layers stay aligned;
    - Apply prepares every layer's result first and then commits them as **one** group undo step;
    - a locked or hidden selected layer refuses the action with a message naming it (Krita skips
      such layers silently).
  - Paste creates one layer per copied layer, above the topmost selected one, in one undo step.
- *Preview of several adjacent layers without a selection.* It is drawn from one merged copy:
  6–8× faster, measured. Apply still transforms each layer exactly. With a selection, or with
  non-adjacent layers, each layer is previewed.
- *Thread safety.* Transform rows render on a thread pool into a buffer detached on the calling
  thread. Calling `scanLine()` from the workers on a shared preview copy was a data race, found by
  a benchmark crash and fixed.
- *New selection tools.*
  - Polygon lasso: click corners; close with Enter, a double-click or the first point; Backspace
    removes a corner, Esc cancels.
  - Magic wand: tolerance, contiguous or global, active layer or all visible layers,
    anti-aliased edge.
  - Grow and shrink by a Euclidean distance; feather as three box blurs.
  - Moving or transforming the selection outline only.
  - Each is one selection undo step. Brushes still apply a feathered selection exactly once.
- *Undo budget.* An edit larger than the whole 128 MiB budget is still not kept (milestone 1
  rule). The canvas now shows a notice saying it cannot be undone; large multi-layer transforms
  can reach this.

## Undo and redo

- **Commands** (`UndoCommand`), each already applied when recorded; `swap()` both undoes and redoes:
  - *pixels*: one layer's changed tiles (brush stroke, eraser stroke, Clear Layer);
  - *presence*: a layer inserted (Add, Duplicate) or removed (Delete); the command holds the
    layer while it is out of the document;
  - *properties*: name, visibility, opacity, lock (one command per action);
  - *move*: a layer's position.
  Commands find their layer by id, so undo after reordering or deleting other layers targets the
  right layer.
- **Unit of undo**: one stroke however it ends; one panel action; one opacity-slider drag (the
  slider previews live without history, the release records one command from the drag's start
  value; a drag that ends at its start value records nothing).
- **Budget**: 128 MiB for undo and redo together, counting the tiles history keeps alive and the
  painted tiles of removed layers (undoing Add or Duplicate moves that layer into the redo entry,
  which is counted too). Since milestone 10 an undo record refers to the old tiles instead of
  copying them: a stroke over unpainted tiles records "was transparent" and costs nothing, and a
  tile the layer still shares is not counted. The count is recomputed after every push, undo and
  redo; when over budget the oldest undo entries go first, then the farthest redo entries. A
  duplicated layer shares tiles with its source until either is edited, but history counts its
  tiles in full (conservative).
- **Not in history**: active-layer selection, zoom, pan, window size, tool/size/color.
- **Unsaved state**: `stateId()` compared with the id recorded at save/open/new, as before.

## Project files (.drawapp)

Defined in `src/core/ProjectFile.h`.
- **Version 3** (written since milestone 10): the version 2 header and metadata plus the tile
  size (64) and each layer's tile count; each layer block lists only its painted tiles
  (column, row, zlib block of the tile's pixels, BGRA premultiplied, exact), in row-major order.
  Layers are never flattened when saving. Byte layout in `ProjectFile.h`; compatibility in
  `docs/m10-file-compatibility.md`.
- **Version 2** (milestones 3–9, read): one zlib block per dense layer (8-bit RGBA premultiplied);
  opens into tiled layers, fully transparent tiles not allocated. Saving writes version 3.
- **Version 1** (milestone 1, read only): opens as a one-layer document; saving writes version 3.
- **Validation before allocation**: dimensions (≤ 16384 per side, ≤ 256 Mpx per layer) and layer
  count (1–64) from the header, plus, for the dense versions 1 and 2, total layer pixels
  (≤ 512 MiB); then exact length, checksum, metadata (unique non-zero ids, names ≤ 256
  characters, opacity 0–1, block sizes that add up, tile size a power of two from 16 to 1024),
  every tile's position (inside the canvas, increasing order, no repeats) and declared raw size
  before decompressing, and premultiplied validity. Version 3 tiles are allocated against
  `MemoryBudget`; a project that does not fit is refused with a message and nothing is kept.
  The file itself is read whole, so its size is reserved against the budget before reading.
  An unknown active layer id falls back to the top layer.
- **Saving**: see "Save guarantees" below. **Opening** decodes into a separate `DocumentData`;
  only a fully valid document replaces the open one.

## Save guarantees: local files vs Android document providers (milestone 5)

Every save first encodes the project in memory and checks the bytes with
`ProjectFile::verify`: structure, SHA-256, and every tile decompressed to the right size and
checked, one at a time. Bytes that fail the check are never written. Then:

| | Local file (desktop path) | Android storage-provider document (`content://`) |
|---|---|---|
| How it is written | `QSaveFile`: temporary file next to the destination, then rename over it | In place: the storage access framework gives access to that one document only, with no way to create a sibling file and rename it over |
| Old contents if the write fails or is interrupted | **Intact** (atomic replace) | Intact if the failure happens before writing starts (access, space, reservation, local copy). **Can be damaged** if it happens during the write |
| Free space | Checked first (`QStorageInfo`, needs file size + 1 MiB); ENOSPC during the write is reported | Checked first with the free space the provider's file descriptor reports (`fstatvfs`); then the full size is reserved with `posix_fallocate`, so "storage full" is found before any byte changes, where the storage supports it |
| Overwriting existing contents | Normal Save | Asked first, once per document per session: **Overwrite** / **Save a Copy…** (Save As to a new document) / **Cancel**. A document the picker has just created (empty) is not asked about |
| Local copy while writing | Not needed (old file intact) | The exact state being saved is written to the crash-recovery file (or, without recovery, `<app data>/staging/unsaved-*.drawapp`) and read back **before** the destination is touched. It is released only after the destination has been written and read back |
| After writing | Rename | `ftruncate` + `fsync`, then the document is **read back and compared**; a mismatch is a failure |
| Access revoked / document deleted | Open error | Found by the probe before writing (SecurityException / FileNotFoundException), so nothing is touched |
| Drawing after any failure | Open, unsaved | Open, unsaved, recoverable. If the failure may have damaged the file the drawing came from, the drawing is marked unsaved even if it had no changes, so the recovery data is kept |

Not covered on either side: OS crashes and power loss beyond what `fsync` provides; a provider
that reports success but stores something else later (the read-back only proves what the
provider returned at that moment); cloud providers that upload asynchronously after `close()`.
Stream-only providers (no seekable file) get no space reservation and a truncating write.

PNG export uses the same paths. It is not asked about before overwriting, because the drawing
itself is never at risk from an export, and it keeps no local copy.

## Asynchronous save and open (milestone 4)

`ProjectIo` (src/core/ProjectIo.h) owns a one-thread `QThreadPool`. `save(snapshot, path,
options)`, `open(path)` and `exportPng(snapshot, path, options)` run `SafeSave::saveProject`,
`ProjectFile::load` and the PNG encode + `SafeSave::writeExport` there, and deliver
`saveFinished` / `openFinished` / `exportFinished` through a queued call back on the UI thread.
Only one operation runs at a time.

`CanvasItem` drives it as a small state machine:
- **Save** takes `LayeredDocument::snapshot()`. This is the same copy-on-write value used for
  recovery, so later edits detach their layer and never reach the worker. Save also records the
  undo `stateId` at that moment. On success, only that `stateId` becomes the saved state: edits
  made during the save stay unsaved, and the recovery file is deleted only if nothing newer
  exists. On failure, the path, the saved state and the previous file are unchanged.
- **Save during a save** to the same path is queued; when the first save finishes, the queued
  one starts from a fresh snapshot, so newer data always lands last. Save As to a different path
  while busy is refused. Opening while saving is refused.
- **Open** records the document id and content revision when it starts. The decoded document
  replaces the current one only if neither changed meanwhile. Otherwise the result is held and
  the user chooses "Open the file" or "Keep my drawing" (`openConflict`,
  `resolveOpenConflict`). The UI disables painting and the layer panel while opening, so this
  path is a safeguard.
- **Main.qml** chains actions after a save (`afterSave`). New, Open and Close run only after the
  requested save succeeded, and prompt again if the document changed while it ran. A failed or
  cancelled save cancels the action. Closing during a save is deferred until it finishes. The
  footer shows a busy indicator and "Saving…" / "Opening…" / "Exporting PNG…".
- **Overwrite confirmation** (provider documents): the worker probes the destination before
  writing anything. If it has contents and the user has not confirmed this document in this
  session, the save ends with `NeedsOverwriteConfirmation`. `CanvasItem` keeps the request and
  emits `overwriteConfirmationNeeded`, and `afterSave` stays pending. `confirmOverwrite()`
  restarts the save. `cancelOverwrite()` reports a cancelled save. "Save a Copy…" cancels and
  opens Save As, keeping the pending action.
- During a save, autosave does not delete the recovery file even when nothing is unsaved: it may
  be the save's local copy.

## PNG export

A fresh composite of the visible layers with their opacity (not the view cache, no checkerboard,
no view transform), composited, encoded and written on the file worker (milestone 5; earlier it
blocked the UI thread for 79–350 ms). It does not change the project path or the unsaved state.

## Crash recovery

As in milestone 2 (per-session `QLockFile`, orphan discovery, one worker thread, generation-gated
atomic commits, cleanup on save/new/open/discard), with these changes:
- **Snapshot**: `LayeredDocument::snapshot()` copies the layer list as plain values. Since
  milestone 10 layers share tiles copy-on-write, so a later write on the UI thread copies only
  the tiles it touches (one 16 KiB tile each, once per snapshot), not the whole layer as the
  milestone 9 `QImage` layers did. The worker therefore reads pixels that can no longer change
  and never touches the live document.
- **Format v2** embeds a layered project (version 3 since milestone 10; version 2 recovery files
  from earlier milestones embed version 2 projects and are still read), so the whole layer
  structure is recovered. Version 1
  recovery files (single image) are still read and offered as one-layer documents, and the
  Restore prompt says so.
- Opacity drags in progress count as unsaved changes for recovery.

## File handling and portability

QML `FileDialog` (native on Windows; storage access framework on Android) returns URLs. `file:`
URLs become local paths, other URLs (Android `content://`) are passed to `QFile` as strings.
Nothing assumes a fixed writable folder. For a `content://` URI, the document title comes
from `QFileInfo::fileName()`. On Android, Qt's content file engine returns the provider's
display name, while the URI's last segment is often only a document id.

The Android file and recovery flows were exercised on an emulator (BUILD.md), not yet on a
physical device. iOS is unverified.

## Decisions

| Decision | Reason |
|---|---|
| Full-canvas image per layer, no tiles | Simplest; memory and timings measured (BENCHMARKS.md) before considering tiles |
| Cached composite, refreshed per changed rect | Stroke cost stays flat with layer count (measured); renderer unchanged |
| Typed undo commands addressed by layer id | Undo stays correct after reorder/delete; one small switch, no framework |
| Removed-layer images counted in the undo budget | Deleting layers cannot grow memory without bound |
| Copy-on-write snapshot for recovery | UI-thread cost independent of layer count; detach cost paid once per edited layer |
| One limit set shared by editor and file reader | Every document the app creates can be reopened |
| Lock = pixel lock only | Predictable: lock prevents accidental painting, not organising layers |
| Static QML module (`drawapp_ui`) | The UI test loads the real Main.qml |
| Per-session recovery files with `QLockFile`; generation-gated commits | From milestone 2 |
| Uniform stroke opacity via a max-coverage mask recomposited from the undo tiles | No extra full-size buffer (the undo snapshot already holds the pre-stroke pixels); per-event cost measured equal or lower than the QPainter dabs it replaces |
| Distance-based smoothing, not time-based | Independent of event rate and timestamps (tested); no latency when the pen stops; deterministic in tests |
| Provider saves keep the local copy in the recovery file | Survives a crash during the write and is offered by the existing Restore prompt; no second staging mechanism when recovery runs |
| Provider access through raw JNI for throwing calls | `QJniObject` clears Java exceptions without reporting their type; the type distinguishes revoked access from other errors |
| MinGW 15.1 from MinGW-Builds (GitHub), SHA-256 pinned | Qt's repository metadata points at 13.1 but Qt was built with 15.1 |
| One brush renderer (`StrokeEngine`) for every preset, scratchpad and thumbnail | Presets differ in data only; one place to test, measure and fix |
| Coverage per stroke (16-bit sparse tiles), recomposited from the undo tiles | Opacity applied once; uniform and build-up from one model; no full-size buffer |
| Flow normalised by spacing and tip pass length | "Flow" means one pass whatever the spacing or tip; found necessary on the brush sheet (bristle strokes were half as strong) |
| Texture in document space | Grain stays coherent along and across strokes; no stamp-shaped texture repeats |
| Airbrush from elapsed time with a timer | Buildup does not depend on how many events a device sends |
| Size is a temporary value, never auto-saved | Resizing while drawing must not change a saved preset |
| Generated tips and textures, no image files | Provenance is simple and nothing third-party ships |
| Selection = separate document-space coverage with distinct "none" and "empty" | An empty result can never let an edit apply everywhere (a risk seen in a reference implementation) |
| Add = saturating sum (as Krita), not max | Adjacent anti-aliased shapes join without a seam |
| Selection steps in history but not "unsaved"; selections not saved | Undoable like other editors, without changing file semantics or nagging about unsaved changes |
| Transform preview in separate images, rendered from the cached source | The document is never touched before Apply; Cancel is exact; no repeated resampling |
| Catmull–Rom final filter, overshoot scaled with alpha, copies for whole-pixel moves | Interpolating (moves and flips stay exact) without fringes; full-layer moves cost a copy, not a resample (measured 50 → 1.6 ms) |
| The selection follows the pixels on Apply | Continue shading the moved part (as in Krita) |
| One engine with data-driven engines, sensors and a property registry (milestone 9) | Brushes stay data; every property is declared once and tested for a rendering effect (decisions D2–D4) |
| Missing sensors are left out, never defaulted | Mouse, touch and pens without tilt behave as before whatever the curves say |
| Milestone 6 pressure responses kept, multiplied with dynamics | Version 1 presets render unchanged; the quick panel stays simple |
| Multi-layer transform: shared geometry, prepare-then-commit, refuse locked/hidden layers | Alignment, atomicity and no silent partial edits (decision D7) |
| Merged-copy preview for adjacent layers without a selection | Measured 6–8× faster preview; Apply stays exact per layer |

## Limitations (known)

Timings below were measured on the milestone 9 (dense) storage; milestone 10 re-measures them
on tiled storage (`docs/m10-performance.md`) and this list is updated with those numbers.

- **Crash recovery has a loss window** (interval + write time; it grows with layer count because
  the worker re-encodes every layer: about 0.37 s per write at 5 layers and 0.8 s at 10, measured).
  Only process kills were tested; OS crash or power loss may lose unflushed data.
- Save, open and export run on a worker, but take about 0.35–0.55 s / 0.12–0.15 s at 5 layers
  and 0.75–1.4 s / 0.24–0.36 s at 10 layers (2048 × 1536, representative content; verification
  adds 0.1–0.22 s). During that time other file operations wait.
- Saving to an Android `content://` document is not atomic: a write that fails part way can
  damage that file (see "Save guarantees"). The drawing and a local copy are kept.
- Brushes: see `docs/milestone9-known-limitations.md`. Wet paint is a prototype (no diffusion,
  drying or pigment mixing); spray and hatching are subsets; no bristle, clone or sketch engines;
  26 of the 120 designed properties are planned only. Smoothing cuts corners by design (by up to
  the string length) and is not a curve fitter.
- Selections: no colour range or border. The outline is static, not animated. Selections are
  not saved in files.
- Transforms: no perspective, skew, warp or mesh. Pixels moved off the canvas are cut off on
  Apply. Apply of many large layers runs on the UI thread (2.3–3.2 s for 7 × 4096², measured).
- During a transform drag on the whole 2048 × 1536 layer, a preview frame costs up to about 20 ms
  plus the composite (12 ms at 10 layers). Nine adjacent layers take 25–33 ms (merged preview);
  7 × 4096² take 152–262 ms. Measured on a desktop CPU; phones are unmeasured.
- Clipboard exchange with other applications is untested.
- Drawing feel (pen pressure response, tilt, latency) is **unverified on hardware**: pressure and
  tilt were exercised only with synthetic tablet events and the emulator's touch input.
- Large brushes cost more per dab: up to about 2–4 ms for an event that places a 200 px dab
  (BENCHMARKS.md). There is no GPU path.
- When a touch is cancelled by the system, a move that Qt Quick was still holding back for the
  next frame is lost; the stroke ends at the last move that arrived.
- A full composite rebuild (visibility, opacity, order) takes about 5.6 ms at 5 layers and 12 ms
  at 10; an opacity drag recomposites on every slider move.
- Memory: 16 KiB per painted 64 x 64 tile in each layer and in the composite (a fully painted
  2048 x 1536 layer is still 12 MiB), plus up to 128 MiB of history, all within the
  `MemoryBudget` limit. The selection mask is still one dense byte per canvas pixel (256 MiB at
  16384 x 16384).
- The view still assembles the visible part of the composite into one dense image per repaint;
  zoomed far out on a very large canvas that image can be refused by the memory budget (the view
  then shows the checkerboard). A reduced-resolution view pyramid is milestone 10 stage 6.
- Normal blending only; no groups, clipping, masks, vector layers or other blend modes.
- Layer drag-and-drop reordering is not implemented (Up/Down buttons only).
- Not a final painting engine: no GPU brush; input-to-display latency unmeasured.
- High-DPI checked only at 200 % scaling. Pen buttons and palm rejection untested on hardware.

## Future requirements (noted, not built)

More blend modes, groups, clipping and masks; tiled or sparse layer storage if memory requires
it; folder-based saving (ACTION_OPEN_DOCUMENT_TREE) that would allow a temporary sibling document
and rename on providers that support it; incremental recovery writes (only changed layers); the document model may
later hold editable strokes.

## Licensing notes

- Qt is used under LGPLv3 (open-source binaries). Windows and Android link Qt dynamically.
  **Open decision:** iOS links Qt statically; LGPLv3 compliance there needs a deliberate approach
  or a commercial license.
- MinGW runtime DLLs: GPL with the GCC runtime library exception.
- No Krita code is used. See `research/krita-input-and-canvas.md` for the provenance note.
- Brush system: Krita (GPL), PhotoCraft and libmypaint (ISC) were read for concepts only
  (`research/brush-system.md`); no code, presets, tips or textures were copied. Our tips and
  textures are generated by our own code (see "Brush tips and textures: provenance").

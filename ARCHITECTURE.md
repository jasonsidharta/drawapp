# Architecture

State: milestone 3 (raster layers) on top of milestone 2 (crash recovery), milestone 1 (undo/redo,
project save/open, PNG export) and the milestone 0 prototype. Everything described here exists in
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
  DocumentData     plain values: LayerData {id, name, image, visible, opacity, locked},
                   DocumentData {size, layers, activeLayerId}; compositeLayers(); shared limits
  LayeredDocument  the open document: ordered layers (index 0 = bottom), active layer, id
                   allocation, cached composite of the visible layers
  RasterDocument   raster operations on one image: stampDabs (brush/eraser), clear
  TileSnapshot     copies of the 64x64 tiles an edit touched; swap = undo/redo
  UndoHistory      UndoCommand (pixels / layer presence / properties / move) + linear history
                   under one byte budget; state ids for "unsaved"
  ProjectFile      .drawapp v2 (layered) encode/decode, v1 reader, safe writes, PNG export
  RecoveryFile     .drawrecovery v2 (metadata + embedded layered project), v1 reader
  RecoveryManager  session lock, orphan discovery, one-at-a-time worker writes with stale checks
  ViewTransform, Pressure.h, StrokeSpacer, ViewRenderer   (input, spacing, view; unchanged)
src/app/    (static QML module "drawapp_ui", URI DrawApp, used by the app and tst_ui)
  CanvasItem       QQuickPaintedItem; input, strokes, layer commands, history, files, recovery
  Main.qml         window, toolbar, File menu, layer panel, dialogs, notices, shortcuts
  main.cpp         application setup (in the drawapp executable)
tests/             tst_core, tst_document, tst_recovery (headless); tst_canvas, tst_ui (window,
                   offscreen under ctest); bench_core
```

## Layers

- **Model**: each layer has a stable id (never reused in a document, saved in files), a name, a
  full-canvas premultiplied ARGB32 image, visibility, opacity (0–1) and a lock. The document has
  an ordered layer list and an active layer. All layers have the canvas size; canvas sizes are
  unchanged from earlier milestones (new documents 2048 × 1536).
- **Compositing**: Normal (source-over) only. `compositeLayers` clears a rect and draws each
  visible layer bottom to top with `QPainter::setOpacity`. `LayeredDocument` keeps a composite
  cache (one more canvas-sized image). Brush/eraser events refresh only the dabs' rect;
  visibility, opacity, order, add/delete/duplicate refresh the whole canvas. The view draws the
  cache exactly as before (renderer unchanged), so the checkerboard appears behind the whole
  stack, and erasing an upper layer shows the layers below.
- **Painting**: brush and eraser affect only the active layer, chosen when the stroke starts.
  Strokes, and Clear Layer, are refused on hidden or locked layers with a notice explaining why
  (the canvas emits `notice`, QML shows a banner). The lock blocks pixel edits only; renaming,
  hiding, reordering and deleting a locked layer are allowed.
- **Panel** (Main.qml): list (top layer first) with visibility check box, name, opacity and lock;
  Add (above the active layer), Duplicate, Delete, Up, Down, Rename (also double-click), and an
  opacity slider for the active layer. At least one layer is always kept. Layer count is limited
  to `min(64, 512 MiB / layer size)` (42 at 2048 × 1536), the same limit the file reader enforces.
- **Active layer rules**: selecting a layer is not an edit (no history entry, not "modified").
  New and duplicated layers become active. Deleting the active layer selects the layer below
  (or, for the bottom layer, the new bottom). Undo/redo that re-inserts a layer selects it; other
  undo/redo steps keep the selection unless the selected layer disappears.

## Drawing data flow

1. **Input** reaches `CanvasItem`: mouse (left draws; middle or Space + left pans), pen (window
   event filter, because `QQuickItem` has no tablet handler), touch (one finger draws, two pan and
   zoom), wheel (zoom about the cursor).
2. **Pressure** only for pens reporting the `Pressure` capability; everything else uses 1.0.
3. **Coordinates** go through `viewToDoc`; document pixels never depend on the view.
4. **Spacing**: a dab every `max(0.5 px, 0.15 × last dab diameter)`.
5. **Raster**: `RasterDocument::stampDabs(layerImage, …)` captures the tiles under each dab into
   the stroke's `TileSnapshot`, draws the dabs, and returns the changed rect.
6. **Composite and repaint**: the composite is refreshed for that rect, which is then repainted.

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
- **Budget**: 128 MiB for undo and redo together, counting tile copies and the images of removed
  layers held by history (a deleted layer counts 12 MiB at 2048 × 1536; undoing Add or Duplicate
  moves that layer's image into the redo entry, which is counted too). The count is recomputed
  after every push, undo and redo; when over budget the oldest undo entries go first, then the
  farthest redo entries. A duplicated layer shares pixels with its source until either is edited,
  but history counts it in full (conservative).
- **Not in history**: active-layer selection, zoom, pan, window size, tool/size/color.
- **Unsaved state**: `stateId()` compared with the id recorded at save/open/new, as before.

## Project files (.drawapp)

Defined in `src/core/ProjectFile.h`.
- **Version 2** (written): 68-byte header (magic, version, width, height, layer count, metadata
  length, payload size, SHA-256 of the payload), then JSON metadata (active layer id; per layer:
  id, name, visibility, opacity, lock, compressed size) and one zlib block per layer (8-bit RGBA
  premultiplied, exact). Layers are never flattened when saving.
- **Version 1** (milestone 1, read only): opens as a one-layer document; saving writes version 2.
- **Validation before allocation**: dimensions (≤ 16384 per side, ≤ 64 Mpx per layer), layer
  count (1–64) and total layer pixels (≤ 512 MiB) are checked from the header; then exact length,
  checksum, metadata (unique non-zero ids, names ≤ 256 characters, opacity 0–1, block sizes that
  add up), each block's declared raw size before decompressing, and premultiplied validity.
  An unknown active layer id falls back to the top layer.
- **Saving** encodes in memory and writes through `QSaveFile` (atomic replace); `content://` URIs
  are written directly (untested). **Opening** decodes into a separate `DocumentData`; only a fully
  valid document replaces the open one.

## PNG export

A fresh composite of the visible layers with their opacity (not the view cache, no checkerboard,
no view transform), written as straight-alpha PNG at document resolution through the same safe
write path. It does not change the project path or the unsaved state.

## Crash recovery

As in milestone 2 (per-session `QLockFile`, orphan discovery, one worker thread, generation-gated
atomic commits, cleanup on save/new/open/discard), with these changes:
- **Snapshot**: `LayeredDocument::snapshot()` copies the layer list as plain values. Layer images
  are `QImage`s shared copy-on-write: the copy takes under 0.01 ms whatever the layer count, and
  any later write on the UI thread (dab, undo swap, clear) detaches that one layer first (about
  2.3–3.2 ms for a 2048 × 1536 layer, once per snapshot and edited layer). The worker therefore
  reads pixels that can no longer change and never touches the live document.
- **Format v2** embeds a version 2 project, so the whole layer structure is recovered. Version 1
  recovery files (single image) are still read and offered as one-layer documents, and the
  Restore prompt says so.
- Opacity drags in progress count as unsaved changes for recovery.

## File handling and portability

QML `FileDialog` (native on Windows; storage access framework on Android) returns URLs. `file:`
URLs become local paths, other URLs (Android `content://`) are passed to `QFile` as strings.
Nothing assumes a fixed writable folder. Android and iOS file and recovery flows are unverified.

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
| MinGW 15.1 from MinGW-Builds (GitHub), SHA-256 pinned | Qt's repository metadata points at 13.1 but Qt was built with 15.1 |

## Limitations (known)

- **Crash recovery has a loss window** (interval + write time; it grows with layer count because
  the worker re-encodes every layer: about 0.37 s per write at 5 layers and 0.8 s at 10, measured).
  Only process kills were tested; OS crash or power loss may lose unflushed data.
- **Save and open run on the UI thread** and grow with layer count: about 0.37 s / 0.14 s at
  5 layers and 0.78 s / 0.25 s at 10 layers (2048 × 1536, representative content). The UI pauses.
- A full composite rebuild (visibility, opacity, order) takes about 5.6 ms at 5 layers and 12 ms
  at 10; an opacity drag recomposites on every slider move.
- Memory: 12 MiB per layer plus the 12 MiB composite, plus up to 128 MiB of history.
- Normal blending only; no groups, clipping, masks, vector layers or other blend modes.
- Layer drag-and-drop reordering is not implemented (Up/Down buttons only).
- Not a final painting engine: no tiled storage, no GPU brush; input-to-display latency unmeasured.
- High-DPI checked only at 200 % scaling. Pen buttons and palm rejection untested on hardware.

## Future requirements (noted, not built)

More blend modes, groups, clipping and masks; tiled or sparse layer storage if memory requires
it; background saving; incremental recovery writes (only changed layers); the document model may
later hold editable strokes.

## Licensing notes

- Qt is used under LGPLv3 (open-source binaries). Windows and Android link Qt dynamically.
  **Open decision:** iOS links Qt statically; LGPLv3 compliance there needs a deliberate approach
  or a commercial license.
- MinGW runtime DLLs: GPL with the GCC runtime library exception.
- No Krita code is used. See `research/krita-input-and-canvas.md` for the provenance note.

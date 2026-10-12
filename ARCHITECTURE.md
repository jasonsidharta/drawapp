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
  RasterDocument   raster operations on one image: stampDabs (test fixtures), clear
  StrokeSmoother   distance-based ("pull string") smoothing of input samples
  StrokeSpacer     evenly spaced dabs; PressureResponse (pressure -> size curve)
  StrokeCompositor uniform-opacity stroke rendering into a layer (coverage mask + undo tiles)
  TileSnapshot     copies of the 64x64 tiles an edit touched; swap = undo/redo
  UndoHistory      UndoCommand (pixels / layer presence / properties / move) + linear history
                   under one byte budget; state ids for "unsaved"
  ProjectFile      .drawapp v2 (layered) encode/decode/verify, v1 reader, local safe writes
  SafeSave         writing projects and exports: local files atomically, provider documents
                   with a local copy, probing, space reservation and read-back
  DocumentProvider destination access for content:// documents (Android: ContentResolver via
                   JNI in AndroidDocumentProvider.cpp; desktop: QFile stand-in; tests: a fake)
  ProjectIo        one worker thread for save, open and PNG export
  RecoveryFile     .drawrecovery v2 (metadata + embedded layered project), v1 reader
  RecoveryManager  session lock, orphan discovery, one-at-a-time worker writes with stale checks
  ViewTransform, Pressure.h, ViewRenderer   (view and input policy; unchanged)
src/app/    (static QML module "drawapp_ui", URI DrawApp, used by the app and tst_ui)
  CanvasItem       QQuickPaintedItem; input, strokes, layer commands, history, files, recovery
  BrushPreview     live preview of the brush settings (same smoother/spacer/compositor)
  Main.qml         window, toolbar, brush settings, File menu, layer panel, dialogs, shortcuts
  main.cpp         application setup (in the drawapp executable)
tests/             tst_core, tst_document, tst_recovery (headless); tst_canvas, tst_ui (window,
                   offscreen under ctest); FakeProvider.h; bench_core, bench_canvas
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
2. **Pressure** only for pens reporting the `Pressure` capability; everything else uses 1.0, so
   mouse and touch always draw at full size whatever the pressure settings.
3. **Coordinates** go through `viewToDoc`; document pixels never depend on the view.
4. **Settings** are captured when the stroke starts: size, color, opacity, pressure response,
   smoothing, tool and layer. Changing them mid-stroke affects the next stroke.
5. **Smoothing** (`StrokeSmoother`, optional): the brush point hangs on a string of length
   `smoothing / 100 × 40` view pixels (converted to document pixels at stroke start) and moves only
   when the pointer pulls the string tight. Input segments are walked in 0.5 px steps, so the
   result depends on the path, not on the event rate or speed. The lag is spatial, not temporal:
   no timers and no delay when the pointer stops. When the stroke ends (release, lost grab,
   cancelled touch, or an undo/save mid-stroke), the line is completed straight to the last input
   position. The release event's position is used, but not its pressure, since pens often report
   near zero at lift-off.
6. **Spacing and pressure** (`StrokeSpacer`): a dab every `max(0.5 px, 0.15 × last dab diameter)`,
   diameter `size × (min + (1 − min) × p^γ)`, with `min` = "Lightest" (default 10 %) and
   γ = 3^firmness (Soft 1/3 … Linear 1 … Firm 3).
7. **Raster** (`StrokeCompositor`): each dab's anti-aliased coverage goes into a per-stroke mask,
   keeping the maximum, never the sum. Pixels under the stroke are recomputed from the layer as it
   was before the stroke, which the stroke's undo `TileSnapshot` holds anyway:
   paint = `before` under `(color, opacity × coverage)`; erase = `before × (1 − opacity × coverage)`.
   **Opacity is therefore uniform within a stroke**: overlapping dabs, slow segments and
   self-crossings do not darken. Separate strokes build up, since the next stroke's "before"
   includes the previous one. At 100 % the result is the familiar opaque stroke. The mask
   allocates 4 KiB per 64 × 64 tile the stroke touches.
8. **Composite and repaint**: the composite is refreshed for the changed rect, which is then
   repainted. One stroke is one undo step (the `TileSnapshot`), as before.

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
- **Saving**: see "Save guarantees" below. **Opening** decodes into a separate `DocumentData`;
  only a fully valid document replaces the open one.

## Save guarantees: local files vs Android document providers (milestone 5)

Every save first encodes the project in memory and checks the bytes with
`ProjectFile::verify`: structure, SHA-256, and every layer block decompressed to the right
size, one layer at a time. Bytes that fail the check are never written. Then:

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

## Limitations (known)

- **Crash recovery has a loss window** (interval + write time; it grows with layer count because
  the worker re-encodes every layer: about 0.37 s per write at 5 layers and 0.8 s at 10, measured).
  Only process kills were tested; OS crash or power loss may lose unflushed data.
- Save, open and export run on a worker, but take about 0.35–0.55 s / 0.12–0.15 s at 5 layers
  and 0.75–1.4 s / 0.24–0.36 s at 10 layers (2048 × 1536, representative content; verification
  adds 0.1–0.22 s). During that time other file operations wait.
- Saving to an Android `content://` document is not atomic: a write that fails part way can
  damage that file (see "Save guarantees"). The drawing and a local copy are kept.
- Brush: round dabs only, Normal blending, no texture, no pressure → opacity, no tilt; smoothing
  cuts corners by design (by up to the string length) and is not a curve fitter.
- When a touch is cancelled by the system, a move that Qt Quick was still holding back for the
  next frame is lost; the stroke ends at the last move that arrived.
- A full composite rebuild (visibility, opacity, order) takes about 5.6 ms at 5 layers and 12 ms
  at 10; an opacity drag recomposites on every slider move.
- Memory: 12 MiB per layer plus the 12 MiB composite, plus up to 128 MiB of history.
- Normal blending only; no groups, clipping, masks, vector layers or other blend modes.
- Layer drag-and-drop reordering is not implemented (Up/Down buttons only).
- Not a final painting engine: no tiled storage, no GPU brush; input-to-display latency unmeasured.
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

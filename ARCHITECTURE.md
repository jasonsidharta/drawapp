# Architecture

State: milestone 1 done (undo/redo, project save/open, PNG export) on top of the milestone 0
prototype. Everything under "Current" exists in the code; anything else is labelled *planned*.

## Stack

- C++17, Qt 6.12.0 (Qt Quick, Qt Quick Controls with the Fusion style, Qt Quick Dialogs, QtCore
  QML types), CMake ≥ 3.22, Ninja.
- Windows toolchain: MinGW-w64 GCC 15.1.0 (the compiler Qt 6.12.0's MinGW binaries were built with).
- Android toolchain: NDK r27c (27.2.12479018, Clang 18), JDK 21, compile/target SDK 36, min SDK 28.
- No third-party libraries beyond Qt. Compression is Qt's zlib (`qCompress`); checksums use
  `QCryptographicHash`.

Why Qt 6 with Qt Quick/QML: one C++ codebase for Windows, Linux, Android and iOS, with a GPU
scene graph, pen/touch input and native file dialogs on all four. No blocker found so far.

## Components (current)

```
src/core/   (static lib "drawcore", depends on QtGui only, no Qt Quick)
  ViewTransform    view = doc * zoom + pan; zoom about an anchor; fit/center; zoom clamp [0.05, 32]
  Pressure.h       effectivePressure(kind, deviceReportsPressure, raw) -> 0..1
  StrokeSpacer     input samples -> evenly spaced dabs, distance carried across samples
  RasterDocument   one QImage, ARGB32 premultiplied; new documents 2048x1536, transparent
  TileSnapshot     copies of the 64x64 tiles an edit touched; swap = undo/redo
  UndoHistory      linear undo/redo stacks under one byte budget; state ids for "unsaved"
  ProjectFile      .drawapp encode/decode, safe file writes, PNG export
  ViewRenderer     paints an exposed view rect: workspace, checkerboard, document through the view
src/app/
  CanvasItem       QQuickPaintedItem; input, stroke state, history, file commands, partial repaints
  Main.qml         window, toolbar, File menu, dialogs, unsaved-changes prompt, shortcuts
  main.cpp         application setup
tests/             tst_core, tst_document (headless), tst_canvas (real window), bench_core
```

## Drawing data flow

1. **Input** reaches `CanvasItem`:
   - Mouse: left button draws; middle button, or Space + left, pans.
   - Pen: a window-level event filter on the `QQuickWindow`, because `QQuickItem` has no tablet
     handler. Press must land inside the item; the event is accepted so Qt does not synthesize
     mouse events.
   - Touch: one finger draws; two fingers pan and zoom.
   - Wheel: zoom about the cursor (×1.25 per notch); a pixel-delta-only wheel pans.
2. **Pressure** is used only for a pen whose device reports the `Pressure` capability; everything
   else uses 1.0.
3. **Coordinates** are converted with `viewToDoc` first. Document pixels never depend on the view.
4. **Spacing**: a dab every `max(0.5 px, 0.15 × last dab diameter)` along straight segments;
   `diameter = size × (0.1 + 0.9 × pressure)`, at least 1 px. A press always emits one dab.
5. **Raster**: `RasterDocument::stampDabs` captures the tiles under each dab into the stroke's
   `TileSnapshot` (first touch only), then draws anti-aliased circles (source-over for the brush,
   destination-out for the eraser) and returns the changed rect.
6. **Repaint**: the changed rect is mapped to the view and passed to `update(rect)`.

Brush size, color and mode are captured when a stroke starts.

## Undo and redo

- **Unit of undo**: one brush stroke, one eraser stroke, or one Clear. A stroke becomes one step
  however it ends: release, lost grab, a second finger, or Undo/Redo/Save/Export/New/Open
  pressed mid-stroke (the stroke is committed first, so Undo removes it whole and stops it).
- **What is stored**: before pixels of each 64×64 tile the edit touched (16 KiB per full tile).
  Tiles left identical (for example erasing transparent pixels) are dropped at commit; an edit
  with no changed tiles is not recorded and does not mark the document modified. Clear stores
  only tiles that had paint. A short stroke stores a few tiles, not a canvas copy; the benchmark
  zig-zag stroke across the canvas stored 1.4–2.1 MiB (BENCHMARKS.md).
- **Undo/redo mechanics**: swapping the snapshot with the document exchanges old and new pixels,
  so an entry costs the same memory on the undo or redo side.
- **Budget**: 128 MiB (`UndoHistory::DefaultBudgetBytes`), counting tile pixel bytes of undo
  *and* redo entries together (per-entry bookkeeping overhead is not counted). Pushing a new edit
  first drops the redo branch, then evicts the oldest undo entries until the total fits. An edit
  larger than the whole budget is applied but cannot be undone. With a 12 MiB canvas the budget
  holds at least 10 full-canvas clears; typical strokes are far smaller. Not tuned per platform yet.
- **Not in history**: zoom, pan, window resize, tool/size/color changes.
- **Unsaved state**: every history entry has a unique id; `stateId()` is the id of the newest undo
  entry (or of the base state). The document is modified when `stateId()` differs from the id
  recorded at the last save/open/new. Undoing back to the saved state clears the mark; eviction
  keeps this correct because evicting an entry makes its id the base id.
- New and Open clear the history. History does not survive closing the app.

## Project files (.drawapp, format version 1)

Defined in `src/core/ProjectFile.h`: 68-byte little-endian header (magic `DRAWAPP\n`, version,
width, height, pixel format = 8-bit RGBA premultiplied, compression = zlib via `qCompress`,
payload size, SHA-256 of the payload) followed by the payload. Pixels are stored exactly as the
document holds them, so save/open is lossless, including partial alpha.

- **Saving** encodes the whole file in memory, then writes through `QSaveFile` (temporary file in
  the same folder, atomic rename over the target on commit). A failed open, write or commit leaves
  any previous file untouched and returns a message; the document stays marked unsaved. On Android,
  `content://` URIs from the storage access framework cannot be replaced atomically, so they are
  written directly (untested on a device).
- **Opening** reads into memory and validates before replacing anything: magic, version (newer
  versions are refused with a clear message), dimensions (≤ 16384 per side, ≤ 64 megapixels),
  known pixel format and compression, exact file length, checksum, the compressed stream's declared
  raw size (checked before decompressing), decompressed size, and premultiplied pixel validity.
  Files larger than the format allows are rejected from their size without being read.
  Only a fully decoded image replaces the open document.
- Opened documents keep their stored size (not necessarily 2048×1536).

## PNG export

`ProjectFile::exportPng` writes the document pixels at their own resolution as straight-alpha
PNG, through the same safe write path. It ignores the view (no workspace, checkerboard or zoom)
and does not change the project path or the unsaved-changes state.

## File handling and portability

QML `FileDialog` (native on Windows; storage access framework on Android) returns URLs.
`CanvasItem` converts `file:` URLs to local paths and passes other URLs (Android `content://`) to
`QFile` as strings. Save As and Export add `.drawapp` / `.png` to local paths chosen without an
extension. Nothing assumes a fixed writable folder; dialogs start in the platform's Documents or
Pictures location. Android and iOS file flows are not verified.

## Decisions

| Decision | Reason |
|---|---|
| CPU raster in one `QImage` | Simplest useful start; measured fast enough at this size |
| 64×64 tile snapshots for undo | Small strokes stay small without changing the raster storage |
| Swap-based undo/redo | Same memory whichever side an entry is on; budget accounting stays exact |
| One 128 MiB budget for undo + redo | Predictable ceiling; oldest history goes first |
| Raw premultiplied RGBA + zlib in a custom header | Lossless and exact; no PNG premultiply round-trip; small code |
| SHA-256 of payload | Detects truncation and corruption before decoding |
| `QSaveFile` | Atomic replace with Qt's tested implementation |
| Clear without a confirmation dialog | It is undoable now |
| Mouse ignored for 400 ms after any pen event | Windows can send OS-synthesized mouse events for pen input |
| MinGW 15.1 from MinGW-Builds (GitHub), SHA-256 pinned | Qt's repository metadata points at 13.1 but Qt was built with 15.1 |

## Limitations (known)

- **No crash recovery.** Unsaved work is lost if the app or OS dies; atomic saving only protects
  the last saved file. See ROADMAP.
- Save, open and export run on the UI thread: 87 ms / 33 ms / 150 ms for typical artwork, up to
  0.44 s / 0.14 s / 0.5 s worst case at 2048×1536 (BENCHMARKS.md). The UI pauses meanwhile.
- `QSaveFile` commit does not guarantee the data reached the disk before a power loss on every
  filesystem.
- Undo budget counts pixel bytes only; it is a fixed number, not adapted to device memory.
- Not a final painting engine: no tiles in the document itself, no worker threads, no GPU brush.
- No smoothing, prediction, pressure curve, tilt or rotation; fast curves become straight segments.
- High-DPI checked only at 200 % scaling. Pen buttons and palm rejection untested on hardware.

## Future requirements (noted, not built)

Crash recovery; layers and blend modes; tiled storage and partial texture uploads; background
saving; the document model may hold both raster layers and editable strokes.

## Licensing notes

- Qt is used under LGPLv3 (open-source binaries). Windows and Android link Qt dynamically.
  **Open decision:** iOS links Qt statically; LGPLv3 compliance there needs a deliberate approach
  or a commercial license.
- MinGW runtime DLLs: GPL with the GCC runtime library exception.
- No Krita code is used. See `research/krita-input-and-canvas.md` for the provenance note.

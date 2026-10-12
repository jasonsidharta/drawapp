# Instructions for coding agents

Read PRODUCT.md, ARCHITECTURE.md, ROADMAP.md and BUILD.md before changing code. Keep them true:
update them in the same change as the code, and label planned work separately from done work.

## Rules

- Independent application. Do not copy or translate code from Krita or other GPL projects.
  Research notes go in `research/` with revision, paths, verified facts and inferences kept
  separate. Any Krita checkout stays outside this repository and is read-only.
- Scope is set by ROADMAP.md. Don't add frameworks, plugin systems or abstractions for
  hypothetical needs.
- `src/core` depends on QtGui only (no Qt Quick) and is testable headless. Keep document pixels
  independent of the view transform.
- Never report a platform as verified without running it there. Never claim speed or memory
  improvements without numbers in BENCHMARKS.md.
- Touch must never be treated as pressure input. Pressure is used only when the pen device
  reports it (`effectivePressure`).

## Build and test (Windows)

```powershell
python tools\fetch_qt_windows.py          # once
powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1
```

All tests must pass before handing off: `tst_core` and `tst_document` (headless) and `tst_canvas`
(opens real windows and saves screenshots to `build\windows-mingw-release\tests\tst_canvas_*.png`;
look at them after rendering changes). Android: `tools\build-android.ps1 [-Config Debug]` (build
directory must not contain spaces).

## Version control

Local Git repository on branch `main`, no remote. Commit only when the project owner asks; never
push. Never commit build outputs, APKs/AABs, keystores or other credentials (see `.gitignore`).

## Undo and file invariants

- Every artwork change goes through `CanvasItem::commitEdit` with a `TileSnapshot` captured
  *before* the change. View state (zoom, pan) never enters history.
- A stroke must become exactly one undo step however it ends (`endStroke`).
- Changing the meaning of `.drawapp` bytes requires a new format version and keeping the reader
  for older versions. Validate before allocating; never replace the open document on failure.
- File writes go through `ProjectFile::writeFileSafely`.

## Conventions

C++17, Qt types, `m_` members, one class per file pair (namespaces like `ProjectFile` excepted).
Comments only for non-obvious reasons. Brush/stroke parameters are captured at stroke start.
User-facing errors are returned as strings from `CanvasItem` file methods and shown by QML.

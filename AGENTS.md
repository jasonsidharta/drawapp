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

All tests must pass before handing off. `tst_canvas` opens real windows and saves screenshots
(`build\windows-mingw-release\tests\tst_canvas_*.png`); look at them after rendering changes.
Android: `tools\build-android.ps1` (build directory must not contain spaces).

## Conventions

C++17, Qt types, `m_` members, one class per file pair. Comments only for non-obvious reasons.
Brush/stroke parameters are captured at stroke start.

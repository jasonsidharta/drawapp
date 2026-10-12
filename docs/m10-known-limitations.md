# Milestone 10: known limitations

What milestone 10 does not do, does only partly, or did not verify. Measured numbers are in
`docs/m10-performance.md`; test status in `docs/m10-test-results.md`.

## Not implemented

- **Reduced-resolution transform preview** (Krita's level of detail). The interactive preview
  renders at full resolution over the changed area; for many large layers a preview frame takes
  hundreds of milliseconds (`docs/m10-performance.md`). The merged-copy preview for adjacent layers
  without a selection (milestone 9) is the fast path. The target of 30 frames per second at
  4096² × 7 is **not met**.
- **Swapping tiles to disk** and **compressing tiles in memory**. Memory beyond the budget is
  refused, not paged.
- **A sparse selection mask.** Selections stay one byte per canvas pixel (256 MiB at 16384²), and
  each selection undo step keeps a copy of the mask.
- **Background compositing.** Composite updates run on the UI thread (fast for brush events, but
  a full rebuild of a large document, for example after toggling a layer's visibility, takes tens
  to hundreds of milliseconds).
- **Implicit applies in the background.** Switching tools with a large transform open applies it
  on the UI thread.
- **Saving format 2.** Milestone 9 and earlier cannot open files written by milestone 10.

## Partly done

- **Memory accounting** covers DrawApp's pixel data, not the process (Qt, QML, fonts, window
  buffers, encoder buffers). The limit is a budget for pixels, not a hard cap on process memory.
- **"Cannot be undone" questions** are asked for Apply, Clear and Delete layer, where the cost is
  known in advance. Strokes still report afterwards.
- **Format 3 file size**: smaller than format 2 for sparse drawings, larger for fully painted
  canvases (per-tile compression; measured in `docs/m10-file-compatibility.md`).
- **Opening format 2 files** still decodes each layer densely, one at a time, before tiling it.

## Not verified

- **Sanitizers.** AddressSanitizer and ThreadSanitizer could not be run: the MinGW toolchain ships
  no `libasan`/`libtsan`, and neither clang nor MSVC is installed (checked by building a test
  program). UndefinedBehaviorSanitizer in trap mode was run (`docs/m10-test-results.md`).
- **Physical devices.** No phone, tablet or pen hardware was available. Android was checked on
  the emulator only (`docs/m10-test-results.md`); the emulator says nothing about pressure, tilt,
  latency or real device memory pressure.
- **Hands-on artist use.** The fifteen workflows (`docs/m10-artist-workflows.md`) use synthetic
  input.
- **Comparison with Krita and other applications** (`docs/m10-comparison-checklist.md`): not run.
- **An on-screen session** on Windows with a real display (all window tests run offscreen).
- **A mouse click on the transform bar's Apply** did not apply a large transform in the
  milestone 10 workflow test, while Enter did; see `docs/m10-artist-workflows.md`.

## Behaviour that changed

- Layer limit: 64 at any size (was 512 MiB of dense layers: 42 at 2048 × 1536).
- Undo budget: a quarter of the memory limit (was 128 MiB); old steps can be dropped under memory
  pressure, with a notice.
- Large Applies run in the background; edits wait with a message meanwhile.
- File > New canvas… creates canvases up to 16384 × 16384.

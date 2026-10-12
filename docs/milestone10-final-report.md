# Milestone 10: final engineering report

Sparse tiled storage, copy-on-write, a memory budget and asynchronous transforms.
2026-10-12. Windows 11 25H2 (Intel Core Ultra 7 155H, 15.4 GB RAM), MinGW 15.1, Qt 6.12.0;
Android emulator (Pixel Tablet AVD, Android 16, 4 GB RAM). Local commits only; nothing pushed.

Evidence levels used throughout:

- **Implemented**: the code exists and is used by the application, not only by tests.
- **Tested**: automated tests check it (`ctest`, 10 suites).
- **Benchmarked**: measured with `bench_m9`/`bench_tiles`; raw output in `docs/benchmarks/`.
- **Driven on the emulator**: exercised in the Android app with `adb` taps and swipes, with screenshots
  inspected. This is automated input; it is not hands-on use, and it says nothing about pen
  input or device performance.

Nothing in this milestone was verified hands-on by an artist, on a physical Android device, or in
an on-screen Windows session.

---

## A. Executive summary

DrawApp's layers, composite, undo records, transforms, clipboard, recovery snapshots and project
files now use sparse 64 × 64 copy-on-write tiles instead of dense images. A process-wide pixel-memory
budget replaces milestone 9's fixed 512 MiB limit on dense layers. Large transform Applies run on a
worker thread with progress, Stop, generations and an atomic commit.

What this changed, measured:

- **Large canvases.** A 16384² canvas with 20 layers and sketch strokes is usable, saved and
  reopened in the automated workflows. Milestone 9 could not hold one layer of that size. A sparse
  16384² × 4 document holds 54.6 MiB of layer tiles where dense layers would take 4096 MiB.
- **Brushes.** Pixels are identical to the dense engine for every built-in brush, tested across
  tile seams. Speed per event is 0.60–1.09× of milestone 9, which is noise-level.
- **Undo.** Records keep tile references instead of copies. Stroke undo/redo dropped from
  0.17–2.4 ms to ≤ 0.02 ms, and a stroke over empty canvas costs no undo memory.
- **Files.** Save takes 0.29–0.40× of milestone 9's time and opening large files 0.65–0.75×.
  Files written by milestones 1 and 9 open identically.
- **Transforms.** Apply's 2.3–3.2 s of rendering on 7 × 4096² moved off the UI thread. The commit
  still takes 90–120 ms on the UI thread, narrowly missing the 100 ms target.
- **Start-up.** Idle memory is 20 MiB lower.

Regressions that remain:

- the per-layer transform preview is 1.26–1.31× slower;
- a full composite rebuild of small documents is 1.16–1.25× slower (≈ 2 ms);
- format 3 files are 20–25 % larger for fully painted canvases.

Not achieved:

- a 30 fps transform preview at 7 × 4096² (reduced-resolution previews were not built);
- AddressSanitizer and ThreadSanitizer runs (not available in this toolchain);
- the side-by-side comparison with Krita and other applications (not installed);
- any physical-device or hands-on check.

**Status against the 15 completion criteria (section 19 of the specification): all 15 are met**
(table below). Several specification targets beyond those criteria were not met. They are listed
in N and are not hidden by the criteria result. Milestone 10 is therefore reported as **complete
against its completion criteria, with stated gaps**.

| # | Criterion | Met? | Evidence |
|---|---|---|---|
| 1 | Tiled storage in real workflows | Yes | every layer is a `TiledImage`; workflows 1–15 |
| 2 | Sparse documents save memory | Yes | 54.6 vs 4096 MiB (H2); workflow 2 |
| 3 | Brush output compatible | Yes | `tiledLayersPaintExactlyLikeDenseImages`, golden images unchanged, 16 seam goldens |
| 4 | Multi-layer editing correct | Yes | `tst_canvas` multi-layer tests; workflows 5, 11, 14 |
| 5 | Large transforms don't block for their full time | Yes | worker Apply; UI-thread commit 90–120 ms of a 3 s job (H4 narrowly missed its 100 ms target) |
| 6 | Cancellation safe | Yes | cancel at four stages, 150-round stress (5 runs), destruction test; Stop on the emulator |
| 7 | Undo/redo reliable | Yes | `tst_document`, `tst_canvas`, workflow 7 |
| 8 | Existing projects open | Yes | milestone 1 and 9 fixtures identical (`tst_fileformat`) |
| 9 | Save/reopen correct | Yes | format 3 round trips; 16384² × 20 in workflow 9 |
| 10 | Windows automated tests pass | Yes | `ctest` 10/10, 424 results |
| 11 | Android builds, available runtime checks pass | Yes | x86_64 and arm64 APKs built; emulator checks pass after four fixes (arm64 not run: no device) |
| 12 | Benchmarks documented | Yes | `docs/m10-performance.md`, `docs/benchmarks/` |
| 13 | No known critical data-loss or painting defect | Yes | none open; the four emulator defects are fixed and covered by tests |
| 14 | Research with verifiable sources | Yes | Krita at 25840ee8 / e579e3a4 and PhotoCraft at 20cb95bc / 8b216216, with file and line references |
| 15 | Limitations reported | Yes | `docs/m10-known-limitations.md`, section N |

## B. Previous baseline verification

The milestone 9 baseline was reproduced before any change (commit 8b2ecea, files
`docs/benchmarks/m10-baseline-*`):

- `ctest` 8/8 suites, 337 QtTest results;
- first frame 155–201 ms; idle working set 89.1–90.4 MiB;
- 9-layer 2048 × 1536 merged preview 24.8 ms;
- 7-layer 4096² preview 242 ms and Apply 2.8 s;
- 4096² × 20 refused.

**The milestone 9 claim "opening a 0.1 MiB project: 1055 MiB before, 29 MiB after"** was
re-measured under equivalent conditions (`docs/benchmarks/m10-open-memory.md`). The same opening
program was built against three libraries (eb47f39 before the fix, 7469a69 final milestone 9, and
milestone 10), with five fresh processes each. Peak private memory:

| File | before | milestone 9 | milestone 10 |
|---|---|---|---|
| 512² × 3 | 1033.6 MiB | 7.7 MiB | 7.6 MiB |
| 2048 × 1536, mostly empty | 1076.5 MiB | 50.6 MiB | 26.5 MiB |

The fix was real. "29 MiB" was the peak of the whole benchmark process, not of opening alone. The
claim overstated its precision, not its direction.

**Baseline for A/B comparison.** Laptop run-to-run variance was large: one milestone 9 render took
317 ms in one run and 547 ms in the next. So milestone 9 (built from a separate worktree at 7469a69)
and milestone 10 were run alternately, section by section, three rounds, in one session
(`docs/benchmarks/m10-ab-raw.md`, `m10-ab-comparison.md`).

## C. Architecture changes

| Area | Milestone 9 | Milestone 10 |
|---|---|---|
| Layer pixels | dense `QImage` | `TiledImage`: grid of nullable 64 × 64 tiles, implicitly shared table, per-tile atomic owner count, copy on first write |
| Brush access | `QImage` scan lines | `StrokeSurface` (dense or tiled), row pointers valid to the end of a 64-pixel segment; the dense form remains for scratchpad, thumbnails and the brush sheet |
| Composite | dense, rebuilt per dirty rect | tiled; per-tile recomposite of dirty tiles, with a per-tile scratch buffer and a small-rect path; an uncovered tile is released |
| View | drawn from the dense composite | `ViewPyramid` (lazy 2× reductions, invalidated by tile stamps) and a persistent view buffer; exact above 50 % zoom |
| Undo pixel record | tile copies | tile references (`TileSnapshot`), budget = limit/4 (32 MiB–2 GiB), evictable under pressure |
| Memory limit | 512 MiB of dense layers | `MemoryBudget`: categorised pixel budget (½ RAM desktop, ¼ Android), evictors, device-memory floor |
| Transform Apply | UI thread | `TransformApply` worker with generation, cancel, progress, atomic commit |
| Project file | format 2, dense layers | format 3: only painted tiles, compressed in parallel; formats 1 and 2 still read |
| New canvas | fixed 2048 × 1536 | File → New canvas…, up to 16384² |

Thread rules (`research/m10-concurrency-design.md`):

- only the UI thread writes live tiles;
- workers get copies and read tiles that, being shared, nobody writes;
- evictors run only on the thread that registered them.

**Storage abstraction (specification stage 2).** It was implemented as `StrokeSurface`, the brush
engine's interface, with dense and tiled implementations. Layers themselves were moved to
`TiledImage` outright; there is no switchable dense layer backend. The dense path survives where
dense images are the right tool (scratchpad, thumbnails), and the tests that compare tiled with
dense output use it.

## D. Krita, PhotoCraft and other research

All research was read from source and is cited with revisions, files and lines. No code was
copied; Krita is GPL and was read for concepts only. Details are in `research/m10-*.md`.

| Source | Taken | Not taken |
|---|---|---|
| Krita (25840ee8, e579e3a4) `research/m10-krita-tile-engine.md` | 64-pixel tiles; transparent default tile; per-tile copy-on-write; undo by tile; budget as a share of RAM; "evict reconstructible data before refusing"; per-tile projection; display pyramid; Apply on workers from immutable inputs | swapping to disk; tile compression in memory; hash-table lookup (a dense grid suffices up to 16384²); sparse selection masks; per-pixel-size tiles; LoD transform previews (planned, **not built**) |
| PhotoCraft (20cb95bc, 8b216216) `research/m10-photocraft-memory.md` | copy-on-write tiles behind shared pointers; fallible allocation reported to callers; counting shared memory once; cancellation checks in long loops | GPU residency; its empty-selection behaviour (empty collapses to none; DrawApp keeps them distinct) |
| GEGL, libmypaint, Skia, OpenRaster `research/m10-gegl-skia-comparison.md` | libmypaint-style tile-segment row access for brushes; assembling the view before scaling (avoids Skia-documented seams) | new dependencies (none added) |

The 11 performance hypotheses were written before measuring, each with a result that would falsify
it (`research/m10-performance-hypotheses.md`). Their outcomes are in L. Two "adopted" items in the
research (LoD previews, per-pixel-size tiles) were not built, and the research documents now say so.

## E. Tiled storage implementation

`src/core/TiledImage.*`, `TileSnapshot.*`, `StrokeSurface.h`, `DocumentData.cpp`;
`docs/m10-tile-storage.md`.

- **Tile size by measurement** (`docs/benchmarks/m10-bench_tiles.md`). 64 vs 128 vs 256: times
  within about 10 % of each other; memory for 40 strokes on 16384² was 334 / 468 / 592 MiB. 64 was
  chosen (H1 confirmed).
- **Allocation.**
  - A missing tile is transparent; tiles are allocated on first non-transparent write and charged
    to `MemoryBudget`.
  - Tiles erased to transparent are released after a stroke, a clear or a transform.
- **Writes.** `write()` allocates fresh blocks for whole tiles that are shared or missing, holding
  them in a pending list until all are allocated (atomic). Tiles the image owns alone are written
  in place.
- **Stamps.** Each tile has a stamp renewed when it is handed out for writing; the view pyramid
  compares stamps, not pixels.
- **Failures.** Every allocation can fail, and each operation then fails whole:
  - a stroke is taken back;
  - Clear, Paste and Apply change nothing;
  - opening is refused;
  - the composite may be stale, with a notice.
- **Limits.** 16384 px per side and 64 layers. The selection mask is still dense (256 MiB at
  16384²). No in-memory compression, no swap.

Implemented, tested (`tst_tiles` 30 results: storage, copy-on-write, concurrency, allocation
failure, budget, eviction, pyramid, renderer and compositing equivalence), benchmarked.

## F. Copy-on-write and memory architecture

`docs/m10-memory-budget.md`, `docs/m10-undo-architecture.md`.

- **Categories:** Document, Composite, Cache, Preview, Temporary. The Memory usage dialog reports
  each, plus the undo history and the peak.
- **Limit:** ½ of physical RAM on desktop (7870 MiB on the development machine), ¼ on Android
  (979.9 MiB on the 4 GB emulator).
- **Eviction order:** view caches (order 0), then the oldest undo steps (order 10), only on the
  registering thread.
- **Refusals:**
  - a request larger than the whole limit is refused without evicting;
  - undo steps are given up only if that can cover the shortfall;
  - the artist gets a notice whenever steps were given up.
- **Device-memory guard.** Added after an 8192² × 10 save paged for 63 minutes with 0.9 GB free.
  The larger of 256 MiB and RAM/32 must stay available, checked every 64 MiB of growth (Windows
  `GlobalMemoryStatusEx`, Linux/Android `MemAvailable`).
- **Never reclaiming referenced tiles.** A tile is freed only when its owner count reaches zero.
  Snapshots, undo records, transform inputs and the compositor all hold references.
  `tst_tiles` and the Apply stress test check that nothing is left allocated and nothing shared is
  written.
- **Non-undoable actions.** Apply, Clear and Delete layer ask first when their undo record would
  not fit; strokes report afterwards.
- **What is not counted:** process overhead, Qt's own buffers, the selection mask.

Implemented and tested (`tst_tiles`, `tst_document`, `tst_canvas`, `tst_ui` dialogs); driven on the
emulator (dialog, refusals, eviction behaviour).

## G. Asynchronous transformation engine

`src/core/TransformApply.*`, `TransformSession.*`; `docs/m10-async-transforms.md`.

- **When it runs.** Work of at least 4 Mpx (changed pixels × layers) applies on a worker.
- **Worker inputs.** The session's `workerCopy()` and the layers, all shared copies.
- **Rendering.** Bands of sixteen tile rows, with the cancel flag checked between bands and
  progress counted in rows.
- **Delivery.** The result is delivered by a queued call only if its generation is current. It is
  committed on the UI thread only if every layer's version is unchanged, by exchanging tile
  references plus one group undo step.
- **While it runs.** Edits are refused with a message; pan and zoom work. Stop or Esc cancels and
  keeps the preview open.
- **Tests:**
  - equivalence with immediate Apply;
  - blocked edits;
  - cancel at four stages;
  - worker failure;
  - implicit apply;
  - destruction mid-job;
  - stale results;
  - a 150-round stress test, which passed five runs in a row.
- **Emulator.** Five 4096² layers showed "Applying… N %" with Stop. Stop left the drawing and the
  history unchanged. Pressing Home during Apply and returning showed the committed result.
- **Found and fixed on the emulator.** Without a selection the session kept a transparent "base"
  copy per layer, so a five-layer 4096² preview held 867.5 of 979.9 MiB and Apply was refused.
  The fix brought it to 499.7 MiB and Apply succeeded. In `bench_m9`, session memory went from 238
  to 136 MiB (9 × 2048 × 1536) and from 1021 to 583 MiB (7 × 4096²). Regression test:
  `transformWithoutSelectionKeepsOneCopyPerLayer`.
- **Not done:**
  - a reduced-resolution interactive preview;
  - implicit applies (switching tools) still run on the UI thread;
  - the UI-thread commit at 7 × 4096² is 90–120 ms, against a 100 ms target.

## H. Brush compatibility

- `tst_brushengine::tiledLayersPaintExactlyLikeDenseImages`: every built-in brush paints
  **identical pixels** on tiled and dense images. This was checked across tile seams, on
  transparency and with a soft selection, and undo is exact.
- 16 new golden images of strokes along tile seams (`tests/golden/stroke-m10-seam-*.png`). The
  existing golden images pass unchanged.
- The brush test sheet was rendered and looked at after the engine changes.
- Speed: 0.60–1.09× of milestone 9 per event across 18 engine cases. Long strokes of 10 000
  events: 1.00–1.09× (H9 confirmed).
- No dab cache was built. Medians and p95 stay under 1 ms. Single 2.1–4.5 ms events occur in both
  milestones in the same session, which points to scheduling noise (H8 partly met).
- The 120-property registry was not expanded (94 implemented, 26 planned, as in milestone 9).

## I. File compatibility and recovery

`docs/m10-file-compatibility.md`; `tst_fileformat` (12), `tst_document`, `tst_recovery`.

- **Format 3:**
  - stores only painted tiles, each zlib-compressed;
  - encoded and decoded in parallel;
  - keeps format 2's header, SHA-256 and JSON metadata.
- **Older formats.** Formats 1 and 2 are still read; format 2 is no longer written. Milestone 9
  refuses format 3 files with a clear message and changes nothing.
- **Fixtures written by older builds** (milestone 1 manual file, milestone 9 Android file,
  milestone 9 layered, sparse and recovery files): layers, properties and composite are identical
  to what milestone 9 decoded. Each also round-trips through format 3.
- **Malformed input.** 9000 deterministic mutations across three file types. Every one was either
  a valid document or refused with a message, with no tile left allocated. 19 hand-made damaged
  format 3 files are each refused by name, and declared sizes are checked before allocating. This
  is not coverage-guided fuzzing.
- **Saving.** `QSaveFile` atomic replace; a failed save leaves the old file byte for byte. No
  existing project is deleted before its replacement is complete.
- **Recovery** keeps container format 2 with an embedded format 3 project. Recovery files from
  milestones 2–9 are still read. On the emulator, recovery after reinstall restored a 16384² and a
  4096² five-layer drawing.
- **Size.** Format 3 is smaller for sparse drawings (e.g. 14 114 → 4 894 bytes) and larger for
  fully painted canvases (e.g. 9 667 → 14 155 bytes; 20–25 % on the full benchmark workloads),
  because each tile is compressed on its own.
- **Not tested:** big-endian machines, physical devices, files from milestones 2–8 other than via
  the kept format 2 encoder.

## J. Automated test results

`docs/m10-test-results.md`. `ctest`: **10 of 10 suites pass**. QtTest results including
init/cleanup and data rows:

| Suite | M9 | M10 |
|---|---|---|
| tst_core | 34 | 34 |
| tst_brush | 37 | 37 |
| tst_brushengine | 28 | 30 |
| tst_tiles (new) | — | 30 |
| tst_fileformat (new) | — | 12 |
| tst_selection | 34 | 34 |
| tst_document | 63 | 86 |
| tst_recovery | 28 | 28 |
| tst_canvas | 100 | 118 |
| tst_ui | 13 | 15 |
| **Total** | **337** | **424** |

**Sanitizers:**

- **UndefinedBehaviorSanitizer: run.** A whole-project trap-mode build (5682 trap instructions in
  the core library) passed 10/10 suites.
- **AddressSanitizer and ThreadSanitizer: not run.** MinGW 15.1 ships no `libasan`/`libtsan`, and
  clang and MSVC are not installed. Thread safety rests on the design rules, the deterministic
  concurrency tests and the stress test, not on a race detector.

**Not covered:** pen hardware, latency to the screen, the OS killing the app during a save, and
sessions lasting hours.

## K. Windows and Android results

**Windows 11** (`BUILD.md`):

- builds; `ctest` 10/10;
- window tests run on the offscreen platform;
- start-up first frame 152–172 ms, idle private memory 55.5–56.0 MiB.

The on-screen Windows session with real input is still outstanding: the desktop was in use, as in
earlier milestones.

**Android** (`BUILD.md`, "Milestone 10 emulator results"):

- x86_64 and arm64 debug APKs were built. The x86_64 APK was driven on the emulator with `adb`
  input; the arm64 APK was not run (no device).
- Checked:
  - Memory usage dialog (limit 979.9 MiB, undo 245 MiB);
  - a 16384² canvas with strokes;
  - recovery after reinstall;
  - an oversized transform refused cleanly;
  - single- and five-layer 4096² Apply with progress and Stop;
  - Back and Home during a transform and during Apply.
- **Four defects found and fixed during these checks:**
  1. a hopeless request evicted the whole undo history;
  2. the guard read MemFree instead of MemAvailable;
  3. a black strip appeared after the toolbar re-wrapped;
  4. the transform's transparent base copy exhausted the budget.

  1 and 4 have regression tests; 2 and 3 were re-checked on the emulator.
- Not checked: pen pressure and tilt, latency and frame rate, saving a format 3 file through the
  Android picker, low-memory kills during a save.

## L. Performance before and after

All CPU processing times on one laptop; not screen latency, not a phone. Full tables are in
`docs/m10-performance.md`; ratio = M10 / M9, so below 1 is faster.

| Workload | Operation | M9 | M10 | Ratio |
|---|---|---|---|---|
| 2048 × 1536 × 10 | stroke event, Solid Round / Textured | 0.020 / 0.163 ms | 0.019 / 0.171 ms | 0.99 / 1.05 |
| 2048 × 1536 × 10 | undo of a stroke | 0.17 ms | < 0.01 ms | ≈ 0 |
| 2048 × 1536 × 10 | full composite rebuild | 12.85 ms | 14.95 ms | 1.16 |
| 2048 × 1536 × 10 | save / open | 824 / 248 ms | 239 / 186 ms | 0.29 / 0.75 |
| 4096² × 8 | save / open | 2751 / 970 ms | 794 / 630 ms | 0.29 / 0.65 |
| 4096² × 8 | full composite rebuild | 62.7 ms | 53.4 ms | 0.85 |
| 512² × 3 | full composite rebuild | 0.32 ms | 0.40 ms | 1.25 |
| 9 × 2048 × 1536 transform | preview frame, per layer | 192 ms | 253 ms | 1.31 |
| 7 × 4096² transform | preview frame, merged | 154 ms | 155 ms | 1.01 |
| 7 × 4096² transform | Apply total (prepare + commit) | 3344 ms on UI thread | 3165 ms, of which 116 ms on UI thread | 0.95 |
| 8192² × 2 | peak private memory | 1833 MiB | 1226 MiB | 0.67 |
| start-up | idle private memory | 75.4–76.0 MiB | 55.5–56.0 MiB | −20 MiB |

**Workloads milestone 9 refused:**

- 4096² × 20: save/open 2.4 / 1.7 s;
- 8192² × 10: save/open 5.7 / 2.5 s, re-run with 4.9 GB free;
- sparse 16384² × 4: 54.6 MiB of tiles, save/open 0.37 / 0.10 s;
- zoomed-out view, later frames 1.4–2.4 ms at every size; the first frame at a new pyramid level
  is up to 171 ms at 8192².

**After the A/B runs:** the transform base-copy fix (G). Measured in one run, not interleaved
(`docs/benchmarks/m10-transform-nobase.md`):

- session memory 238 → 136 and 1021 → 583 MiB;
- begin 118 → 70 ms and 476 → 359 ms. The begin regression (1.10–1.14×) is probably gone, but
  that is not claimed as measured against milestone 9.

**Hypotheses:**

| Hypotheses | Outcome |
|---|---|
| H1, H2, H3, H6 (later frames), H9, H10, H11 | Confirmed |
| H5 | Mostly met (1.25× at 512² × 3) |
| H4 | Narrowly not met: UI-thread commit 90–120 ms against < 100 ms |
| H7 | Not met; reduced-resolution preview not built |
| H8 | Medians and p95 yes, isolated events no; no cache built |

## M. Artist workflow results

`docs/m10-artist-workflows.md`. Fifteen tasks were run through the real interface by
`tst_ui::artistWorkflowsMilestone10` with **synthetic input** (QtTest mouse events and generated
strokes with simulated pressure; offscreen rendering). **All fifteen pass.** They cover:

- a new 16384² canvas;
- long sketch strokes;
- the fit view through the pyramid;
- work at 100 %;
- 20 layers;
- erasing that frees tiles;
- 10 undo/redo steps;
- the memory dialog;
- save and reopen of 16384² × 20;
- opening a milestone 9 project;
- a six-layer 4096² background Apply with a refused stroke, Stop and re-Apply;
- magic wand;
- copy/paste of all layers;
- PNG export.

The screenshots were looked at. This is not hands-on artist validation and says nothing about pen
feel, latency or comfort. The emulator checks (K) are likewise automated touch input.

Found while writing them:

- canvases could only be 2048 × 1536, so File → New canvas… was added;
- "0 MiB" appeared in notices for small budgets, now shown in KiB;
- a test click timing issue, not an app defect.

## N. Known limitations

`docs/m10-known-limitations.md` has the full list. The ones that matter most:

- **Not built:**
  - reduced-resolution transform preview (no 30 fps at 7 × 4096²);
  - swap to disk and in-memory tile compression (memory beyond the budget is refused);
  - sparse selection masks (256 MiB at 16384²);
  - background compositing;
  - implicit applies in the background;
  - saving format 2.
- **Partly done:**
  - the budget counts pixel data, not the whole process;
  - "cannot be undone" is asked only where the cost is known in advance;
  - format 3 is larger for fully painted canvases;
  - format 2 files are still decoded densely, one layer at a time;
  - the transform session keeps a dense source per layer, about 500 MiB for five full 4096²
    layers, half of a 4 GB device's limit;
  - the UI-thread commit is 90–120 ms at 7 × 4096².
- **Remaining regressions:**
  - per-layer transform preview 1.26–1.31×;
  - small-document composite rebuild 1.16–1.25×;
  - opening very small files 1.16× (3 ms).
- **Not verified:**
  - ASan and TSan;
  - physical Android devices and pen hardware;
  - an on-screen Windows session;
  - hands-on artist use;
  - the comparison with Krita, PhotoCraft, GIMP and MyPaint (not installed; procedure in
    `docs/m10-comparison-checklist.md`; no claim that DrawApp is faster, lighter or more capable);
  - big-endian machines.

## O. Commit history and build artifacts

Local branch `main`; no remote; nothing pushed. Milestone 10 commits after 7469a69 (milestone 9):

| Commit | Stage |
|---|---|
| 8b2ecea | research and reproduced baseline (stage 1) |
| 57863ae | sparse copy-on-write `TiledImage` and the memory budget (stages 2–4) |
| efcc900 | layers on tiled storage, project format 3 (stage 5) |
| 90590db | view pyramid (stage 6) |
| b34a1e6 | transform Apply on a worker thread (stage 7) |
| 8ddc4f2 | memory-aware undo, memory report, refusals (stage 8) |
| 85dc492 | file compatibility, recovery and fuzzing (stage 9) |
| 99877c0, 1cae597, a7c88eb | measured regressions fixed, device-memory guard, A/B benchmarks (stage 10) |
| 4d2bc61 | workflow test click timing (stage 12) |
| a7e2941, 16d0fb6, 697ab56 | fixes found on the Android emulator, emulator results (stage 11) |
| (this commit) | final report and documentation (stage 13) |

Artifacts:

- **Windows:** `build\windows-mingw-release\drawapp.exe`; standalone folder via
  `cmake --install` (`BUILD.md`).
- **Android:** `C:\Users\Asus\Downloads\DrawApp-m10\`:
  - `DrawApp-m10-arm64-debug.apk`, 74.4 MB, for devices, not run;
  - `DrawApp-m10-x86_64-emulator-debug.apk`, 77.9 MB, the build driven on the emulator.

  Both are debug builds signed with a development key, built from the source committed as
  697ab56 (later commits change documentation only).

## P. Next milestone recommendation

In order of reliability first, then performance:

1. **Hardware validation (milestone 7's outstanding list plus milestone 10).** Run the arm64 APK
   on a real tablet with a pen:
   - large canvases under real memory pressure, including Android low-memory kills during a save;
   - Apply with Home/Back;
   - saving format 3 through the picker;
   - stroke latency.

   Run the on-screen Windows checklist.
2. **Race detection.** Install clang or MSVC (with the owner's agreement), run ASan and TSan on the
   tile, Apply and file code, and fix what they find.
3. **Reduced-resolution transform preview.** Render drags at the view's pyramid level and keep
   full resolution for release and Apply (H7). Pair it with a tiled or reduced transform source,
   which also removes the remaining ~500 MiB dense session cost at 5 × 4096².
4. **Commit under 100 ms.** Move the composite refresh after Apply off the UI thread, or split it
   (H4); run implicit applies in the background.
5. **Sparse selection masks**, so a 16384² selection does not cost 256 MiB.
6. **The external comparison** (`docs/m10-comparison-checklist.md`) once Krita and the others can be
   installed, so any claim against them rests on measured numbers.

Feature work (brush properties, blend modes, groups) should wait until 1–3 are done.

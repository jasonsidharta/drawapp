# Milestone 11 final report: advanced brush technologies

Branch `m11-brushes`, from milestone 10 (`dcde383`). Thirteen local commits, none pushed:
stages 1–11 (stage 2 in two parts) and stage 12, which adds this report. Codex's milestone 10.5
branch was not merged. Dates: 2026-10-12 to 2026-10-13. Machine: Intel Core Ultra 7 155H,
15.4 GB RAM, Windows 11 25H2.

## Status

**All 16 completion criteria are met as written** (table below). Two caveats limit what that
means:
- **Nothing was physically verified.** No pen, no physical tablet and no artist were involved,
  so whether these brushes serve serious illustrators is unproven.
- **Watercolour is a bounded approximation**, verified against its own wet-media behaviours, not
  a fluid simulation.

The limitations are listed in `docs/m11-known-limitations.md`.

## Evidence by feature

Columns:
- **Implemented:** in the code and reachable from the UI.
- **Tested:** automated tests on rendered pixels.
- **Benchmarked:** `bench_m11`, see `docs/m11-performance.md`.
- **Emulator:** exercised by touch on the Android x86_64 emulator.
- **Physical:** on a physical device or with a real pen.

| Feature | Implemented | Tested | Benchmarked | Emulator | Physical |
|---|---|---|---|---|---|
| Deposit-strategy pipeline; milestone 10 output identical | yes | yes: 330 exact renders, legacy goldens unchanged | yes: A/B vs milestone 10 | indirectly | no |
| Dry media: 7 presets | yes | yes | yes | no | no |
| Ink: curves, corners, live end taper, adaptive smoothing; 4 presets | yes | yes | yes | no | no |
| Mixing: mix mode, smudge length and radius, colour-rate dynamics, pigment mixing; 3 presets | yes | yes | yes | no | no |
| Bristles: 2D contact model; 3 presets | yes | yes | yes | yes (Round Bristle) | no |
| Watercolour: wet layer and canvas wet session; 2 presets | yes (approximation) | yes: wet-on-wet, wet-on-dry, rims, granulation, drying, lifting, glazing, seams, undo, save | yes | yes (wash, drying, undo) | no |
| Procedural: strands, grass, foliage, splatter, pattern; 9 presets with cross-hatch and Gaussian spray | yes | yes | yes | yes (Grass, Hair, Foliage, Splatter) | no |
| Registry audit: every property on every claimed engine | yes | yes: 1035 pairs | — | — | — |
| Panel shows only settings that apply | yes | yes (tst_ui) | — | found there; fix not re-run on the emulator | no |
| Preset categories and search | yes | yes (tst_ui) | — | yes | no |
| Preset compatibility with milestone 10 builds | yes | yes: samples, plus the milestone 10 decoder | — | — | — |
| Android x86_64 / arm64 builds | built | — | — | x86_64 run | arm64 not run |

**Not implemented:**
- physical bristle deformation, paint thickness and impasto;
- a fluid watercolour simulation, and wet paint stored in files;
- a live end taper for bristles, smudge, wet paint and watercolour;
- a procedural shape cache;
- editable categories;
- Krita `.kpp` and Procreate `.brush` import;
- the 24 still-planned properties of the 120-property milestone 9 design target (96
  implemented).

## Completion criteria

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 1 | Existing brush outputs remain compatible | Met | `tst_brushengine::milestone10EngineReference`: 330 renders bit-identical through every stage; legacy goldens never rewritten; new keys written only when used |
| 2 | Dry media: useful texture and buildup | Met (measured, not artist-judged) | `tst_brushmedia::dry*`: penetration follows pressure on the paper's heights, build-up within the same tooth, tilt shades peaks, streaks follow the stroke |
| 3 | Ink smooth and controllable | Met | 16-sample circle: inner radius 148.5 (curve) vs 145.7 (straight); corners kept; no gaps at 6 or 1000 events; the live taper reaches the pen (x 265 vs 181 held back) |
| 4 | Smudge and mixing distinct and tested | Met | Smear keeps structure (variation 20.5), mix averages it (3.8); blue + yellow → (56,106,27) with pigment mixing vs (186,170,69) in RGB; alpha-aware; depletion |
| 5 | Bristles: contact and paint loading | Met | Loaded 3632/3592 px vs dry 1808/0 (first/last third); flat 40 px across vs 8 along; pickup carries colour (3974 px vs 0); light pressure 26 px wide vs 40 |
| 6 | Watercolour: verified wet behaviour, or reported as a prototype | Met, as a **bounded approximation** (not a fluid simulation) | Wet-on-wet bleed 24 px vs 12 px on dry or dried paper; hard wet-on-dry edges; rim/centre 1.13 vs 1.03; granulation 13.8 vs 0; drying time follows its setting; lifting 158 → 53; glazes 150 → 183; canvas session with undo and honest saving |
| 7 | Procedural: deterministic, useful results | Met | Seeded and reproducible; tiles = dense; direction (strands ahead 159/147 px, behind 0/0); grass up and shaded; colour variation; splatter power law and stretch; patterns join and follow the stroke |
| 8 | Presets serialize and reopen | Met | Round trips; committed samples; the milestone 10 decoder keeps old-engine presets and skips new ones individually |
| 9 | Every newly exposed setting affects real behaviour | Met | Registry audit, 1035 property-engine pairs, after removing over-claims and fixing two gaps; the panel hides settings that do not apply |
| 10 | No unexplained major performance or memory regressions | Met, with a disclosed possible small regression | Milestone 10 presets: geomean 0.93–1.15 per workload (±40 % noise per row); memory, undo and file sizes unchanged. New-engine costs measured and bounded |
| 11 | Sparse tiled storage intact | Met | Tiled = dense for every new engine; per-tile wet layer and coverage buffers, no dense full-canvas buffers in the brush path; sparse 8192² and 16384² memory unchanged |
| 12 | Undo, selections, layers, transforms keep working | Met | Existing suites, selection tests per engine, scripted workflow (`tst_canvas::artistWorkflowWithMilestone11Brushes`). A pre-existing milestone 10 issue was found (undo shedding at transform apply) and reported |
| 13 | All applicable Windows tests pass | Met | ctest 11/11 suites, 396 test functions, after the last change |
| 14 | Android builds, emulator testing | Met | x86_64 and arm64 debug APKs built; x86_64 exercised on the API 36 emulator (BUILD.md) |
| 15 | Licensing and source use documented | Met | `research/m11-licenses.md`: no external code, data, presets or assets copied |
| 16 | Incomplete or unverified features disclosed | Met | This report, `docs/m11-known-limitations.md`, BUILD.md, `docs/m11-performance.md` |

## Deliverables

| # | Deliverable | Where |
|---|---|---|
| 1 | Brush implementations | `src/core`: `*Deposit.*`, `WetLayer.*`, `StrokeCurve.*`, `PigmentMix.*`, `BrushRaster.h`, `StrokeContext.h`; `src/app/CanvasItemWet.cpp` |
| 2 | Artist-usable presets | 30 new built-ins (38 in total), in nine categories |
| 3 | Native preset serialization | Formats stay at version 2 with new keys (`research/m11-brush-architecture.md` A2) |
| 4 | Regression and visual tests | `tests/tst_brushmedia.cpp` (new, 52 functions); additions to `tst_brushengine`, `tst_canvas`, `tst_ui`, `tst_brush`; 112 new golden images and 3 reference files (no existing golden changed) |
| 5 | Benchmarks against milestone 10 | `tests/bench_m11.cpp`, `docs/m11-performance.md`, `docs/benchmarks/m11-*`, BENCHMARKS.md |
| 6 | Windows build | `build/windows-mingw-release` (MinGW 15.1, Qt 6.12.0) |
| 7 | Android x86_64 and arm64 builds | Debug APKs in `%USERPROFILE%\drawapp-build\m11-android-*-debug` |
| 8 | Research documentation | `research/m11-*.md` (7 files) |
| 9 | Licence audit | `research/m11-licenses.md` |
| 10 | Artist-workflow testing | `docs/m11-artist-workflows.md`: scripted and emulator workflows, not hands-on |
| 11 | Known limitations | `docs/m11-known-limitations.md` |
| 12 | Engineering report | This file; engine description in `docs/brush-engine-architecture.md` |

## Stages

| Stage | Commit | Result |
|---|---|---|
| 1 Research | `1756e8d` | Krita, PhotoCraft, Procreate handbook, Krita Artists top 30 (479 presets checked), simulation literature, licences, decisions A1–A10 |
| 2 Engine design | `b3ad4a4`, `87e3ecc` | Exact milestone 10 reference (330 hashes); deposit strategies; identical output |
| 3 Dry media | `4ba0923` | Paper tooth × penetration; 7 presets |
| 4 Ink | `6bf5b98` | Curves, corners, live taper, adaptive smoothing; 4 presets |
| 5 Mixing | `632631b` | Mix mode, pigment mixing; 3 presets |
| 6 Bristles | `5be7949` | 2D contact model; 3 presets; under 2 ms per event |
| 7 Watercolour | `b8b2eb8` | Wet layer and canvas session; the first model failed its own tests and was fixed in the model, not in the thresholds |
| 8 Procedural | `9b79b04` | 5 stamps, cross-hatching, Gaussian spray; 9 presets |
| 9 Integration | `51ca836` | Audit (39 non-effective claims fixed), categories, search, compatibility checked with the milestone 10 decoder |
| 10 Performance | `987b6dc` | A/B; watercolour and procedural fixed (stalls of up to 30 s → bounded) |
| 11 Android | `9eff262` | Builds; emulator run; panel fix |
| 12 Documentation | this commit | Reports, limitations, workflow test |

## Coordination with milestone 10.5 (Codex)

- **Not modified** (the protected systems):
  - `TiledImage`, `TileSnapshot`, `MemoryBudget`;
  - `TransformSession`, `TransformApply`;
  - `ViewPyramid`, `ViewRenderer`;
  - `ProjectFile`, `UndoHistory`, `LayeredDocument`.

  Milestone 11 uses only their existing public interfaces. No interface change was needed.
- **Files both branches change**, which will need care at merge:
  - `CanvasItem.cpp`/`.h` and `CanvasItemSelection.cpp`: one-line wet-session hooks; the logic
    is in the new `CanvasItemWet.cpp`;
  - `CMakeLists.txt` and `tests/CMakeLists.txt`: new sources and targets;
  - `tests/tst_canvas.cpp`: new tests, and one existing test adjusted for the open watercolour
    undo step;
  - `BUILD.md`: a milestone 11 emulator section;
  - ARCHITECTURE.md, PRODUCT.md and ROADMAP.md: short milestone 11 sections, placed away from
    Codex's edits.
- **Build output:** separate directories (`build/windows-mingw-release` in this worktree;
  `m11-android-*`). Benchmarks ran only with no Codex process active.
- **Reported to milestone 10.5:** applying a transform after a session with several layers and a
  selection makes the memory evictor drop undo steps, with the budget nearly empty. Reproduced
  on the milestone 10 baseline (10 of 14 steps). Details in `docs/m11-artist-workflows.md`.

## Reproduce

```
cmake --build build/windows-mingw-release
ctest --test-dir build/windows-mingw-release          # 11 suites
build/windows-mingw-release/brush_sheet.exe sheet.png # every preset
build/windows-mingw-release/bench_m11.exe doc2048 300 # and doc4096, sparse8192, sparse16384
powershell -File tools/build-android.ps1 -Config Debug -Abi x86_64 -BuildDir <dir without spaces>
```

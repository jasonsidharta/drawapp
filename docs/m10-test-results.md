# Milestone 10: test results

Windows 11 25H2, MinGW 15.1, Qt 6.12.0, release build, 2026-10-12. Window tests run on the
offscreen platform under ctest (they do not open windows on the desktop).

## Suites

`ctest`: **10 of 10 suites pass** (milestone 9: 8 suites). QtTest totals (including
init/cleanup and data rows):

| Suite | Milestone 9 baseline | Milestone 10 | New for milestone 10 |
|---|---|---|---|
| tst_core | 34 | 34 | |
| tst_brush | 37 | 37 | |
| tst_brushengine | 28 | 30 | every built-in brush paints identical pixels on tiled and dense images (seams, transparency, soft selection; undo exact); 16 tile-seam golden images |
| tst_tiles (new) | — | 30 | storage, copy-on-write, concurrency, allocation failure, budget, eviction, the device-memory floor, view pyramid, renderer equivalence above 50 %, compositing paths equal to dense compositing, exact content bounds |
| tst_fileformat (new) | — | 12 | files written by milestones 1 and 9 open identically, M9 recovery file, failed save keeps the old file, 9 000 fuzzed inputs, huge declared sizes |
| tst_selection | 34 | 34 | |
| tst_document | 63 | 86 | format 3 damaged-file cases (19), format 2 reading, sparse 16384² file, opening under memory pressure, undo memory on blank tiles |
| tst_recovery | 28 | 28 | (updated for format 3) |
| tst_canvas | 100 | 118 | background Apply (equivalence, blocked edits, cancel at four stages, worker failure, implicit apply, destruction, stale results, 150-round stress), "cannot be undone" confirmations, undo eviction, memory report, export refusal; two regressions found on the Android emulator (hopeless requests keep the undo history; a transform without a selection keeps one dense copy per layer) |
| tst_ui | 13 | 15 | memory dialogs; fifteen artist workflows (`docs/m10-artist-workflows.md`) |
| **Total** | 337 | **424** | |

The brush sheet was rendered and looked at after the engine changes; the window-test
screenshots were looked at (`docs/m10-artist-workflows.md`).

## The twelve test areas of the milestone

| Area | Where |
|---|---|
| Tile storage and copy-on-write | `tst_tiles` |
| Concurrency (readers while writing, isolated writers, worker jobs) | `tst_tiles`, `tst_canvas` Apply tests and stress test |
| Memory budget, eviction, refusal, device floor | `tst_tiles`, `tst_document::openingRespectsTheMemoryBudget`, `tst_canvas` memory tests |
| Brushes on tiles (zero regression) | `tst_brushengine::tiledLayersPaintExactlyLikeDenseImages`, `tileSeamGoldenImages`, `goldenImages` |
| Compositing | `tst_tiles::tiledCompositingMatchesDenseCompositing` |
| View and pyramid | `tst_tiles` pyramid and renderer tests |
| Undo on tiles | `tst_document` budget tests, `tst_canvas` undo tests |
| Transforms and background Apply | `tst_canvas`, `tst_selection` |
| File format 3 and compatibility | `tst_fileformat`, `tst_document` |
| Recovery | `tst_recovery`, `tst_fileformat::recoveryFileFromMilestone9Restores` |
| Malformed input | `tst_fileformat::malformedFilesNeverCrashOrLeak`, `tst_document::rejectsDamagedTiledFiles` |
| Workflows through the interface | `tst_ui::artistWorkflowsMilestone10` (synthetic input) |

## Repeated runs

- The background-Apply tests (`applyJobStress`, cancellation, blocked edits, destruction) passed
  five runs in a row.
- `ctest` passed on every run during the milestone after each fix.

## Sanitizers

| Sanitizer | Status |
|---|---|
| UndefinedBehaviorSanitizer | **Run.** Separate build of the whole project with `-fsanitize=undefined -fsanitize-trap=undefined -fno-sanitize-recover=all` (RelWithDebInfo); every compile line had the flags and the core library contained 5 682 trap instructions. All 10 suites passed (`ctest`, 10/10), i.e. no undefined behaviour was detected on the paths the tests execute. Trap mode reports by crashing, without a message, so a failure would have shown as a crashed test. |
| AddressSanitizer | **Not run.** The MinGW 15.1 toolchain has no `libasan` (linking `-fsanitize=address` fails: "cannot find -lasan"); clang and MSVC are not installed. |
| ThreadSanitizer | **Not run.** No `libtsan` in the toolchain; not available on Windows. |

Thread safety therefore rests on the design rules (`research/m10-concurrency-design.md`), the
deterministic concurrency tests and the stress test, not on a race detector.

## Android

See the "Milestone 10 emulator results" in `BUILD.md`.

## Not covered by tests

- Pen pressure and tilt on real hardware; latency to the screen.
- Behaviour when the operating system kills the app under memory pressure (Android) during a save.
- Very long sessions (hours) and memory fragmentation.

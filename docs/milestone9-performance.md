# Milestone 9 performance

All measurements were made on one machine. They are processing times, not screen latency:
nothing here measures input-to-photon latency or frame times on a display.

## Machine, build and method

| | |
|---|---|
| Machine | ASUS Zenbook Duo UX8406MA, Intel Core Ultra 7 155H (16 cores / 22 threads, 3.8 GHz max), 15.4 GiB RAM, Intel Arc graphics |
| OS | Windows 11 25H2; other desktop apps running; power state not recorded |
| Build | Release, MinGW-w64 GCC 15.1 (`-O2`), Qt 6.12.0, `build\windows-mingw-release` |
| Timer | `QElapsedTimer` around the measured call, on the calling thread |
| Memory | process private bytes (`PrivateUsage`, commit charge), peak private (`PeakPagefileUsage`) and working set, from `GetProcessMemoryInfo` |
| Programs | `bench_m9` (new: workloads, engines, long strokes, multi-layer transforms), `bench_core` (milestone 2–8 tables, kept for before/after comparisons), the app's `DRAWAPP_STARTUP_PROBE` |
| Raw output | `docs/benchmarks/m9-baseline-bench_core.md` (before milestone 9), `docs/benchmarks/m9-final-bench_core.md` and `docs/benchmarks/m9-bench_m9.md` (after) |

How to reproduce:

```powershell
$d = "build\windows-mingw-release"
foreach ($s in "doc512","doc2048","doc4096","doc8192","brushes","long","transforms") { & "$d\bench_m9.exe" 20 $s }
& "$d\bench_core.exe" 30            # or: bench_core 30 presets
$env:QT_QPA_PLATFORM="offscreen"; $env:DRAWAPP_STARTUP_PROBE="1"; & "$d\drawapp.exe"
```

Each `bench_m9` section runs in its own process, so its peak memory is its own. The brush input
traces are synthetic and identical in every run: a wavy 1000-event stroke 4 ms apart with
simulated pressure, and a 10 000-event spiral for the long strokes. The documents are generated
the same way each time: dabs whose density scales with the area.

Run-to-run variation on this machine is about ±10 % for medians and more for p95 and maximum
values. Background desktop activity causes isolated outliers: one 191-second "maximum" in a
first run did not reproduce in three reruns, where the maximum was 2.3–5.2 ms.

## Start-up and idle memory (app, offscreen, 5 runs)

| | Milestone 9 baseline | Milestone 9 final |
|---|---|---|
| First frame | 150–168 ms | 156–178 ms |
| Idle working set (after 2 s) | 87.4 MiB | 89.6–89.9 MiB |
| Idle private memory | ≈ 74 MiB | 75.3–75.7 MiB |

During the milestone, start-up had regressed to 192–206 ms and 96 MiB. The causes:
- the property registry generated every brush texture before the first frame;
- the Brush Studio page was created while hidden.

Both were fixed (commit "restore start-up time"). The remaining ≈ 2 MiB is the larger QML module
and code. The first frame is measured from `main()` to the first `frameSwapped`, offscreen: a
lower bound for a real window.

## Required workloads (`bench_m9`)

| Workload | Layer pixels | Document memory (private) | Full composite | View render, fit 1920 × 1080 (median / p95) | Stroke event, Solid Round 24 px (median / p95) | Stroke event, Textured Paint 40 px (median / p95) | Undo / redo of that stroke | Save | Open | Peak private |
|---|---|---|---|---|---|---|---|---|---|---|
| 512 × 512, 3 layers | 3 MiB | 4 MiB | 0.22 ms | 0.32 / 4.0 ms | 0.005 / 0.017 ms | 0.066 / 0.10 ms | 0.03 / 0.03 ms | 17 ms | 17 ms | 29 MiB |
| 2048 × 1536, 10 layers | 120 MiB | 132 MiB | 12.4 ms | 1.75 / 6.2 ms | 0.019 / 0.037 ms | 0.16 / 0.32 ms | 0.17 / 0.17 ms | 773 ms | 239 ms | 310 MiB |
| 4096 × 4096, **20 layers: refused by design**; measured at the limit, 8 layers | 512 MiB | 578 MiB | 62 ms | 1.80 / 5.8 ms | 0.037 / 0.068 ms | 0.38 / 0.63 ms | 0.82 / 0.77 ms | 2.7 s | 1.1 s | 1267 MiB |
| 8192 × 8192, 2 layers (stress: the largest the limits allow) | 512 MiB | 770 MiB (incl. a 256 MiB composite) | 72 ms | 2.46 / 6.6 ms | 0.066 / 0.12 ms | 0.71 / 1.23 ms | 3.0 / 2.9 ms | 2.9 s | 1.1 s | 1833 MiB |

Notes:
- **4096 × 4096 × 20 is not possible.** DrawApp limits a document to 512 MiB of layer pixels
  (`DocumentLimits::MaxPixelBytes`), the same limit the file reader enforces. 4096² × 20 would be
  1.25 GiB, so the app refuses the 9th layer of that size. This limit is unchanged in milestone 9.
  Lifting it would mean tiled or sparse layer storage (decision D1 in
  `research/milestone9-architecture-decisions.md`).
- **Stroke event** = engine + undo capture + composite refresh for one input event, as
  `CanvasItem` does it. It is CPU time, not latency to the screen.
- **Save** includes encoding and verification; open includes verification and decoding.
- **Peak private** includes the save buffer and the generator's temporary images.

## Brush engines (`bench_m9 brushes`, 2048 × 1536 × 10 layers)

One input event of a 1000-event stroke (engine + undo capture + composite refresh):

| Brush | median ms | p95 ms | max ms | dabs/s (dabs ÷ processing time) | engine buffers | undo snapshot |
|---|---|---|---|---|---|---|
| Solid Round 12 px (milestone 6 default) | 0.021 | 0.070 | 0.15 | 131 000 | 792 KiB | 1.5 MiB |
| Solid Round 128 px | 0.0005 | 0.49 | 0.66 | 3 700 | 1.5 MiB | 2.7 MiB |
| Clean Ink | 0.041 | 0.11 | 0.25 | 170 000 | 784 KiB | 1.5 MiB |
| Sketch Pencil (textured) | 0.074 | 0.12 | 0.19 | 111 000 | 768 KiB | 1.4 MiB |
| Textured Paint 40 px | 0.20 | 0.33 | 0.67 | 15 100 | 1.2 MiB | 1.8 MiB |
| Textured Paint, texture each dab + rotated grain | 0.22 | 0.37 | 1.20 | 13 500 | 1.2 MiB | 1.8 MiB |
| Dense textured 120 px, build-up, each dab | 0.53 | 0.87 | 1.94 | 1 700 | 2.1 MiB | 2.8 MiB |
| Soft Airbrush 90 px | 0.19 | 0.30 | 1.21 | 5 200 | 1.5 MiB | 2.6 MiB |
| Smudge 36 px | 0.12 | 0.23 | 0.64 | 17 900 | 25 KiB | 1.7 MiB |
| Smudge, dulling + colour rate | 0.14 | 0.28 | 0.70 | 15 200 | 25 KiB | 1.7 MiB |
| Wet paint 40 px | 0.062 | 0.12 | 0.62 | 18 100 | 30 KiB | 1.7 MiB |
| Wet paint 120 px | 0.0004 | 0.76 | 1.98 | 2 500 | 240 KiB | 2.6 MiB |
| Spray 80 px, 60 particles | 0.0003 | 0.10 | 0.44 | 13 100 | 1.1 MiB | 2.1 MiB |
| Hatching 80 px | 0.032 | 0.17 | 0.77 | 11 300 | 1.2 MiB | 2.2 MiB |
| Colour dynamics 30 px (per-pixel colour) | 0.024 | 0.060 | 0.34 | 55 300 | 2.6 MiB | 1.7 MiB |
| Dynamics-heavy 30 px (4 parameters follow sensors) | 0.023 | 0.062 | 0.29 | 105 000 | 856 KiB | 1.5 MiB |
| Count 4 + scatter 30 px | 0.065 | 0.17 | 0.52 | 84 500 | 1.1 MiB | 2.0 MiB |
| Start and end taper, 24 px | 0.019 | 0.046 | 1.06 | 95 500 | 880 KiB | 1.6 MiB |

Notes:
- A median near zero means most events placed no dab: large brushes space their dabs further
  apart than one event moves.
- The end-taper maximum (1.06 ms) is the pen-up event, which draws the held-back tail.
- No brush exceeded 2 ms for any event. 16.7 ms is one 60 Hz frame; that is a target, not a
  measured frame time.

### Long strokes (10 000 events, 2048 × 1536 × 10)

| Brush | total | per event median / p95 / max | dabs | engine buffers | undo snapshot | process private after |
|---|---|---|---|---|---|---|
| Solid Round 24 px | 220 ms | 0.021 / 0.041 / 0.29 ms | 14 810 | 2.3 MiB | 4.4 MiB | 139 MiB |
| Textured Paint 40 px | 1.9 s | 0.18 / 0.33 / 3.6 ms | 28 551 | 2.4 MiB | 4.5 MiB | 139 MiB |
| Smudge 36 px | 1.3 s | 0.13 / 0.27 / 4.3 ms | 20 863 | ≈ 0 | 4.5 MiB | 137 MiB |
| Wet paint 40 px | 0.63 s | 0.067 / 0.14 / 1.0 ms | 8 886 | ≈ 0 | 4.5 MiB | 137 MiB |

Memory per stroke stays bounded: the buffers grow with the area touched, not the event count.

### Milestone 6 presets before and after milestone 9 (`bench_core 30 presets`, 1 layer)

Baseline = `m9-baseline-bench_core.md` (before milestone 9). After = three runs after the final
change, as ranges.

| Preset, size | Baseline median / p95 ms | After median / p95 ms |
|---|---|---|
| Solid Round 12 | 0.013 / 0.056 | 0.013–0.014 / 0.055–0.058 |
| Clean Ink 8 | 0.028 / 0.076 | 0.026–0.031 / 0.072–0.082 |
| Sketch Pencil 200 | 0.000 / 1.080 | 0.000–0.001 / 0.89–0.93 |
| Soft Airbrush 200 | 0.000 / 0.743 | 0.001 / 0.78–0.82 |
| Textured Paint 40 | 0.146 / 0.263 | 0.148–0.155 / 0.26–0.27 |
| Textured Paint 200 | 0.762 / 1.941 | 0.747–0.752 / 1.85–1.90 |
| Blend / Smudge 200 | 0.000 / 1.213 | 0.001 / 1.27–1.30 |

What happened along the way:
- The extended engine first measured 10–50 % slower on large textured or build-up brushes
  (Textured Paint 200 px median 0.86–0.91 ms; Sketch Pencil 200 px p95 1.20–1.48 ms).
- Two fast paths restored it:
  - the milestone 6 rasteriser loop is used when no new per-pixel feature is active;
  - the usual paper grain is read directly from its row.
- Remaining differences are within run-to-run variation, except Soft Airbrush and Smudge at
  200 px, whose p95 is 5–7 % above the baseline's single run. That is not confirmed as a
  regression.

## Multi-layer transforms (`bench_m9 transforms`, no selection, all layers but the background)

Measured twice. The ranges show both runs; the second ran nearer the machine's memory limit
(peak 3.5 GiB while about 4 GiB of RAM was free).

| | 9 layers × 2048 × 1536 | 7 layers × 4096 × 4096 |
|---|---|---|
| Begin (cache each layer's source and reductions) | 96–102 ms | 411–496 ms |
| Preview frame, **per layer** (milestone 8 approach): fast render of every layer + composite | 198–215 ms | 812–1098 ms |
| Preview frame, **merged copy** (milestone 9): fast render of one merged layer + composite | **25–33 ms** | **152–262 ms** |
| Preview after releasing a handle, merged (bicubic) | 67–93 ms | 430–537 ms |
| Apply: prepare all layers (bicubic, exact per layer) | 0.60–0.90 s | 2.3–3.2 s |
| Apply: commit + one group undo entry + composite | 103–180 ms (108 MiB kept by history) | 0.53–1.03 s (**not kept**: over the 128 MiB undo budget) |
| Undo / redo of the transform + composite | 36–54 / 37–58 ms | **not meaningful**: there is no entry to undo, and the app now says so |
| Transform session memory | 238 MiB | 1021 MiB |
| Process peak private during the transform | 826 MiB | 3492 MiB |

**Optimisation with evidence.** Adjacent layers without a selection are previewed from one
merged copy. That makes the preview frame 6–8× faster at 2048 × 1536 and 4–7× faster at
4096 × 4096, at the cost of a merged copy and its reductions (about 2.3 × one layer).
`tst_canvas` checks that the merged preview equals the applied result except at a few
overlapping anti-aliased edges.

**Targets not reached (reported, not claimed).**
- 60 fps previews: the 9-layer 2048 × 1536 preview frame (25–33 ms) is about 30–40 fps of
  processing; the 4096² preview (152–262 ms) is about 4–7 fps.
- Apply on 7 layers × 4096² takes 2.3–3.2 s on the UI thread.
- Keeping a 4096² multi-layer transform undoable needs more than the 128 MiB undo budget.

## Memory

| | |
|---|---|
| Idle app | 89.6–89.9 MiB working set, 75.5 MiB private |
| Opening a small project | peak private was **1055 MiB** before the `readBounded` fix (the read buffer reserved the 512 MiB file limit) and **29 MiB** after |
| Active painting | stroke buffers 25 KiB – 2.7 MiB per stroke in the table above, plus the undo snapshot (1.4–2.8 MiB per 1000-event stroke at 2048 × 1536) |
| Peak | the document, plus about 2–4× the document during a whole-layer multi-layer transform (table above) |

## Optimisations investigated

| Idea | Outcome |
|---|---|
| Dirty-region rendering, incremental tile updates | already present (milestones 3–6): per-event work is bounded by the touched tiles |
| Avoiding full-canvas invalidation | unchanged; strokes refresh only their rect |
| Shared brush resources, tip caching | generated tips and textures are shared statics. Imported tips are mip-mapped per stroke (under 1 ms at 256²); not cached, since no measurement showed a need |
| Reduced allocations, reusable buffers | by design, a stroke allocates its carried patch once and a tile the first time it is touched; smudge and wet paint under a selection also allocate a working region per dab. Not profiled with an allocation tracer |
| Fast path for the common rasteriser case | **done**, measured above |
| Direct grain lookup | **done**, measured above |
| Merged multi-layer preview | **done**, measured above |
| Thread-safe background work | transform rows are rendered on a thread pool (milestone 8). A data race in it was **found and fixed** this milestone |
| SIMD | not attempted: the hot loops are dominated by the per-pixel tip evaluation and branches, and no SIMD version was measured |
| GPU | not attempted: no measurement showed that a CPU path blocks interactive painting. Strokes stay under 2 ms per event; transforms are the slow part, and they would need a different (tiled or GPU) layer store |
| Memory pressure | bounded file reads (fixed); out-of-memory during transform Apply is handled (milestone 8/9 transaction); saving under memory pressure is not tested |

## Android

Not measured. The emulator is not a performance reference, and no physical device was available.
Android frame stability is therefore **unknown**.

## Comparison with Krita and PhotoCraft: not run

Krita is not installed on this machine, and PhotoCraft needs a Rust toolchain that is not
installed. **No comparative numbers exist, and DrawApp makes no claim of being lighter or faster
than either.**

Procedure for a machine where they are installed:

1. **Same conditions.** One machine, mains power, other apps closed, the same display and
   scaling. Record CPU, RAM, GPU, OS, and the app versions and builds (Krita: the release build
   from krita.org).
2. **Documents.**
   - 2048 × 1536 with 10 layers;
   - 4096 × 4096 with 8 layers, which is DrawApp's limit (Krita can also be measured at 20
     layers);
   - 8-bit RGBA; the same content in each app, created by painting with the same input or by
     importing the same PNG layers.
3. **Start-up.** Cold start (after a reboot) and warm start, 5 times each; time from launch to
   the first responsive canvas. Use the same method in each app: a screen recording at 60 fps,
   counting frames.
4. **Memory.** Idle working set and private bytes 10 s after start-up, after opening each
   document, and after a 60-second painting session. Tool: Process Explorer, sampling the peak.
5. **Brush responsiveness.**
   - Replay the same input trace with an input recorder (or a pen-plotter rig) at 100 % zoom:
     a 1000-point stroke with a hard round brush of 12 and 128 px, and a textured brush of 40 px.
   - Measure the time from the last input to the final stroke appearing, with a 240 fps camera.
     Software timers in the two apps are not comparable.
6. **Textured brush.** 10 passes over the same area; note dropped input and the stroke's
   catch-up time.
7. **Transforms.** Select the 9 or 7 paint layers, start a free transform, then drag a corner
   handle continuously for 5 seconds. Count preview frames from the screen recording and time
   Apply.
8. **Undo/redo.** Time undoing and redoing the transform and a long stroke (recording).
9. **Save/load.** Save to the native format (`.kra` / `.drawapp`) on the same SSD and reopen;
   time with a stopwatch from the recording. Record the file sizes.
10. **Report.** Medians of 5 runs, with the method stated for every number. Do not mix
    processing timers with camera latency.

PhotoCraft (`research/milestone9-photocraft-architecture.md`, HEAD 8b216216):
- run `cargo build --release`, then the same steps where the features exist;
- note any step it cannot do (for example multi-layer transform), and do not substitute
  numbers for it.

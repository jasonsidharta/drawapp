# Benchmarks

Baseline for the CPU raster prototype. Numbers describe this prototype on one machine. They are
not a comparison with other apps and do not support claims of being "faster" or "smaller".

## Method

- `bench_core` (tests/bench_core.cpp) times the code paths the app uses:
  - one input event (`StrokeSpacer::moveTo` plus `RasterDocument::stampDabs`) over a 1000-event
    zig-zag stroke with varying pressure;
  - `ViewRenderer::render` into a 1920×1080 `QImage` for a full view and for a 64×64 dirty rect;
  - `RasterDocument::clear`.
- In-app timing: `DRAWAPP_STATS=1` logs `CanvasItem::paint()` CPU time and per-input-event time
  every 2 s. This excludes scene-graph texture upload and display latency.
- Memory: the process working set and private bytes, read from Windows while the deployed app
  ran.

Run: `build\windows-mingw-release\bench_core.exe 30` (Release build, GCC 15.1 `-O3`).

## Results: 2026-10-11, Windows 11 25H2, Intel Core Ultra 7 155H, 15.4 GB RAM, other desktop apps running, power state not recorded

Document: 2048×1536 ARGB32 premultiplied = 12.0 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event, brush 4 px (~8 dabs/event) | 0.0094 | 0.0175 | 0.7683 |
| stroke event, brush 12 px (~5 dabs/event) | 0.0056 | 0.0176 | 0.0718 |
| stroke event, brush 64 px (~1 dab/event) | 0.0052 | 0.0088 | 0.0348 |
| stroke event, brush 256 px (most events emit no dab) | 0.0001 | 0.0335 | 0.1700 |
| render full 1920×1080 view, zoom 0.5 | 0.8992 | – | 4.5389 |
| render full 1920×1080 view, zoom 1.0 | 0.9822 | – | 2.3645 |
| render full 1920×1080 view, zoom 4.0 | 1.5675 | – | 2.8129 |
| render 64×64 dirty rect, zoom 1.0 | 0.0032 | – | 0.0752 |
| clear document | 0.2718 | – | 0.3294 |

In-app, deployed build, real OS mouse stroke of 100 events at 49 % zoom in a 2586×1671 px window:
`paint()` avg 0.136 ms (max 5.08 ms, the first full paint); input handling avg 0.061 ms
(max 0.099 ms); 810 dabs.

Memory, deployed app, idle after launch: working set 202 MB, private bytes 167 MB. After the
drawing session: working set 198 MB, private bytes 165 MB. The document accounts for 12 MiB; we
have not broken down the rest (Qt Quick, Controls, D3D11 and the item's backing texture).

## Milestone 1 additions: 2026-10-11, same machine and conditions

`bench_core 30` after adding undo and files. The undo rows use the same 1000-event zig-zag stroke,
capturing tiles exactly as the app does; the release event includes the commit (dropping
unchanged tiles and pushing to history).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event + undo capture, brush 12 px | 0.0059 | 0.0203 | 0.0650 |
| undo (or redo) of that whole stroke, 1.4 MiB of tiles | 0.2103 | – | 0.2315 |
| stroke event + undo capture, brush 64 px | 0.0054 | 0.0149 | 0.0945 |
| undo (or redo) of that whole stroke, 2.1 MiB of tiles | 0.2722 | – | 0.3465 |
| save project, 4000 painted dabs (0.60 MiB file) | 86.8 | – | 126.2 |
| open project, same | 32.8 | – | 40.9 |
| export PNG, same (0.87 MiB file) | 149.6 | – | 152.0 |
| save project, random noise, worst case (11.42 MiB file) | 443.6 | – | 458.6 |
| open project, random noise | 142.7 | – | 148.5 |
| export PNG, random noise (11.13 MiB file) | 498.7 | – | 510.7 |

Stroke events with undo capture cost the same as without it within run-to-run noise (0.0059 vs
0.0059 ms median at 12 px in this run). Save/open/export run on the UI thread, so the worst-case
numbers are visible pauses. The undo budget is a fixed 128 MiB; actual history memory depends on
what is drawn and was not measured in the running app.

## Milestone 2: crash recovery, 2026-10-11, same machine

Measured in the deployed app with `DRAWAPP_STATS=1` (log lines `[recovery]` and `[stats]`),
driven by real OS mouse/keyboard input and killed with `taskkill /F`. "Change → recovery" is the
age of the oldest change not yet in a committed recovery file at the moment a write commits.
"UI loop lag" is how late a 5 ms timer fired, the worst value per 2 s window.

| Run | Interval | Document | Snapshot copy (UI thread) | Worker encode + write | Change → recovery | UI loop lag while drawing |
|---|---|---|---|---|---|---|
| A: 14 strokes over 32 s, then kill | 10 s (default) | `manual1.drawapp`, light artwork (23 KB recovery file) | 3.0–3.7 ms | 38–40 ms | 1.5–9.3 s (4 commits) | ≤ 4 ms |
| B: 8 rounds of restore → draw 1.5–4 s → kill | 400 ms | random-noise worst case (11.2–11.6 MB recovery file) | 2.1–3.5 ms (n = 61) | 344–388 ms (55 commits) | median 751 ms, max 784 ms | ≤ 5 ms |

Timer accuracy: ticks were 10 000–10 001 ms apart in run A and 403–410 ms apart in run B. The only
larger UI lag seen (24–26 ms) was the first full repaint after opening or restoring a document.

**Loss window.** In run A the last commit before the kill was at 21:01:56.09; stroke 14 (ended
21:01:57.52) was lost and strokes 1–13 were restored exactly. The 10 s timer does **not** mean
"at most 10 s lost". From these measurements the exposure is roughly *interval + worker time*,
plus up to one more worker time if a write is still running at a tick: about 10.1 s for light
artwork and about 10.8 s for worst-case 2048×1536 content at the default interval (the latter is
an estimate from run B's worker times, not a measurement at 10 s). It grows if the UI thread is
blocked (saving or opening runs on the UI thread, up to 0.44 s worst case), and it does not cover
OS crashes or power loss, which were not tested.

Every forced kill (1 in run A, 8 in run B) left a valid recovery file, as checked by an
independent Python decoder (header, metadata checksum, project checksum, decompressed size).
One kill in run B landed during the file-write phase and left a temporary file, which the next
start removed. The original project files had identical SHA-256 hashes before and after.

## Milestone 3: layers, 2026-10-11, same machine

`bench_core 30`. All numbers are **CPU processing time on the calling thread**, not
input-to-display latency (which also includes event delivery, scene-graph texture upload, GPU and
display; not measured). The machine had other desktop applications in use during these runs, and
run-to-run variation was large (the single-layer build's save measured 87–144 ms across runs), so
compare only rows measured side by side.

**Single-layer build vs layered build, same content, run back to back** (milestone 2 commit
`a98ec27` built in a separate worktree):

| Operation | single-layer build | layered build |
|---|---|---|
| stroke event + undo capture, 12 px brush (median / p95 ms) | 0.0070 / 0.0222 | 0.0069 / 0.0213 (includes composite refresh) |
| save project, 4000 dabs on transparent (ms, median) | 95.5 | 86.7 |
| open project (ms) | 32.9 | 34.5 |
| export PNG (ms) | 152.5 | 157.5 (includes compositing) |
| app idle private memory, offscreen platform, 3 launches (MiB) | 49.8–50.0 | 61.9–62.1 (+12 MiB composite cache) |

No measurable regression for one layer apart from the composite cache's memory.

**Representative documents** (opaque white background + layers with 2500 dabs each, one layer at
70 % opacity, one hidden when there are five or more):

| Operation (median ms unless noted) | 1 layer | 5 layers | 10 layers |
|---|---|---|---|
| Process private memory for the document | 24 MiB | 72 MiB | 132 MiB |
| File size (project = recovery file, approx.) | 0.01 MiB | 2.06 MiB | 4.60 MiB |
| Stroke event: stamp + undo capture + composite refresh (median / p95) | 0.0068 / 0.021 | 0.0093 / 0.033 | 0.0104 / 0.028 |
| Full composite rebuild (visibility / opacity / order change) | 1.0 | 5.6 | 12.2 |
| Recovery snapshot on UI thread (copy-on-write) | 0.0003 | 0.0007 | 0.0007 |
| First dab after a snapshot (detaches the active layer) | 2.26 | 2.53 | 2.47 |
| Same dab without a pending snapshot | 0.0025 | 0.0033 | 0.0026 |
| Recovery encode + atomic write (worker thread) | 42 | 370 | 800 |
| Save project (UI thread) | 41 | 369 | 776 |
| Open project (UI thread) | 29 | 137 | 251 |
| Export PNG: composite + encode (UI thread) | 89 | 224 | 350 |

Observations:
- Stroke cost per input event stays about 0.01 ms up to 10 layers because only the dab rect is
  recomposited.
- The recovery snapshot no longer copies pixels on the UI thread (milestone 2 copied the image,
  3.0–3.7 ms each autosave). Instead the first edit of a shared layer after a snapshot copies that
  layer once (≈ 2.3–3.2 ms), independent of the number of layers.
- Save/open and recovery writes grow roughly linearly with painted layer content; at 10 layers a
  save blocks the UI for about 0.8 s, and the crash-recovery loss window grows by the worker time
  (see Milestone 2 for how the window is defined).
- History memory is extra: up to 128 MiB, including deleted layers held for undo.

Not measured for layers: on-screen app memory with a real GPU surface (the session was postponed
because the desktop was in use), Android, and input-to-display latency.

## Milestone 4: async save and the first edit after a snapshot, 2026-10-12, same machine

`bench_canvas` (tests/bench_canvas.cpp) drives the real `CanvasItem` on Qt's offscreen platform:
synthetic mouse strokes at 250 Hz (one event every 4 ms) for about 12.5 s per document, an
autosave (recovery snapshot) every 1 s, and one asynchronous project save started about 5 s in,
while drawing continues. Documents are those of the milestone 3 table (2048×1536, 1, 5 and 10
painted layers). Release build; other desktop applications were open; power state not recorded.
Two runs back to back; the table gives run 1 / run 2.

Run: `$env:QT_QPA_PLATFORM="offscreen"; build\windows-mingw-release\bench_canvas.exe`.

What each row measures, all **CPU time on the UI thread unless marked "worker"**:
- *Input handler*: `CanvasItem`'s mouse handler for one event (spacing, dab stamping into the
  active layer, undo capture, composite refresh of the dab rect).
- *First event after a snapshot*: the same for the first event that paints after an autosave or
  save snapshot. This is where the active layer, still shared with the snapshot, is copied
  (12 MiB).
- *Event loop + render*: processing the queued events and the item's paint after each event.
- *Whole iteration*: handler plus event loop plus render for one event. Its maximum is the
  largest UI stall seen during the run. The first-edit iteration (layer copy plus redraw) is the
  maximum in every run.

| Measure (ms) | 1 layer | 5 layers | 10 layers |
|---|---|---|---|
| Input handler, ordinary event, median / p95 | 0.10 / 0.93 – 0.94 | 0.10 / 0.87 – 0.88 | 0.10 / 0.73 – 0.77 |
| First event after a snapshot (layer copy), median | 3.03 / 3.10 | 3.30 / 3.23 | 3.13 / 3.30 |
| First event after a snapshot, max | 4.40 / 4.23 | 3.96 / 4.06 | 3.73 / 4.67 |
| Event loop + render after an event, max | 0.91 / 0.72 | 0.62 / 1.00 | 0.63 / 1.02 |
| Whole iteration, p95 | 1.00 / 1.00 | 0.93 / 0.96 | 0.83 / 0.78 |
| **Largest UI stall** (whole iteration, max) | **4.40 / 4.23** | **3.96 / 4.06** | **3.73 / 4.67** |
| Save request on the UI thread (snapshot + start) | 0.12 / 0.13 | 0.19 / 0.09 | 0.08 / 0.15 |
| Save, worker | 48 / 43 | 352 / 350 | 755 / 785 |
| Save, request to finished signal (drawing continued) | 50 / 44 | 356 / 353 | 756 / 788 |
| Recovery write, worker, median | 45 / 45 | 337 / 340 | 743 / 718 |

12–13 snapshots per run. In milestone 3 a 10-layer save blocked the UI for about 0.8 s. Now
the UI thread spends about 0.1 ms to start it, and the worker time (0.75–0.79 s) passes while
drawing continues. The first edit after each snapshot costs about 3–4.7 ms whatever the layer
count, because only the active layer is copied.

`bench_core 30` in the same session reproduced the milestone 3 rows within run-to-run
variation. For example, the first dab after a snapshot took 2.1–2.2 ms median, and save/open
took 38/20, 340/119 and 728/236 ms for 1/5/10 layers. Opening runs on the worker as well. Only
replacing the document runs on the UI thread, and that step was not benchmarked separately.

Limits of these numbers:
- Offscreen platform: no GPU texture upload or display. This is CPU time, **not pen-to-display
  latency**.
- Synthetic input at a fixed rate.
- Not measured on the Android emulator, which uses the host GPU and an x86_64 image and would
  not represent a phone.

## Android emulator observations (2026-10-12, not a physical device)

Pixel Tablet AVD, Android 16 (API 36, google_apis x86_64 image), emulator 36.6.11 with
`-gpu host -no-window`, on the same Windows machine. Only qualitative timings were recorded:
- App cold start (`am start -W` TotalTime): 0.8–2.8 s, and 11 s once right after a forced stop.
- On the first launch after the emulator booted, the window showed its first frame for 10–15 s
  before later frames, such as the recovery prompt, appeared.
- With the SwiftShader software renderer, the canvas showed a diagonal tear; with `-gpu host`
  it rendered correctly.
- Once the whole emulator display froze while the soft keyboard animated in. The status-bar
  clock stopped too, so the emulator's compositor, not the app, had hung. A cold restart of the
  emulator fixed it.

These are properties of this emulator setup and say nothing about a phone or tablet.

## Milestone 5: brush engine, smoothing and save verification, 2026-10-12, same machine

**Conditions:** the machine was in use. Windows reported 23–38 % CPU load before some runs, and one
10-layer `bench_canvas` run took 18.8 s for 12 s of strokes. Absolute file times are therefore
noisier than in earlier milestones; for example, the unchanged `ProjectFile::save` of 10 layers
measured 0.73 s in milestone 4 and 1.4 s here. Compare only rows measured in the same run. All
times are **CPU time on the calling thread, not pen-to-display latency**.

### Brush engine per input event (`bench_core 30`, two runs: run 1 / run 2)

Same 1000-event zig-zag stroke on one opaque 2048 × 1536 layer, with undo capture and composite
refresh, as `CanvasItem` runs it. The "milestone 4 path" is the previous QPainter dab stamping.

| Variant | median ms | p95 ms | max ms |
|---|---|---|---|
| milestone 4 path, 12 px | 0.0075 / 0.0097 | 0.025 / 0.030 | 0.11 / 0.11 |
| compositor, 12 px, 100 %, smoothing off | 0.0044 / 0.0055 | 0.011 / 0.014 | 0.11 / 0.22 |
| compositor, 12 px, 50 %, smoothing off | 0.0044 / 0.0057 | 0.011 / 0.014 | 0.17 / 0.14 |
| compositor, 12 px, 50 %, smoothing 20 px string | 0.0048 / 0.0066 | 0.012 / 0.022 | 0.15 / 0.22 |
| milestone 4 path, 128 px | 0.0041 / 0.0082 | 0.030 / 0.066 | 1.40 / 0.28 |
| compositor, 128 px, 50 % | 0.0037 / 0.0055 | 0.095 / 0.131 | 0.46 / 0.52 |
| smoothing only, string 0 / 20 / 80 px (no painting) | 0.0000 / 0.0003 / 0.0003 | ≤ 0.0004 | ≤ 0.011 |

- The uniform-opacity compositor costs no more per event than the dab stamping it replaces. It
  was 30–45 % lower at 12 px in both runs; at 128 px the p95 is higher (0.10–0.13 ms against
  0.03–0.07 ms), because each event recomputes whole dab rectangles.
- Smoothing adds about 0.0002–0.0003 ms per event. Its 0.5 px walking steps cost about the
  same for 20 and 80 px strings.
- The mask used 94 tiles (376 KiB) for the 12 px zig-zag and 184 tiles (736 KiB) at 128 px,
  freed when the stroke ends.
- At 128 px most events add no dab, since the spacing is 19 px; the medians mostly measure events
  without painting.

### Saving with verification (`bench_core`, run 2)

| Operation (median ms) | 1 layer | 5 layers | 10 layers |
|---|---|---|---|
| `ProjectFile::verify` of the encoded project (checksum + decompress each layer, worker) | 13.7 | 98.2 | 223.9 |
| App save to a local file: `SafeSave` = encode + verify + space check + atomic write (worker) | 75.6 | 548.3 | 1237.5 |
| Encode + atomic write alone (as in milestone 4), same run | 61.9 | 384.1 | 1407.5 |

Measured alone, verification costs 16–26 % of the encode + write time from the same run. The
complete `SafeSave` rows are too noisy to show that difference directly: 1 layer +22 %, 5 layers
+43 %, 10 layers −12 %; the 10-layer rows varied by 2× between repetitions (p95 2.5 s). Saves
run on the worker, so this is time before "Saving…" ends, not a UI pause.

### First stroke after an autosave, largest UI stall (`bench_canvas`)

Real `CanvasItem`, offscreen, synthetic mouse strokes at 250 Hz, autosave every 1 s, one
async save at about 5 s. Each run covers documents with 1, 5 and 10 layers.

| Run | First event after a snapshot, median ms (1 / 5 / 10 layers) | Largest UI stall, ms (1 / 5 / 10) | Ordinary event, median ms |
|---|---|---|---|
| Defaults (100 %, smoothing off) | 3.0 / 5.1 / 3.5 | 6.6 / 7.2 / 5.9 | 0.09–0.10 |
| 50 % opacity, smoothing 50, run A | 5.6 / 3.2 / 4.9 | 27.2 / 4.5 / 7.7 | 0.10–0.16 |
| 50 % opacity, smoothing 50, run B | 5.3 / 3.1 / 3.1 | 9.2 / 10.0 / 10.7 | 0.12–0.24 |

The largest stall is still the first edit after a snapshot: one layer copy plus the redraw. It was
3.7–4.7 ms in milestone 4 on a quieter machine and 5.9–10.7 ms here, with a single 27 ms outlier
in run A that did not recur in run B. With today's load, a real increase cannot be separated
from noise. In `bench_core`, where noise is lower, the layer copy measured 2.5–3.7 ms (2.1–2.2 ms
in milestone 4). The save worker in `bench_canvas` took 55 / 541 / 1186 ms with verification.
This number has not been measured on a quiet machine.

### Android emulator (not a performance measure)

On the x86_64 emulator, provider saves of a one-layer drawing took 0.75–2.6 s on the worker. That
covers encode, verify, recovery copy with read-back, probe, reservation, write and read-back.
`posix_fallocate` succeeded on the Downloads provider's file descriptor. These numbers describe
the emulator, not a phone.

## Milestone 6: brush presets, 2026-10-12, same machine (Intel Core Ultra 7 155H, 15.4 GB RAM, Windows 11 25H2)

All numbers are CPU time on the UI thread with synthetic input. They are not pen-to-screen latency,
which also includes OS input delivery, the scene-graph upload, display refresh and the compositor.

### Per input event, every preset (`bench_core 30 presets`)

Setup:
- the 1000-event zig-zag with varying pressure and samples 8 ms apart, three strokes per row;
- time covers `StrokeEngine` (smoothing off), undo tile capture and the composite refresh, on the
  active layer of a representative 2048 × 1536 document;
- each preset runs at its own size and at 200 px;
- memory columns are per stroke: coverage tiles (8 KiB each), engine buffers, and the undo
  snapshot (the touched 64 × 64 layer tiles).

Median ms is 0.000 for most 200 px rows, because most events place no dab: a dab every
`spacing × 200` px of travel. Their p95 and max are the events that do.

| Preset | size px | median ms (1 / 5 / 10 layers) | p95 ms (1 / 5 / 10) | max ms (1 / 5 / 10) | coverage tiles | engine KiB | undo MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.051 / 0.054 / 0.057 | 0.098 / 0.102 / 0.106 | 0.50 / 0.32 / 0.32 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 / 0.000 / 0.000 | 1.01 / 1.06 / 1.23 | 2.29 / 2.26 / 2.15 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.026 / 0.027 / 0.030 | 0.071 / 0.075 / 0.077 | 0.15 / 0.18 / 0.24 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.053 / 0.059 / 0.065 | 0.62 / 0.68 / 0.80 | 1.06 / 1.39 / 1.88 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.012 / 0.014 / 0.016 | 0.054 / 0.060 / 0.062 | 0.14 / 0.38 / 0.55 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 / 0.000 / 0.000 | 0.45 / 0.50 / 0.58 | 0.94 / 1.14 / 1.67 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.141 / 0.156 / 0.187 | 0.24 / 0.26 / 0.31 | 0.70 / 0.63 / 0.65 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.000 / 0.000 / 0.001 | 0.72 / 0.81 / 0.94 | 2.24 / 1.87 / 6.51 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.146 / 0.153 / 0.162 | 0.25 / 0.26 / 0.28 | 0.79 / 0.79 / 0.75 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.75 / 0.81 / 0.90 | 1.84 / 2.00 / 2.26 | 3.27 / 3.27 / 4.17 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.075 / 0.099 / 0.102 | 0.13 / 0.19 / 0.20 | 0.31 / 0.75 / 0.37 | – | 25 | 2.0 |
| Blend / Smudge | 200 | 0.000 / 0.000 / 0.001 | 1.18 / 1.66 / 1.79 | 2.40 / 2.91 / 3.38 | – | 650 | 4.9 |
| Hard Eraser | 24 | 0.012 / 0.014 / 0.016 | 0.024 / 0.027 / 0.032 | 0.12 / 0.08 / 0.11 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 / 0.000 / 0.000 | 0.44 / 0.50 / 0.62 | 1.44 / 1.22 / 1.83 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.082 / 0.093 / 0.107 | 0.15 / 0.16 / 0.18 | 0.62 / 0.55 / 0.53 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.000 / 0.000 / 0.000 | 0.67 / 0.75 / 0.87 | 1.71 / 1.59 / 1.99 | 359 | 2872 | 5.6 |

The milestone 5 comparison, with the same table as before, is in `bench_core 30`. For the 12 px
uniform brush the stroke engine takes 0.0115–0.0122 ms median per event, against 0.0067 for the
milestone 4 QPainter path, which had no uniform opacity. The extra cost is the 16-bit coverage, the
dithered composite and the per-dab tip evaluation.

**First event after a snapshot** (a recovery or save snapshot makes the next edit copy the shared
layer):

| | 1 / 5 / 10 layers |
|---|---|
| Clean Ink 8 px, first event, median ms | 2.12 / 2.19 / 2.22 |
| Clean Ink 8 px, next event, median ms | 0.062 / 0.066 / 0.069 |

Brushes whose first dab waits for a travel direction (Textured Paint) pay the same layer copy on
their second event instead: about 3.6–4.3 ms including a 200 px dab.

### On the canvas with autosave (`bench_canvas 1 0 <preset>`)

Real `CanvasItem`, offscreen, synthetic mouse strokes at 250 Hz, autosave every 1 s, one async
save at about 5 s; the preset's own settings (Clean Ink includes its smoothing).

| Preset | Ordinary event, median / p95 / max ms (1, 5, 10 layers) | First event after a snapshot, median ms (1 / 5 / 10) | Largest UI stall, ms (1 / 5 / 10) |
|---|---|---|---|
| Clean Ink | 0.12 / 0.74–0.95 / 1.2–1.7 | 2.8 / 3.2 / 2.7 | 3.6 / 4.0 / 3.3 |
| Textured Paint | 0.41 / 1.06–1.07 / 2.5–3.1 | 2.9 / 3.0 / 2.9 | 4.2 / 4.2 / 3.6 |

Machine load was not recorded (other desktop apps were open). The largest stall is still the first
edit after a snapshot.

### Android emulator (behaviour, not performance)

On the x86_64 emulator by touch: the preset panel, thumbnails, favourites, scratchpad, Textured
Paint and smudge strokes, and airbrush buildup while held still all worked. A 1.5 s hold built a
dark dot where a tap left a faint one. No timings were taken; the emulator is not a phone.

## Milestone 8: selections and transforms, 2026-10-12, same machine (Intel Core Ultra 7 155H, 15.4 GB RAM, Windows 11 25H2)

`bench_core 30 selection`: CPU wall time on the calling thread. Renders split rows over a
dedicated pool of worker threads, with the caller taking part. Canvas 2048 × 1536, one layer of
the representative 5-layer document. These are not display latencies.

**Selections**

| Operation | median ms | p95 ms |
|---|---|---|
| Rasterize a 1024 × 768 rectangle / ellipse / 600-point lasso (full-size mask) | 1.0 / 1.3 / 1.7 | 1.5 / 1.5 / 1.8 |
| Combine (Add), with new bounds | 6.7 | 7.6 |
| Invert, with new bounds | 3.5 | 3.6 |
| Outline of a 1024 × 768 ellipse / of Select All | 5.7 / 24.1 | 6.5 / 31.3 |

Each of these runs once per selection change, not per frame.

**Transforms** (preview = bilinear while dragging; apply = Catmull–Rom, written once on Apply)

| Source | begin | move: preview / apply | scale 1.5×: preview / apply | rotate 30°: preview / apply | reduce to 0.3×: preview / apply | selection after Apply |
|---|---|---|---|---|---|---|
| 512 × 512 selection | 3.9 | 0.2 / 0.2 | 3.0 / 9.4 | 1.6 / 4.9 | 0.3 / 0.8 | 5.5 |
| 1024 × 1024 selection | 8.0 | 0.6 / 0.5 | 14.7 / 42.0 | 6.3 / 19.8 | 0.8 / 2.3 | 6.3 |
| Whole layer (no selection) | 11.1 | 1.4 / 1.6 | 17.1 / 61.3 | 16.6 / 48.3 | 2.1 / 5.7 | – |

All values are medians in ms. A whole-pixel move or flip is a pixel copy. Before that path
existed, a whole-layer move took 13.5 ms per preview and 49.9 ms to apply (same machine, earlier
run that day).

**Preview composite** (the transformed layer recomposited with the others over the changed area;
the full canvas here, the worst case): 1.2 / 5.1 / 12.8 ms median for 1 / 5 / 10 layers.

A drag frame is roughly preview + composite. For the whole layer scaled on 10 layers that is
about 30 ms CPU (about 30 frames per second), and under 15 ms for selections up to 1024 px.
Apply on the UI thread takes up to 61 ms (whole layer, scaled).

**Memory while a transform preview is open:** a copy of the layer and of the composite
(24 MiB), the base layer when there is a selection (12 MiB), and the source with its reductions
(about 1.33 × the source). All of it is freed on Apply or Cancel.

**Android emulator:** the selection and transform tools were exercised by touch (BUILD.md); no
timings were taken.

## Not measured yet

- End-to-end latency from pen contact to photons (needs hardware or a high-speed camera). The
  smoothing adds a spatial lag (up to the string length) by design, but no time delay; how that
  feels with a real pen is unmeasured.
- Scene-graph texture upload cost per frame for `QQuickPaintedItem`.
- Physical Android devices, iOS, Linux, real pen hardware, larger canvases, battery.
- Brush feel on hardware: pressure and tilt response, and a comparison with Krita's presets on
  the same device (no pen hardware was available for milestone 6).
- Brush performance on a phone or tablet CPU (the 200 px brushes are the ones to watch).
- Transform previews on a phone or tablet CPU (whole-layer scaling is the case to watch), and the
  time from a transform drag to the screen.
- On-screen Windows memory and timing with a real GPU surface for layers and async save.

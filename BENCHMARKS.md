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

## Not measured yet

- End-to-end latency from pen contact to photons (needs hardware or a high-speed camera).
- Scene-graph texture upload cost per frame for `QQuickPaintedItem`.
- Android/iOS/Linux, real pen hardware, larger canvases, battery.

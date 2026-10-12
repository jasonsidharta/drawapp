# Milestone 10 only: workloads milestone 9 cannot open (raw output)

20 repetitions, one process per section, same session as the A/B runs. NOTE: other programs held most of the 15.4 GB RAM (about 0.9 GB free); doc8192x10 and transforms20 ran into paging and are re-measured in `m10-heavy-rerun.md`.

## doc512x1

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 512 x 512, 1 layer(s)

Dense equivalent: 1 MiB of layers + 1 MiB composite. Allocated tiles: layers 1.0 MiB, composite 1.0 MiB. Private memory added by the document: 2 MiB (process 4 MiB, working set 12 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 2 | - | - |
| full composite rebuild | 0.25 | 0.25 | 0.25 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 0.975 | 5.849 | 5.849 |
| render the view without the pyramid (100 %) | 1.010 | 2.992 | 2.992 |
| render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) | 1.3 | - | - |
| render the view as the canvas does (fit), later frames | 0.504 | 0.754 | 0.754 |
| render the view as the canvas does (100 %), later frames | 0.500 | 0.583 | 0.583 |
| render the view as the canvas does (fit), while panning 7 px per frame | 0.516 | 0.695 | 0.695 |
| render the view as the canvas does (5 %) | 0.285 | 1.388 | 1.388 |
| (view pyramid memory after these frames: 0.3 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0076 | 0.0269 | 0.1133 |
| undo that stroke (0.5 MiB of tiles) | 0.00 | 0.01 | 0.01 |
| redo that stroke | 0.00 | 0.00 | 0.00 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.0930 | 0.1404 | 0.2982 |
| save project (encode + atomic write) | 7.4 | 7.4 | 7.4 |
| open project (read + verify + decode) | 8.5 | 8.5 | 8.5 |
| recovery: snapshot on the UI thread | 0.0002 | 0.0011 | 0.0011 |
| recovery: encode (worker) | 3.6 | 3.6 | 3.6 |

File 0.0 MiB. Process peak private memory 27 MiB, peak working set 34 MiB. MemoryBudget peak 4 MiB.

## doc4096

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 4096 x 4096, 20 layer(s)

Dense equivalent: 1280 MiB of layers + 64 MiB composite. Allocated tiles: layers 1261.1 MiB, composite 64.0 MiB. Private memory added by the document: 1343 MiB (process 1345 MiB, working set 1350 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 5247 | - | - |
| full composite rebuild | 125.70 | 129.53 | 129.53 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 25.111 | 29.068 | 29.068 |
| render the view without the pyramid (100 %) | 3.845 | 5.447 | 5.447 |
| render the view as the canvas does (fit, level 1 of the pyramid), first frame (builds the level) | 40.4 | - | - |
| render the view as the canvas does (fit), later frames | 1.528 | 2.422 | 2.422 |
| render the view as the canvas does (100 %), later frames | 1.153 | 1.610 | 1.610 |
| render the view as the canvas does (fit), while panning 7 px per frame | 1.728 | 2.441 | 2.441 |
| render the view as the canvas does (5 %) | 0.540 | 12.463 | 12.463 |
| (view pyramid memory after these frames: 21.2 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0489 | 0.0843 | 0.1955 |
| undo that stroke (3.7 MiB of tiles) | 0.01 | 0.02 | 0.02 |
| redo that stroke | 0.01 | 0.01 | 0.01 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.4022 | 0.6230 | 1.5075 |
| save project (encode + atomic write) | 2406.7 | 2406.7 | 2406.7 |
| open project (read + verify + decode) | 1685.3 | 1685.3 | 1685.3 |
| recovery: snapshot on the UI thread | 0.0006 | 0.0051 | 0.0051 |
| recovery: encode (worker) | 2292.7 | 2292.7 | 2292.7 |

File 34.4 MiB. Process peak private memory 2704 MiB, peak working set 2676 MiB. MemoryBudget peak 2618 MiB.

## doc8192x10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 8192 x 8192, 10 layer(s)

Dense equivalent: 2560 MiB of layers + 256 MiB composite. Allocated tiles: layers 2136.3 MiB, composite 256.0 MiB. Private memory added by the document: 2417 MiB (process 2419 MiB, working set 2421 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 25983 | - | - |
| full composite rebuild | 220.79 | 228.09 | 228.09 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 102.937 | 106.325 | 106.325 |
| render the view without the pyramid (100 %) | 4.214 | 6.207 | 6.207 |
| render the view as the canvas does (fit, level 2 of the pyramid), first frame (builds the level) | 178.7 | - | - |
| render the view as the canvas does (fit), later frames | 2.184 | 3.585 | 3.585 |
| render the view as the canvas does (100 %), later frames | 1.239 | 1.363 | 1.363 |
| render the view as the canvas does (fit), while panning 7 px per frame | 2.123 | 2.610 | 2.610 |
| render the view as the canvas does (5 %) | 0.726 | 13.618 | 13.618 |
| (view pyramid memory after these frames: 85.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.0791 | 0.1344 | 7.8614 |
| undo that stroke (6.1 MiB of tiles) | 0.02 | 0.05 | 0.05 |
| redo that stroke | 0.02 | 0.02 | 0.02 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.8112 | 1.4151 | 2.8914 |
| save project (encode + atomic write) | 3815805.6 | 3815805.6 | 3815805.6 |
| open project (read + verify + decode) | 6287.9 | 6287.9 | 6287.9 |
| recovery: snapshot on the UI thread | 0.0009 | 0.7893 | 0.7893 |
| recovery: encode (worker) | 4581.7 | 4581.7 | 4581.7 |

File 42.9 MiB. Process peak private memory 4573 MiB, peak working set 2692 MiB. MemoryBudget peak 4482 MiB.

## sparse16384

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 16384 x 16384, 4 layer(s), sparse (a few long strokes per layer)

Dense equivalent: 4096 MiB of layers + 1024 MiB composite. Allocated tiles: layers 54.6 MiB, composite 54.6 MiB. Private memory added by the document: 117 MiB (process 119 MiB, working set 125 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 2562 | - | - |
| full composite rebuild | 12.80 | 13.16 | 13.16 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 269.186 | 350.329 | 350.329 |
| render the view without the pyramid (100 %) | 3.139 | 5.499 | 5.499 |
| render the view as the canvas does (fit, level 3 of the pyramid), first frame (builds the level) | 83.8 | - | - |
| render the view as the canvas does (fit), later frames | 2.296 | 2.712 | 2.712 |
| render the view as the canvas does (100 %), later frames | 1.129 | 1.589 | 1.589 |
| render the view as the canvas does (fit), while panning 7 px per frame | 2.419 | 3.146 | 3.146 |
| render the view as the canvas does (5 %) | 1.402 | 5.730 | 5.730 |
| (view pyramid memory after these frames: 41.6 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 18190 dabs | 0.1160 | 0.1947 | 0.6598 |
| undo that stroke (0.2 MiB of tiles) | 0.02 | 0.08 | 0.08 |
| redo that stroke | 0.02 | 0.02 | 0.02 |
| stroke input event, Textured Paint 40 px, 30517 dabs | 1.2735 | 2.0059 | 2.9834 |
| save project (encode + atomic write) | 370.1 | 370.1 | 370.1 |
| open project (read + verify + decode) | 100.0 | 100.0 | 100.0 |
| recovery: snapshot on the UI thread | 0.0003 | 0.0017 | 0.0017 |
| recovery: encode (worker) | 415.6 | 415.6 | 415.6 |

File 2.4 MiB. Process peak private memory 1153 MiB, peak working set 1157 MiB. MemoryBudget peak 1133 MiB.

## transforms20

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Multi-layer transform: 19 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 2560.8 | 2560.8 | 2560.8 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 4413.7 | 5022.5 | 5022.5 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 27.0 | 36.6 | 36.6 |
| preview after release, merged copy (bicubic) | 25.2 | 26.3 | 26.3 |
| begin with the merged copy (merge + cache, once per transform) | 698.8 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 11679.8 | 11679.8 | 11679.8 |
| apply: commit (exchange tile references + one group undo entry, 0 MiB kept by history) + composite | 435.1 | - | - |
| undo the transform (all layers) + composite | 173.5 | 173.5 | 173.5 |
| redo the transform (all layers) + composite | 165.2 | 165.2 | 165.2 |

Transform session memory 2771 MiB (sources and reductions); preview layers 1214 MiB of their own tiles; process private memory grew by 5291 MiB during the transform; peak private 7864 MiB.


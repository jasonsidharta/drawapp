# Milestone 9 vs milestone 10, interleaved runs (raw output)

Same machine and session; for each section milestone 9 (commit 7469a69, built from a worktree) then milestone 10, three rounds, 20 repetitions, one process per section. Document sections and brushes/long strokes: `ab/` run; transforms: the later `abt/` run (after the content-bounds change). Comparison tables: `m10-ab-comparison.md`.

## doc512, round 1, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 512 x 512, 3 layer(s)

Layer pixels 3 MiB (+ composite 1 MiB). Private memory added by the document: 4 MiB (process 6 MiB, working set 14 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 3 | - | - |
| full composite rebuild | 0.22 | 0.27 | 0.27 |
| render the view (fit, 1920 x 1080 window) | 0.314 | 3.657 | 3.657 |
| render the view (100 %, 1920 x 1080 window) | 0.327 | 1.730 | 1.730 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0047 | 0.0169 | 0.0733 |
| undo that stroke (0.5 MiB of tiles) | 0.03 | 0.05 | 0.05 |
| redo that stroke | 0.03 | 0.03 | 0.03 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.0641 | 0.0953 | 0.2517 |
| save project (encode + atomic write) | 16.4 | 16.4 | 16.4 |
| open project (read + verify + decode) | 18.7 | 18.7 | 18.7 |

File 0.1 MiB. Process peak private memory 29 MiB, peak working set 38 MiB.

## doc512, round 1, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 512 x 512, 3 layer(s)

Dense equivalent: 3 MiB of layers + 1 MiB composite. Allocated tiles: layers 3.0 MiB, composite 1.0 MiB. Private memory added by the document: 4 MiB (process 7 MiB, working set 15 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 5 | - | - |
| full composite rebuild | 0.30 | 0.34 | 0.34 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 0.711 | 3.928 | 3.928 |
| render the view without the pyramid (100 %) | 0.739 | 2.006 | 2.006 |
| render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) | 1.1 | - | - |
| render the view as the canvas does (fit), later frames | 0.335 | 0.380 | 0.380 |
| render the view as the canvas does (100 %), later frames | 0.350 | 0.501 | 0.501 |
| render the view as the canvas does (fit), while panning 7 px per frame | 0.340 | 0.522 | 0.522 |
| render the view as the canvas does (5 %) | 0.186 | 0.961 | 0.961 |
| (view pyramid memory after these frames: 0.3 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0053 | 0.0188 | 0.0835 |
| undo that stroke (0.5 MiB of tiles) | 0.00 | 0.00 | 0.00 |
| redo that stroke | 0.00 | 0.00 | 0.00 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.0662 | 0.1203 | 0.2759 |
| save project (encode + atomic write) | 13.1 | 13.1 | 13.1 |
| open project (read + verify + decode) | 20.4 | 20.4 | 20.4 |
| recovery: snapshot on the UI thread | 0.0002 | 0.0013 | 0.0013 |
| recovery: encode (worker) | 8.4 | 8.4 | 8.4 |

File 0.1 MiB. Process peak private memory 30 MiB, peak working set 39 MiB. MemoryBudget peak 7 MiB.

## doc512, round 2, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 512 x 512, 3 layer(s)

Layer pixels 3 MiB (+ composite 1 MiB). Private memory added by the document: 4 MiB (process 6 MiB, working set 14 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 5 | - | - |
| full composite rebuild | 0.32 | 0.44 | 0.44 |
| render the view (fit, 1920 x 1080 window) | 0.521 | 5.545 | 5.545 |
| render the view (100 %, 1920 x 1080 window) | 0.520 | 2.246 | 2.246 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0076 | 0.0258 | 0.1054 |
| undo that stroke (0.5 MiB of tiles) | 0.05 | 0.12 | 0.12 |
| redo that stroke | 0.05 | 0.05 | 0.05 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.0902 | 0.1463 | 0.3213 |
| save project (encode + atomic write) | 23.2 | 23.2 | 23.2 |
| open project (read + verify + decode) | 18.6 | 18.6 | 18.6 |

File 0.1 MiB. Process peak private memory 29 MiB, peak working set 38 MiB.

## doc512, round 2, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 512 x 512, 3 layer(s)

Dense equivalent: 3 MiB of layers + 1 MiB composite. Allocated tiles: layers 3.0 MiB, composite 1.0 MiB. Private memory added by the document: 4 MiB (process 7 MiB, working set 15 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 7 | - | - |
| full composite rebuild | 0.40 | 0.54 | 0.54 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 0.934 | 5.752 | 5.752 |
| render the view without the pyramid (100 %) | 0.891 | 2.547 | 2.547 |
| render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) | 1.1 | - | - |
| render the view as the canvas does (fit), later frames | 0.448 | 0.496 | 0.496 |
| render the view as the canvas does (100 %), later frames | 0.469 | 0.526 | 0.526 |
| render the view as the canvas does (fit), while panning 7 px per frame | 0.484 | 0.726 | 0.726 |
| render the view as the canvas does (5 %) | 0.249 | 1.165 | 1.165 |
| (view pyramid memory after these frames: 0.3 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0073 | 0.0247 | 0.0898 |
| undo that stroke (0.5 MiB of tiles) | 0.00 | 0.01 | 0.01 |
| redo that stroke | 0.00 | 0.00 | 0.00 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.0858 | 0.1284 | 0.2716 |
| save project (encode + atomic write) | 17.2 | 17.2 | 17.2 |
| open project (read + verify + decode) | 21.7 | 21.7 | 21.7 |
| recovery: snapshot on the UI thread | 0.0005 | 0.0027 | 0.0027 |
| recovery: encode (worker) | 9.1 | 9.1 | 9.1 |

File 0.1 MiB. Process peak private memory 31 MiB, peak working set 38 MiB. MemoryBudget peak 7 MiB.

## doc512, round 3, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 512 x 512, 3 layer(s)

Layer pixels 3 MiB (+ composite 1 MiB). Private memory added by the document: 4 MiB (process 6 MiB, working set 14 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 5 | - | - |
| full composite rebuild | 0.34 | 0.42 | 0.42 |
| render the view (fit, 1920 x 1080 window) | 0.446 | 4.577 | 4.577 |
| render the view (100 %, 1920 x 1080 window) | 0.548 | 1.961 | 1.961 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0060 | 0.0207 | 0.0880 |
| undo that stroke (0.5 MiB of tiles) | 0.04 | 0.08 | 0.08 |
| redo that stroke | 0.04 | 0.04 | 0.04 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.0760 | 0.1162 | 0.6872 |
| save project (encode + atomic write) | 18.5 | 18.5 | 18.5 |
| open project (read + verify + decode) | 13.7 | 13.7 | 13.7 |

File 0.1 MiB. Process peak private memory 29 MiB, peak working set 38 MiB.

## doc512, round 3, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 512 x 512, 3 layer(s)

Dense equivalent: 3 MiB of layers + 1 MiB composite. Allocated tiles: layers 3.0 MiB, composite 1.0 MiB. Private memory added by the document: 4 MiB (process 7 MiB, working set 15 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 7 | - | - |
| full composite rebuild | 0.41 | 0.52 | 0.52 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 0.788 | 5.114 | 5.114 |
| render the view without the pyramid (100 %) | 0.760 | 2.307 | 2.307 |
| render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) | 1.1 | - | - |
| render the view as the canvas does (fit), later frames | 0.394 | 0.714 | 0.714 |
| render the view as the canvas does (100 %), later frames | 0.399 | 0.531 | 0.531 |
| render the view as the canvas does (fit), while panning 7 px per frame | 0.409 | 0.622 | 0.622 |
| render the view as the canvas does (5 %) | 0.210 | 1.038 | 1.038 |
| (view pyramid memory after these frames: 0.3 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0060 | 0.0202 | 0.0805 |
| undo that stroke (0.5 MiB of tiles) | 0.00 | 0.00 | 0.00 |
| redo that stroke | 0.00 | 0.00 | 0.00 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.0712 | 0.1076 | 0.2261 |
| save project (encode + atomic write) | 14.5 | 14.5 | 14.5 |
| open project (read + verify + decode) | 21.6 | 21.6 | 21.6 |
| recovery: snapshot on the UI thread | 0.0002 | 0.0013 | 0.0013 |
| recovery: encode (worker) | 8.5 | 8.5 | 8.5 |

File 0.1 MiB. Process peak private memory 31 MiB, peak working set 39 MiB. MemoryBudget peak 7 MiB.

## doc2048, round 1, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 2048 x 1536, 10 layer(s)

Layer pixels 120 MiB (+ composite 12 MiB). Private memory added by the document: 132 MiB (process 135 MiB, working set 142 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 89 | - | - |
| full composite rebuild | 12.40 | 12.57 | 12.57 |
| render the view (fit, 1920 x 1080 window) | 1.365 | 6.260 | 6.260 |
| render the view (100 %, 1920 x 1080 window) | 1.080 | 2.796 | 2.796 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 1806 dabs | 0.0183 | 0.0373 | 0.1211 |
| undo that stroke (1.6 MiB of tiles) | 0.14 | 0.42 | 0.42 |
| redo that stroke | 0.14 | 0.18 | 0.18 |
| stroke input event, Textured Paint 40 px, 3028 dabs | 0.1616 | 0.2656 | 0.3750 |
| save project (encode + atomic write) | 805.7 | 805.7 | 805.7 |
| open project (read + verify + decode) | 250.6 | 250.6 | 250.6 |

File 4.8 MiB. Process peak private memory 310 MiB, peak working set 316 MiB.

## doc2048, round 1, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 2048 x 1536, 10 layer(s)

Dense equivalent: 120 MiB of layers + 12 MiB composite. Allocated tiles: layers 119.9 MiB, composite 12.0 MiB. Private memory added by the document: 135 MiB (process 137 MiB, working set 145 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 228 | - | - |
| full composite rebuild | 14.78 | 18.00 | 18.00 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 5.366 | 11.919 | 11.919 |
| render the view without the pyramid (100 %) | 3.674 | 5.188 | 5.188 |
| render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) | 6.2 | - | - |
| render the view as the canvas does (fit), later frames | 1.594 | 2.216 | 2.216 |
| render the view as the canvas does (100 %), later frames | 1.022 | 1.424 | 1.424 |
| render the view as the canvas does (fit), while panning 7 px per frame | 1.610 | 2.452 | 2.452 |
| render the view as the canvas does (5 %) | 0.213 | 8.702 | 8.702 |
| (view pyramid memory after these frames: 4.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 1806 dabs | 0.0184 | 0.0347 | 0.0915 |
| undo that stroke (1.6 MiB of tiles) | 0.00 | 0.01 | 0.01 |
| redo that stroke | 0.00 | 0.00 | 0.00 |
| stroke input event, Textured Paint 40 px, 3028 dabs | 0.1643 | 0.2818 | 0.8732 |
| save project (encode + atomic write) | 231.4 | 231.4 | 231.4 |
| open project (read + verify + decode) | 146.2 | 146.2 | 146.2 |
| recovery: snapshot on the UI thread | 0.0003 | 0.0031 | 0.0031 |
| recovery: encode (worker) | 243.9 | 243.9 | 243.9 |

File 5.8 MiB. Process peak private memory 290 MiB, peak working set 295 MiB. MemoryBudget peak 258 MiB.

## doc2048, round 2, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 2048 x 1536, 10 layer(s)

Layer pixels 120 MiB (+ composite 12 MiB). Private memory added by the document: 132 MiB (process 135 MiB, working set 142 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 113 | - | - |
| full composite rebuild | 14.13 | 15.10 | 15.10 |
| render the view (fit, 1920 x 1080 window) | 1.901 | 7.275 | 7.275 |
| render the view (100 %, 1920 x 1080 window) | 1.145 | 2.966 | 2.966 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 1806 dabs | 0.0220 | 0.0430 | 0.1376 |
| undo that stroke (1.6 MiB of tiles) | 0.21 | 0.40 | 0.40 |
| redo that stroke | 0.17 | 0.21 | 0.21 |
| stroke input event, Textured Paint 40 px, 3028 dabs | 0.1805 | 0.3088 | 0.9082 |
| save project (encode + atomic write) | 849.3 | 849.3 | 849.3 |
| open project (read + verify + decode) | 247.9 | 247.9 | 247.9 |

File 4.8 MiB. Process peak private memory 310 MiB, peak working set 316 MiB.

## doc2048, round 2, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 2048 x 1536, 10 layer(s)

Dense equivalent: 120 MiB of layers + 12 MiB composite. Allocated tiles: layers 119.9 MiB, composite 12.0 MiB. Private memory added by the document: 135 MiB (process 137 MiB, working set 145 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 248 | - | - |
| full composite rebuild | 16.25 | 16.97 | 16.97 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 6.206 | 11.070 | 11.070 |
| render the view without the pyramid (100 %) | 3.925 | 5.664 | 5.664 |
| render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) | 7.8 | - | - |
| render the view as the canvas does (fit), later frames | 1.836 | 2.189 | 2.189 |
| render the view as the canvas does (100 %), later frames | 1.250 | 1.425 | 1.425 |
| render the view as the canvas does (fit), while panning 7 px per frame | 1.732 | 1.962 | 1.962 |
| render the view as the canvas does (5 %) | 0.252 | 9.446 | 9.446 |
| (view pyramid memory after these frames: 4.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 1806 dabs | 0.0219 | 0.0409 | 0.1025 |
| undo that stroke (1.6 MiB of tiles) | 0.00 | 0.01 | 0.01 |
| redo that stroke | 0.00 | 0.01 | 0.01 |
| stroke input event, Textured Paint 40 px, 3028 dabs | 0.1823 | 0.3102 | 0.8388 |
| save project (encode + atomic write) | 242.7 | 242.7 | 242.7 |
| open project (read + verify + decode) | 186.9 | 186.9 | 186.9 |
| recovery: snapshot on the UI thread | 0.0005 | 0.0031 | 0.0031 |
| recovery: encode (worker) | 265.6 | 265.6 | 265.6 |

File 5.8 MiB. Process peak private memory 290 MiB, peak working set 295 MiB. MemoryBudget peak 258 MiB.

## doc2048, round 3, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 2048 x 1536, 10 layer(s)

Layer pixels 120 MiB (+ composite 12 MiB). Private memory added by the document: 132 MiB (process 135 MiB, working set 142 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 107 | - | - |
| full composite rebuild | 12.85 | 13.10 | 13.10 |
| render the view (fit, 1920 x 1080 window) | 1.536 | 6.736 | 6.736 |
| render the view (100 %, 1920 x 1080 window) | 1.033 | 2.437 | 2.437 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 1806 dabs | 0.0196 | 0.0404 | 0.0945 |
| undo that stroke (1.6 MiB of tiles) | 0.17 | 0.44 | 0.44 |
| redo that stroke | 0.19 | 0.19 | 0.19 |
| stroke input event, Textured Paint 40 px, 3028 dabs | 0.1632 | 0.2774 | 0.6903 |
| save project (encode + atomic write) | 823.5 | 823.5 | 823.5 |
| open project (read + verify + decode) | 234.1 | 234.1 | 234.1 |

File 4.8 MiB. Process peak private memory 310 MiB, peak working set 316 MiB.

## doc2048, round 3, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 2048 x 1536, 10 layer(s)

Dense equivalent: 120 MiB of layers + 12 MiB composite. Allocated tiles: layers 119.9 MiB, composite 12.0 MiB. Private memory added by the document: 135 MiB (process 137 MiB, working set 145 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 240 | - | - |
| full composite rebuild | 14.95 | 15.54 | 15.54 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 5.634 | 11.089 | 11.089 |
| render the view without the pyramid (100 %) | 3.821 | 5.506 | 5.506 |
| render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) | 6.6 | - | - |
| render the view as the canvas does (fit), later frames | 1.744 | 2.310 | 2.310 |
| render the view as the canvas does (100 %), later frames | 1.133 | 1.342 | 1.342 |
| render the view as the canvas does (fit), while panning 7 px per frame | 1.767 | 1.956 | 1.956 |
| render the view as the canvas does (5 %) | 0.195 | 8.284 | 8.284 |
| (view pyramid memory after these frames: 4.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 1806 dabs | 0.0194 | 0.0357 | 0.0882 |
| undo that stroke (1.6 MiB of tiles) | 0.00 | 0.01 | 0.01 |
| redo that stroke | 0.00 | 0.00 | 0.00 |
| stroke input event, Textured Paint 40 px, 3028 dabs | 0.1713 | 0.2919 | 0.6575 |
| save project (encode + atomic write) | 239.1 | 239.1 | 239.1 |
| open project (read + verify + decode) | 186.4 | 186.4 | 186.4 |
| recovery: snapshot on the UI thread | 0.0004 | 0.0032 | 0.0032 |
| recovery: encode (worker) | 234.0 | 234.0 | 234.0 |

File 5.8 MiB. Process peak private memory 290 MiB, peak working set 296 MiB. MemoryBudget peak 258 MiB.

## doc4096x8, round 1, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 4096 x 4096, 20 layer(s)

Refused by design: 20 layers of this size exceed the 512 MiB document limit (at most 8 layers). Measured with 8 layers instead.

Layer pixels 512 MiB (+ composite 64 MiB). Private memory added by the document: 577 MiB (process 580 MiB, working set 586 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 469 | - | - |
| full composite rebuild | 60.32 | 64.22 | 64.22 |
| render the view (fit, 1920 x 1080 window) | 1.782 | 5.964 | 5.964 |
| render the view (100 %, 1920 x 1080 window) | 1.258 | 3.006 | 3.006 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0356 | 0.0665 | 0.1524 |
| undo that stroke (3.7 MiB of tiles) | 0.96 | 1.12 | 1.12 |
| redo that stroke | 0.95 | 1.07 | 1.07 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.3789 | 0.5802 | 1.1091 |
| save project (encode + atomic write) | 2682.0 | 2682.0 | 2682.0 |
| open project (read + verify + decode) | 989.1 | 989.1 | 989.1 |

File 10.3 MiB. Process peak private memory 1267 MiB, peak working set 1268 MiB.

## doc4096x8, round 1, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 4096 x 4096, 8 layer(s)

Dense equivalent: 512 MiB of layers + 64 MiB composite. Allocated tiles: layers 505.1 MiB, composite 64.0 MiB. Private memory added by the document: 578 MiB (process 580 MiB, working set 587 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 2002 | - | - |
| full composite rebuild | 53.35 | 54.55 | 54.55 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 24.489 | 27.142 | 27.142 |
| render the view without the pyramid (100 %) | 3.773 | 5.669 | 5.669 |
| render the view as the canvas does (fit, level 1 of the pyramid), first frame (builds the level) | 41.9 | - | - |
| render the view as the canvas does (fit), later frames | 1.371 | 1.989 | 1.989 |
| render the view as the canvas does (100 %), later frames | 1.118 | 1.632 | 1.632 |
| render the view as the canvas does (fit), while panning 7 px per frame | 1.511 | 2.074 | 2.074 |
| render the view as the canvas does (5 %) | 0.356 | 12.122 | 12.122 |
| (view pyramid memory after these frames: 21.2 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0362 | 0.0621 | 0.0922 |
| undo that stroke (3.7 MiB of tiles) | 0.01 | 0.02 | 0.02 |
| redo that stroke | 0.01 | 0.01 | 0.01 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.3756 | 0.5898 | 1.3117 |
| save project (encode + atomic write) | 794.2 | 794.2 | 794.2 |
| open project (read + verify + decode) | 628.0 | 628.0 | 628.0 |
| recovery: snapshot on the UI thread | 0.0003 | 0.0023 | 0.0023 |
| recovery: encode (worker) | 718.6 | 718.6 | 718.6 |

File 12.8 MiB. Process peak private memory 1132 MiB, peak working set 1135 MiB. MemoryBudget peak 1086 MiB.

## doc4096x8, round 2, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 4096 x 4096, 20 layer(s)

Refused by design: 20 layers of this size exceed the 512 MiB document limit (at most 8 layers). Measured with 8 layers instead.

Layer pixels 512 MiB (+ composite 64 MiB). Private memory added by the document: 577 MiB (process 580 MiB, working set 586 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 607 | - | - |
| full composite rebuild | 64.71 | 65.77 | 65.77 |
| render the view (fit, 1920 x 1080 window) | 1.637 | 7.449 | 7.449 |
| render the view (100 %, 1920 x 1080 window) | 1.177 | 2.773 | 2.773 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0401 | 0.0757 | 0.1305 |
| undo that stroke (3.7 MiB of tiles) | 0.99 | 1.20 | 1.20 |
| redo that stroke | 0.96 | 1.07 | 1.07 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.3981 | 0.6523 | 1.3762 |
| save project (encode + atomic write) | 2988.9 | 2988.9 | 2988.9 |
| open project (read + verify + decode) | 967.3 | 967.3 | 967.3 |

File 10.3 MiB. Process peak private memory 1267 MiB, peak working set 1268 MiB.

## doc4096x8, round 2, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 4096 x 4096, 8 layer(s)

Dense equivalent: 512 MiB of layers + 64 MiB composite. Allocated tiles: layers 505.1 MiB, composite 64.0 MiB. Private memory added by the document: 578 MiB (process 580 MiB, working set 587 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 1980 | - | - |
| full composite rebuild | 56.63 | 60.81 | 60.81 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 25.015 | 28.739 | 28.739 |
| render the view without the pyramid (100 %) | 3.932 | 6.155 | 6.155 |
| render the view as the canvas does (fit, level 1 of the pyramid), first frame (builds the level) | 41.8 | - | - |
| render the view as the canvas does (fit), later frames | 1.490 | 2.067 | 2.067 |
| render the view as the canvas does (100 %), later frames | 1.106 | 1.259 | 1.259 |
| render the view as the canvas does (fit), while panning 7 px per frame | 1.434 | 2.245 | 2.245 |
| render the view as the canvas does (5 %) | 0.376 | 12.386 | 12.386 |
| (view pyramid memory after these frames: 21.2 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0400 | 0.0666 | 0.0967 |
| undo that stroke (3.7 MiB of tiles) | 0.01 | 0.02 | 0.02 |
| redo that stroke | 0.01 | 0.01 | 0.01 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.3861 | 0.5916 | 1.4025 |
| save project (encode + atomic write) | 882.9 | 882.9 | 882.9 |
| open project (read + verify + decode) | 677.0 | 677.0 | 677.0 |
| recovery: snapshot on the UI thread | 0.0004 | 0.0027 | 0.0027 |
| recovery: encode (worker) | 918.5 | 918.5 | 918.5 |

File 12.8 MiB. Process peak private memory 1131 MiB, peak working set 1134 MiB. MemoryBudget peak 1086 MiB.

## doc4096x8, round 3, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 4096 x 4096, 20 layer(s)

Refused by design: 20 layers of this size exceed the 512 MiB document limit (at most 8 layers). Measured with 8 layers instead.

Layer pixels 512 MiB (+ composite 64 MiB). Private memory added by the document: 577 MiB (process 580 MiB, working set 586 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 542 | - | - |
| full composite rebuild | 62.68 | 66.74 | 66.74 |
| render the view (fit, 1920 x 1080 window) | 1.573 | 7.104 | 7.104 |
| render the view (100 %, 1920 x 1080 window) | 1.171 | 2.736 | 2.736 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0370 | 0.0699 | 0.1579 |
| undo that stroke (3.7 MiB of tiles) | 0.77 | 1.11 | 1.11 |
| redo that stroke | 0.68 | 1.17 | 1.17 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.3825 | 0.5831 | 1.0795 |
| save project (encode + atomic write) | 2750.6 | 2750.6 | 2750.6 |
| open project (read + verify + decode) | 969.9 | 969.9 | 969.9 |

File 10.3 MiB. Process peak private memory 1267 MiB, peak working set 1268 MiB.

## doc4096x8, round 3, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 4096 x 4096, 8 layer(s)

Dense equivalent: 512 MiB of layers + 64 MiB composite. Allocated tiles: layers 505.1 MiB, composite 64.0 MiB. Private memory added by the document: 578 MiB (process 580 MiB, working set 587 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 1956 | - | - |
| full composite rebuild | 52.71 | 53.40 | 53.40 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 23.938 | 26.454 | 26.454 |
| render the view without the pyramid (100 %) | 3.657 | 5.225 | 5.225 |
| render the view as the canvas does (fit, level 1 of the pyramid), first frame (builds the level) | 39.6 | - | - |
| render the view as the canvas does (fit), later frames | 1.658 | 3.612 | 3.612 |
| render the view as the canvas does (100 %), later frames | 1.123 | 1.278 | 1.278 |
| render the view as the canvas does (fit), while panning 7 px per frame | 1.429 | 2.003 | 2.003 |
| render the view as the canvas does (5 %) | 0.286 | 11.144 | 11.144 |
| (view pyramid memory after these frames: 21.2 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0358 | 0.0702 | 0.1539 |
| undo that stroke (3.7 MiB of tiles) | 0.01 | 0.02 | 0.02 |
| redo that stroke | 0.01 | 0.01 | 0.01 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.3891 | 0.5842 | 0.9481 |
| save project (encode + atomic write) | 712.0 | 712.0 | 712.0 |
| open project (read + verify + decode) | 630.0 | 630.0 | 630.0 |
| recovery: snapshot on the UI thread | 0.0002 | 0.0018 | 0.0018 |
| recovery: encode (worker) | 678.8 | 678.8 | 678.8 |

File 12.8 MiB. Process peak private memory 1131 MiB, peak working set 1134 MiB. MemoryBudget peak 1086 MiB.

## doc8192, round 1, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 8192 x 8192, 2 layer(s)

Layer pixels 512 MiB (+ composite 256 MiB). Private memory added by the document: 770 MiB (process 772 MiB, working set 778 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 420 | - | - |
| full composite rebuild | 71.95 | 72.45 | 72.45 |
| render the view (fit, 1920 x 1080 window) | 2.367 | 6.408 | 6.408 |
| render the view (100 %, 1920 x 1080 window) | 1.201 | 3.055 | 3.055 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.0633 | 0.1109 | 0.1862 |
| undo that stroke (7.5 MiB of tiles) | 2.39 | 2.42 | 2.42 |
| redo that stroke | 2.33 | 2.47 | 2.47 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.7338 | 1.1620 | 1.9191 |
| save project (encode + atomic write) | 2241.1 | 2241.1 | 2241.1 |
| open project (read + verify + decode) | 877.7 | 877.7 | 877.7 |

File 4.6 MiB. Process peak private memory 1833 MiB, peak working set 1835 MiB.

## doc8192, round 1, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 8192 x 8192, 2 layer(s)

Dense equivalent: 512 MiB of layers + 256 MiB composite. Allocated tiles: layers 464.3 MiB, composite 256.0 MiB. Private memory added by the document: 726 MiB (process 729 MiB, working set 735 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 3379 | - | - |
| full composite rebuild | 102.17 | 115.60 | 115.60 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 103.914 | 145.378 | 145.378 |
| render the view without the pyramid (100 %) | 3.772 | 4.825 | 4.825 |
| render the view as the canvas does (fit, level 2 of the pyramid), first frame (builds the level) | 171.3 | - | - |
| render the view as the canvas does (fit), later frames | 2.060 | 2.840 | 2.840 |
| render the view as the canvas does (100 %), later frames | 1.072 | 1.184 | 1.184 |
| render the view as the canvas does (fit), while panning 7 px per frame | 2.045 | 3.004 | 3.004 |
| render the view as the canvas does (5 %) | 0.573 | 11.599 | 11.599 |
| (view pyramid memory after these frames: 85.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.0653 | 0.1075 | 0.1830 |
| undo that stroke (6.1 MiB of tiles) | 0.02 | 0.03 | 0.03 |
| redo that stroke | 0.02 | 0.02 | 0.02 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.7371 | 1.2836 | 3.7537 |
| save project (encode + atomic write) | 856.8 | 856.8 | 856.8 |
| open project (read + verify + decode) | 615.8 | 615.8 | 615.8 |
| recovery: snapshot on the UI thread | 0.0002 | 0.0026 | 0.0026 |
| recovery: encode (worker) | 863.5 | 863.5 | 863.5 |

File 5.6 MiB. Process peak private memory 1227 MiB, peak working set 1230 MiB. MemoryBudget peak 1183 MiB.

## doc8192, round 2, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 8192 x 8192, 2 layer(s)

Layer pixels 512 MiB (+ composite 256 MiB). Private memory added by the document: 770 MiB (process 772 MiB, working set 778 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 520 | - | - |
| full composite rebuild | 73.87 | 74.58 | 74.58 |
| render the view (fit, 1920 x 1080 window) | 2.417 | 7.474 | 7.474 |
| render the view (100 %, 1920 x 1080 window) | 1.276 | 2.956 | 2.956 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.0667 | 0.1212 | 0.1916 |
| undo that stroke (7.5 MiB of tiles) | 2.49 | 3.32 | 3.32 |
| redo that stroke | 2.48 | 2.84 | 2.84 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.7152 | 1.1120 | 2.1897 |
| save project (encode + atomic write) | 2097.7 | 2097.7 | 2097.7 |
| open project (read + verify + decode) | 860.8 | 860.8 | 860.8 |

File 4.6 MiB. Process peak private memory 1833 MiB, peak working set 1835 MiB.

## doc8192, round 2, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 8192 x 8192, 2 layer(s)

Dense equivalent: 512 MiB of layers + 256 MiB composite. Allocated tiles: layers 464.3 MiB, composite 256.0 MiB. Private memory added by the document: 726 MiB (process 729 MiB, working set 735 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 3163 | - | - |
| full composite rebuild | 77.70 | 82.85 | 82.85 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 100.997 | 114.992 | 114.992 |
| render the view without the pyramid (100 %) | 4.306 | 5.722 | 5.722 |
| render the view as the canvas does (fit, level 2 of the pyramid), first frame (builds the level) | 169.9 | - | - |
| render the view as the canvas does (fit), later frames | 1.961 | 2.630 | 2.630 |
| render the view as the canvas does (100 %), later frames | 1.360 | 1.636 | 1.636 |
| render the view as the canvas does (fit), while panning 7 px per frame | 2.023 | 2.964 | 2.964 |
| render the view as the canvas does (5 %) | 0.663 | 12.119 | 12.119 |
| (view pyramid memory after these frames: 85.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.0627 | 0.1155 | 0.2401 |
| undo that stroke (6.1 MiB of tiles) | 0.02 | 0.03 | 0.03 |
| redo that stroke | 0.02 | 0.02 | 0.02 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.7330 | 1.2297 | 2.3333 |
| save project (encode + atomic write) | 1031.0 | 1031.0 | 1031.0 |
| open project (read + verify + decode) | 721.5 | 721.5 | 721.5 |
| recovery: snapshot on the UI thread | 0.0002 | 0.0023 | 0.0023 |
| recovery: encode (worker) | 1254.2 | 1254.2 | 1254.2 |

File 5.6 MiB. Process peak private memory 1226 MiB, peak working set 1229 MiB. MemoryBudget peak 1183 MiB.

## doc8192, round 3, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 8192 x 8192, 2 layer(s)

Layer pixels 512 MiB (+ composite 256 MiB). Private memory added by the document: 770 MiB (process 772 MiB, working set 778 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 417 | - | - |
| full composite rebuild | 70.93 | 71.31 | 71.31 |
| render the view (fit, 1920 x 1080 window) | 2.204 | 7.462 | 7.462 |
| render the view (100 %, 1920 x 1080 window) | 1.202 | 3.006 | 3.006 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.0629 | 0.1097 | 0.1592 |
| undo that stroke (7.5 MiB of tiles) | 1.89 | 2.84 | 2.84 |
| redo that stroke | 1.85 | 1.94 | 1.94 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.7289 | 1.1209 | 2.2118 |
| save project (encode + atomic write) | 2132.2 | 2132.2 | 2132.2 |
| open project (read + verify + decode) | 854.0 | 854.0 | 854.0 |

File 4.6 MiB. Process peak private memory 1833 MiB, peak working set 1835 MiB.

## doc8192, round 3, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 8192 x 8192, 2 layer(s)

Dense equivalent: 512 MiB of layers + 256 MiB composite. Allocated tiles: layers 464.3 MiB, composite 256.0 MiB. Private memory added by the document: 726 MiB (process 729 MiB, working set 735 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 3193 | - | - |
| full composite rebuild | 69.97 | 71.79 | 71.79 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 94.351 | 103.235 | 103.235 |
| render the view without the pyramid (100 %) | 3.856 | 5.681 | 5.681 |
| render the view as the canvas does (fit, level 2 of the pyramid), first frame (builds the level) | 161.5 | - | - |
| render the view as the canvas does (fit), later frames | 1.896 | 2.830 | 2.830 |
| render the view as the canvas does (100 %), later frames | 1.033 | 1.425 | 1.425 |
| render the view as the canvas does (fit), while panning 7 px per frame | 2.037 | 2.623 | 2.623 |
| render the view as the canvas does (5 %) | 0.699 | 17.345 | 17.345 |
| (view pyramid memory after these frames: 85.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.0643 | 0.1025 | 0.3430 |
| undo that stroke (6.1 MiB of tiles) | 0.01 | 0.03 | 0.03 |
| redo that stroke | 0.01 | 0.01 | 0.01 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.7296 | 1.1240 | 2.1351 |
| save project (encode + atomic write) | 826.2 | 826.2 | 826.2 |
| open project (read + verify + decode) | 583.4 | 583.4 | 583.4 |
| recovery: snapshot on the UI thread | 0.0002 | 0.0016 | 0.0016 |
| recovery: encode (worker) | 819.8 | 819.8 | 819.8 |

File 5.6 MiB. Process peak private memory 1226 MiB, peak working set 1229 MiB. MemoryBudget peak 1183 MiB.

## brushes, round 1, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers)

Engine + undo capture + composite refresh per input event (4 ms apart, simulated pressure). Dab throughput = dabs / total processing time of the stroke.

| Brush | median ms | p95 ms | max ms | dabs | dabs per s | engine buffers KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Solid Round 12 px (milestone 6 default) | 0.0172 | 0.0646 | 0.204 | 3611 | 147022 | 792 | 1.5 |
| Solid Round 128 px | 0.0005 | 0.4481 | 0.692 | 339 | 4172 | 1584 | 2.7 |
| Clean Ink (preset) | 0.0372 | 0.0989 | 0.179 | 8047 | 183753 | 784 | 1.5 |
| Sketch Pencil (preset, textured) | 0.0693 | 0.1090 | 0.364 | 8060 | 118083 | 768 | 1.4 |
| Textured Paint 40 px (stroke texture) | 0.1888 | 0.3081 | 0.650 | 3028 | 16006 | 1184 | 1.8 |
| Textured Paint 40 px, texture each dab + rotated grain | 0.2425 | 0.3948 | 0.868 | 3028 | 12550 | 1184 | 1.8 |
| Dense textured 120 px, build-up, each dab | 0.5867 | 0.9758 | 4.187 | 808 | 1528 | 2192 | 2.8 |
| Soft Airbrush 90 px (preset) | 0.2413 | 0.3732 | 0.743 | 896 | 4216 | 1568 | 2.6 |
| Smudge 36 px (preset) | 0.1508 | 0.2728 | 0.606 | 2191 | 15025 | 25 | 1.7 |
| Smudge 36 px, dulling + colour rate | 0.1580 | 0.2999 | 0.626 | 2191 | 13842 | 25 | 1.7 |
| Wet paint 40 px | 0.0671 | 0.1279 | 0.427 | 1084 | 16657 | 30 | 1.7 |
| Wet paint 120 px | 0.0004 | 0.8838 | 1.534 | 362 | 2301 | 240 | 2.6 |
| Spray 80 px, 60 particles | 0.0003 | 0.0936 | 0.173 | 271 | 13881 | 1088 | 2.1 |
| Hatching 80 px | 0.0328 | 0.1727 | 0.327 | 542 | 11254 | 1224 | 2.2 |
| Colour dynamics 30 px (hue jitter: per-pixel colour) | 0.0229 | 0.0490 | 0.139 | 1445 | 62549 | 2688 | 1.7 |
| Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors) | 0.0201 | 0.0615 | 0.116 | 2889 | 116879 | 856 | 1.5 |
| Count 4 + scatter 30 px | 0.0626 | 0.1099 | 0.301 | 5780 | 94422 | 1088 | 2.0 |
| End taper 200 px, 24 px | 0.0186 | 0.0366 | 1.052 | 1952 | 105371 | 880 | 1.6 |

## brushes, round 1, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers)

Engine + undo capture + composite refresh per input event (4 ms apart, simulated pressure). Dab throughput = dabs / total processing time of the stroke.

| Brush | median ms | p95 ms | max ms | dabs | dabs per s | engine buffers KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Solid Round 12 px (milestone 6 default) | 0.0275 | 0.0838 | 1.919 | 3611 | 100825 | 792 | 1.5 |
| Solid Round 128 px | 0.0003 | 0.4163 | 0.531 | 339 | 4599 | 1584 | 2.7 |
| Clean Ink (preset) | 0.0367 | 0.0929 | 0.157 | 8047 | 192055 | 784 | 1.5 |
| Sketch Pencil (preset, textured) | 0.0642 | 0.1065 | 0.467 | 8060 | 125627 | 768 | 1.4 |
| Textured Paint 40 px (stroke texture) | 0.1840 | 0.2984 | 0.529 | 3028 | 16247 | 1184 | 1.8 |
| Textured Paint 40 px, texture each dab + rotated grain | 0.2492 | 0.4404 | 1.087 | 3028 | 12413 | 1184 | 1.8 |
| Dense textured 120 px, build-up, each dab | 0.5551 | 0.8878 | 1.920 | 808 | 1632 | 2192 | 2.8 |
| Soft Airbrush 90 px (preset) | 0.2252 | 0.3330 | 0.741 | 896 | 4610 | 1568 | 2.6 |
| Smudge 36 px (preset) | 0.1286 | 0.2341 | 4.229 | 2191 | 16391 | 25 | 1.7 |
| Smudge 36 px, dulling + colour rate | 0.1406 | 0.2560 | 0.834 | 2191 | 15506 | 25 | 1.7 |
| Wet paint 40 px | 0.0590 | 0.1163 | 0.512 | 1084 | 18963 | 30 | 1.7 |
| Wet paint 120 px | 0.0003 | 0.7921 | 1.731 | 362 | 2477 | 240 | 2.6 |
| Spray 80 px, 60 particles | 0.0003 | 0.0989 | 0.510 | 271 | 13157 | 1088 | 2.1 |
| Hatching 80 px | 0.0353 | 0.1786 | 1.051 | 542 | 10687 | 1224 | 2.2 |
| Colour dynamics 30 px (hue jitter: per-pixel colour) | 0.0267 | 0.0515 | 0.391 | 1445 | 53330 | 2688 | 1.7 |
| Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors) | 0.0214 | 0.0590 | 3.377 | 2889 | 105719 | 856 | 1.5 |
| Count 4 + scatter 30 px | 0.0673 | 0.1265 | 0.305 | 5780 | 87396 | 1088 | 2.0 |
| End taper 200 px, 24 px | 0.0199 | 0.0645 | 1.302 | 1952 | 88374 | 880 | 1.6 |

## brushes, round 2, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers)

Engine + undo capture + composite refresh per input event (4 ms apart, simulated pressure). Dab throughput = dabs / total processing time of the stroke.

| Brush | median ms | p95 ms | max ms | dabs | dabs per s | engine buffers KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Solid Round 12 px (milestone 6 default) | 0.0333 | 0.1225 | 0.297 | 3611 | 77678 | 792 | 1.5 |
| Solid Round 128 px | 0.0008 | 0.7505 | 1.567 | 339 | 2827 | 1584 | 2.7 |
| Clean Ink (preset) | 0.0528 | 0.1460 | 0.347 | 8047 | 134158 | 784 | 1.5 |
| Sketch Pencil (preset, textured) | 0.0811 | 0.1371 | 2.946 | 8060 | 100627 | 768 | 1.4 |
| Textured Paint 40 px (stroke texture) | 0.1963 | 0.3283 | 0.858 | 3028 | 15210 | 1184 | 1.8 |
| Textured Paint 40 px, texture each dab + rotated grain | 0.2147 | 0.3847 | 0.923 | 3028 | 13555 | 1184 | 1.8 |
| Dense textured 120 px, build-up, each dab | 0.5083 | 0.8067 | 4.154 | 808 | 1762 | 2192 | 2.8 |
| Soft Airbrush 90 px (preset) | 0.1969 | 0.3130 | 1.106 | 896 | 5020 | 1568 | 2.6 |
| Smudge 36 px (preset) | 0.1200 | 0.2133 | 0.478 | 2191 | 18871 | 25 | 1.7 |
| Smudge 36 px, dulling + colour rate | 0.1343 | 0.2444 | 0.638 | 2191 | 16800 | 25 | 1.7 |
| Wet paint 40 px | 0.0551 | 0.1083 | 0.224 | 1084 | 20188 | 30 | 1.7 |
| Wet paint 120 px | 0.0003 | 0.7751 | 3.991 | 362 | 2504 | 240 | 2.6 |
| Spray 80 px, 60 particles | 0.0002 | 0.0878 | 0.217 | 271 | 14746 | 1088 | 2.1 |
| Hatching 80 px | 0.0313 | 0.1620 | 0.559 | 542 | 12098 | 1224 | 2.2 |
| Colour dynamics 30 px (hue jitter: per-pixel colour) | 0.0225 | 0.0483 | 0.112 | 1445 | 63850 | 2688 | 1.7 |
| Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors) | 0.0195 | 0.0575 | 0.129 | 2889 | 122269 | 856 | 1.5 |
| Count 4 + scatter 30 px | 0.0635 | 0.1030 | 0.225 | 5780 | 95945 | 1088 | 2.0 |
| End taper 200 px, 24 px | 0.0203 | 0.0484 | 1.304 | 1952 | 98647 | 880 | 1.6 |

## brushes, round 2, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers)

Engine + undo capture + composite refresh per input event (4 ms apart, simulated pressure). Dab throughput = dabs / total processing time of the stroke.

| Brush | median ms | p95 ms | max ms | dabs | dabs per s | engine buffers KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Solid Round 12 px (milestone 6 default) | 0.0170 | 0.0600 | 0.112 | 3611 | 156137 | 792 | 1.5 |
| Solid Round 128 px | 0.0003 | 0.3850 | 0.532 | 339 | 4704 | 1584 | 2.7 |
| Clean Ink (preset) | 0.0364 | 0.0939 | 0.398 | 8047 | 190385 | 784 | 1.5 |
| Sketch Pencil (preset, textured) | 0.0618 | 0.1016 | 0.382 | 8060 | 130513 | 768 | 1.4 |
| Textured Paint 40 px (stroke texture) | 0.1698 | 0.2768 | 0.690 | 3028 | 17514 | 1184 | 1.8 |
| Textured Paint 40 px, texture each dab + rotated grain | 0.2043 | 0.3529 | 0.684 | 3028 | 14553 | 1184 | 1.8 |
| Dense textured 120 px, build-up, each dab | 0.5366 | 0.8366 | 4.510 | 808 | 1699 | 2192 | 2.8 |
| Soft Airbrush 90 px (preset) | 0.2102 | 0.3200 | 1.074 | 896 | 4854 | 1568 | 2.6 |
| Smudge 36 px (preset) | 0.1220 | 0.2178 | 0.697 | 2191 | 17803 | 25 | 1.7 |
| Smudge 36 px, dulling + colour rate | 0.1358 | 0.2379 | 0.666 | 2191 | 16175 | 25 | 1.7 |
| Wet paint 40 px | 0.0557 | 0.1107 | 0.591 | 1084 | 19805 | 30 | 1.7 |
| Wet paint 120 px | 0.0003 | 0.7499 | 1.531 | 362 | 2663 | 240 | 2.6 |
| Spray 80 px, 60 particles | 0.0002 | 0.0968 | 0.187 | 271 | 13793 | 1088 | 2.1 |
| Hatching 80 px | 0.0353 | 0.1788 | 0.422 | 542 | 10782 | 1224 | 2.2 |
| Colour dynamics 30 px (hue jitter: per-pixel colour) | 0.0240 | 0.0458 | 0.403 | 1445 | 60711 | 2688 | 1.7 |
| Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors) | 0.0218 | 0.0574 | 0.173 | 2889 | 114854 | 856 | 1.5 |
| Count 4 + scatter 30 px | 0.0654 | 0.1079 | 0.254 | 5780 | 90202 | 1088 | 2.0 |
| End taper 200 px, 24 px | 0.0190 | 0.0401 | 0.953 | 1952 | 101661 | 880 | 1.6 |

## brushes, round 3, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers)

Engine + undo capture + composite refresh per input event (4 ms apart, simulated pressure). Dab throughput = dabs / total processing time of the stroke.

| Brush | median ms | p95 ms | max ms | dabs | dabs per s | engine buffers KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Solid Round 12 px (milestone 6 default) | 0.0208 | 0.0757 | 0.129 | 3611 | 125741 | 792 | 1.5 |
| Solid Round 128 px | 0.0005 | 0.4892 | 0.655 | 339 | 3845 | 1584 | 2.7 |
| Clean Ink (preset) | 0.0439 | 0.1097 | 0.288 | 8047 | 163870 | 784 | 1.5 |
| Sketch Pencil (preset, textured) | 0.0710 | 0.1157 | 0.192 | 8060 | 116092 | 768 | 1.4 |
| Textured Paint 40 px (stroke texture) | 0.1755 | 0.2892 | 3.401 | 3028 | 16838 | 1184 | 1.8 |
| Textured Paint 40 px, texture each dab + rotated grain | 0.2003 | 0.3424 | 0.996 | 3028 | 14762 | 1184 | 1.8 |
| Dense textured 120 px, build-up, each dab | 0.5128 | 0.8047 | 1.959 | 808 | 1764 | 2192 | 2.8 |
| Soft Airbrush 90 px (preset) | 0.2001 | 0.3053 | 0.441 | 896 | 5106 | 1568 | 2.6 |
| Smudge 36 px (preset) | 0.1210 | 0.2184 | 3.219 | 2191 | 18259 | 25 | 1.7 |
| Smudge 36 px, dulling + colour rate | 0.1338 | 0.2436 | 0.611 | 2191 | 16607 | 25 | 1.7 |
| Wet paint 40 px | 0.0550 | 0.1133 | 0.481 | 1084 | 19440 | 30 | 1.7 |
| Wet paint 120 px | 0.0003 | 0.7393 | 1.468 | 362 | 2618 | 240 | 2.6 |
| Spray 80 px, 60 particles | 0.0003 | 0.0990 | 0.389 | 271 | 13733 | 1088 | 2.1 |
| Hatching 80 px | 0.0334 | 0.1690 | 0.492 | 542 | 11909 | 1224 | 2.2 |
| Colour dynamics 30 px (hue jitter: per-pixel colour) | 0.0228 | 0.0498 | 0.311 | 1445 | 63073 | 2688 | 1.7 |
| Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors) | 0.0197 | 0.0585 | 0.108 | 2889 | 119810 | 856 | 1.5 |
| Count 4 + scatter 30 px | 0.0626 | 0.1038 | 0.853 | 5780 | 94329 | 1088 | 2.0 |
| End taper 200 px, 24 px | 0.0180 | 0.0416 | 0.954 | 1952 | 104701 | 880 | 1.6 |

## brushes, round 3, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers)

Engine + undo capture + composite refresh per input event (4 ms apart, simulated pressure). Dab throughput = dabs / total processing time of the stroke.

| Brush | median ms | p95 ms | max ms | dabs | dabs per s | engine buffers KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Solid Round 12 px (milestone 6 default) | 0.0174 | 0.0611 | 0.151 | 3611 | 154414 | 792 | 1.5 |
| Solid Round 128 px | 0.0003 | 0.3832 | 0.611 | 339 | 4818 | 1584 | 2.7 |
| Clean Ink (preset) | 0.0385 | 0.0947 | 0.355 | 8047 | 193371 | 784 | 1.5 |
| Sketch Pencil (preset, textured) | 0.0642 | 0.1047 | 0.339 | 8060 | 128239 | 768 | 1.4 |
| Textured Paint 40 px (stroke texture) | 0.1767 | 0.2911 | 0.767 | 3028 | 16869 | 1184 | 1.8 |
| Textured Paint 40 px, texture each dab + rotated grain | 0.2101 | 0.3473 | 0.715 | 3028 | 14251 | 1184 | 1.8 |
| Dense textured 120 px, build-up, each dab | 0.5253 | 0.8623 | 2.578 | 808 | 1652 | 2192 | 2.8 |
| Soft Airbrush 90 px (preset) | 0.2169 | 0.3314 | 1.005 | 896 | 4679 | 1568 | 2.6 |
| Smudge 36 px (preset) | 0.1271 | 0.2304 | 0.475 | 2191 | 17435 | 25 | 1.7 |
| Smudge 36 px, dulling + colour rate | 0.1360 | 0.2373 | 0.693 | 2191 | 16339 | 25 | 1.7 |
| Wet paint 40 px | 0.0601 | 0.1245 | 2.085 | 1084 | 17438 | 30 | 1.7 |
| Wet paint 120 px | 0.0004 | 0.7557 | 1.428 | 362 | 2514 | 240 | 2.6 |
| Spray 80 px, 60 particles | 0.0003 | 0.0959 | 0.201 | 271 | 13515 | 1088 | 2.1 |
| Hatching 80 px | 0.0355 | 0.1844 | 0.344 | 542 | 10903 | 1224 | 2.2 |
| Colour dynamics 30 px (hue jitter: per-pixel colour) | 0.0246 | 0.0433 | 0.091 | 1445 | 59282 | 2688 | 1.7 |
| Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors) | 0.0214 | 0.0584 | 0.110 | 2889 | 113491 | 856 | 1.5 |
| Count 4 + scatter 30 px | 0.0651 | 0.1342 | 0.391 | 5780 | 89186 | 1088 | 2.0 |
| End taper 200 px, 24 px | 0.0194 | 0.0397 | 0.985 | 1952 | 101636 | 880 | 1.6 |

## long, round 1, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Long strokes: 10 000 input events (2048 x 1536, 10 layers)

| Brush | total ms | per event median ms | p95 ms | max ms | dabs | engine buffers MiB | undo snapshot MiB | process private MiB after |
|---|---|---|---|---|---|---|---|---|
| Solid Round 24 px | 212 | 0.0208 | 0.0395 | 0.27 | 14810 | 2.3 | 4.4 | 140 |
| Textured Paint 40 px | 1990 | 0.1875 | 0.3475 | 3.46 | 28551 | 2.4 | 4.5 | 139 |
| Smudge 36 px | 1317 | 0.1290 | 0.2510 | 1.89 | 20863 | 0.0 | 4.5 | 137 |
| Wet paint 40 px | 663 | 0.0698 | 0.1520 | 1.04 | 8886 | 0.0 | 4.5 | 137 |

## long, round 1, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Long strokes: 10 000 input events (2048 x 1536, 10 layers)

| Brush | total ms | per event median ms | p95 ms | max ms | dabs | engine buffers MiB | undo snapshot MiB | process private MiB after |
|---|---|---|---|---|---|---|---|---|
| Solid Round 24 px | 227 | 0.0225 | 0.0421 | 0.25 | 14810 | 2.3 | 4.4 | 146 |
| Textured Paint 40 px | 2077 | 0.1940 | 0.3680 | 3.86 | 28551 | 2.4 | 4.5 | 146 |
| Smudge 36 px | 1444 | 0.1385 | 0.2773 | 0.96 | 20863 | 0.0 | 4.5 | 146 |
| Wet paint 40 px | 660 | 0.0713 | 0.1386 | 0.52 | 8886 | 0.0 | 4.5 | 146 |

## long, round 2, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Long strokes: 10 000 input events (2048 x 1536, 10 layers)

| Brush | total ms | per event median ms | p95 ms | max ms | dabs | engine buffers MiB | undo snapshot MiB | process private MiB after |
|---|---|---|---|---|---|---|---|---|
| Solid Round 24 px | 204 | 0.0205 | 0.0367 | 0.32 | 14810 | 2.3 | 4.4 | 140 |
| Textured Paint 40 px | 1896 | 0.1787 | 0.3242 | 1.04 | 28551 | 2.4 | 4.5 | 139 |
| Smudge 36 px | 1266 | 0.1253 | 0.2317 | 0.87 | 20863 | 0.0 | 4.5 | 137 |
| Wet paint 40 px | 623 | 0.0669 | 0.1231 | 0.50 | 8886 | 0.0 | 4.5 | 137 |

## long, round 2, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Long strokes: 10 000 input events (2048 x 1536, 10 layers)

| Brush | total ms | per event median ms | p95 ms | max ms | dabs | engine buffers MiB | undo snapshot MiB | process private MiB after |
|---|---|---|---|---|---|---|---|---|
| Solid Round 24 px | 223 | 0.0221 | 0.0406 | 0.22 | 14810 | 2.3 | 4.4 | 146 |
| Textured Paint 40 px | 1942 | 0.1858 | 0.3251 | 0.95 | 28551 | 2.4 | 4.5 | 146 |
| Smudge 36 px | 1355 | 0.1339 | 0.2492 | 0.93 | 20863 | 0.0 | 4.5 | 146 |
| Wet paint 40 px | 624 | 0.0676 | 0.1245 | 0.61 | 8886 | 0.0 | 4.5 | 146 |

## long, round 3, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Long strokes: 10 000 input events (2048 x 1536, 10 layers)

| Brush | total ms | per event median ms | p95 ms | max ms | dabs | engine buffers MiB | undo snapshot MiB | process private MiB after |
|---|---|---|---|---|---|---|---|---|
| Solid Round 24 px | 201 | 0.0203 | 0.0359 | 0.32 | 14810 | 2.3 | 4.4 | 140 |
| Textured Paint 40 px | 1909 | 0.1829 | 0.3236 | 1.05 | 28551 | 2.4 | 4.5 | 139 |
| Smudge 36 px | 1270 | 0.1262 | 0.2351 | 0.97 | 20863 | 0.0 | 4.5 | 137 |
| Wet paint 40 px | 632 | 0.0675 | 0.1253 | 4.01 | 8886 | 0.0 | 4.5 | 137 |

## long, round 3, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Long strokes: 10 000 input events (2048 x 1536, 10 layers)

| Brush | total ms | per event median ms | p95 ms | max ms | dabs | engine buffers MiB | undo snapshot MiB | process private MiB after |
|---|---|---|---|---|---|---|---|---|
| Solid Round 24 px | 223 | 0.0220 | 0.0416 | 0.23 | 14810 | 2.3 | 4.4 | 146 |
| Textured Paint 40 px | 2005 | 0.1895 | 0.3437 | 0.98 | 28551 | 2.4 | 4.5 | 146 |
| Smudge 36 px | 1351 | 0.1343 | 0.2421 | 4.31 | 20863 | 0.0 | 4.5 | 146 |
| Wet paint 40 px | 634 | 0.0675 | 0.1307 | 0.59 | 8886 | 0.0 | 4.5 | 146 |

## transforms, round 1, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 89.4 | 89.4 | 89.4 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 187.5 | 236.5 | 236.5 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 24.5 | 27.8 | 27.8 |
| preview after release, merged copy (bicubic) | 66.9 | 70.6 | 70.6 |
| begin with the merged copy (merge + cache, once per transform) | 119.5 | - | - |
| apply: prepare all layers (bicubic) | 582.3 | 582.3 | 582.3 |
| apply: commit (copy in + one group undo entry, 108 MiB kept by history) + composite | 99.7 | - | - |
| undo the transform (all layers) + composite | 43.1 | 43.1 | 43.1 |
| redo the transform (all layers) + composite | 37.4 | 37.4 | 37.4 |

Transform session memory 238 MiB (sources and reductions); preview layers 108 MiB; process private memory grew by 691 MiB during the transform; peak private 826 MiB.

### Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 447.6 | 447.6 | 447.6 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 820.1 | 968.4 | 968.4 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 176.7 | 212.6 | 212.6 |
| preview after release, merged copy (bicubic) | 379.2 | 384.6 | 384.6 |
| begin with the merged copy (merge + cache, once per transform) | 613.9 | - | - |
| apply: prepare all layers (bicubic) | 2305.3 | 2305.3 | 2305.3 |
| apply: commit (copy in + one group undo entry, 0 MiB kept by history) + composite | 520.5 | - | - |
| undo the transform (all layers) + composite | 93.0 | 93.0 | 93.0 |
| redo the transform (all layers) + composite | 103.2 | 103.2 | 103.2 |

Transform session memory 1021 MiB (sources and reductions); preview layers 448 MiB; process private memory grew by 2436 MiB during the transform; peak private 3492 MiB.

## transforms, round 1, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 104.9 | 104.9 | 104.9 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 231.4 | 295.9 | 295.9 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 30.8 | 31.8 | 31.8 |
| preview after release, merged copy (bicubic) | 71.6 | 72.1 | 72.1 |
| begin with the merged copy (merge + cache, once per transform) | 160.0 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 705.6 | 705.6 | 705.6 |
| apply: commit (exchange tile references + one group undo entry, 108 MiB kept by history) + composite | 20.7 | - | - |
| undo the transform (all layers) + composite | 17.2 | 17.2 | 17.2 |
| redo the transform (all layers) + composite | 18.2 | 18.2 | 18.2 |

Transform session memory 238 MiB (sources and reductions); preview layers 108 MiB of their own tiles; process private memory grew by 471 MiB during the transform; peak private 807 MiB.

### Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 452.5 | 452.5 | 452.5 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 1056.4 | 1488.1 | 1488.1 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 216.7 | 235.3 | 235.3 |
| preview after release, merged copy (bicubic) | 506.3 | 532.1 | 532.1 |
| begin with the merged copy (merge + cache, once per transform) | 1002.1 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 3032.8 | 3032.8 | 3032.8 |
| apply: commit (exchange tile references + one group undo entry, 0 MiB kept by history) + composite | 89.6 | - | - |
| undo the transform (all layers) + composite | 73.1 | 73.1 | 73.1 |
| redo the transform (all layers) + composite | 71.7 | 71.7 | 71.7 |

Transform session memory 1021 MiB (sources and reductions); preview layers 447 MiB of their own tiles; process private memory grew by 1998 MiB during the transform; peak private 3498 MiB.

## transforms, round 2, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 104.0 | 104.0 | 104.0 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 192.2 | 201.3 | 201.3 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 23.4 | 24.5 | 24.5 |
| preview after release, merged copy (bicubic) | 71.6 | 75.4 | 75.4 |
| begin with the merged copy (merge + cache, once per transform) | 174.9 | - | - |
| apply: prepare all layers (bicubic) | 654.5 | 654.5 | 654.5 |
| apply: commit (copy in + one group undo entry, 108 MiB kept by history) + composite | 181.8 | - | - |
| undo the transform (all layers) + composite | 49.0 | 49.0 | 49.0 |
| redo the transform (all layers) + composite | 50.3 | 50.3 | 50.3 |

Transform session memory 238 MiB (sources and reductions); preview layers 108 MiB; process private memory grew by 691 MiB during the transform; peak private 826 MiB.

### Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 420.1 | 420.1 | 420.1 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 910.8 | 1050.5 | 1050.5 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 150.0 | 163.2 | 163.2 |
| preview after release, merged copy (bicubic) | 407.1 | 423.9 | 423.9 |
| begin with the merged copy (merge + cache, once per transform) | 1006.5 | - | - |
| apply: prepare all layers (bicubic) | 2692.4 | 2692.4 | 2692.4 |
| apply: commit (copy in + one group undo entry, 0 MiB kept by history) + composite | 731.0 | - | - |
| undo the transform (all layers) + composite | 74.9 | 74.9 | 74.9 |
| redo the transform (all layers) + composite | 68.3 | 68.3 | 68.3 |

Transform session memory 1021 MiB (sources and reductions); preview layers 448 MiB; process private memory grew by 2436 MiB during the transform; peak private 3492 MiB.

## transforms, round 2, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 118.5 | 118.5 | 118.5 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 254.6 | 270.5 | 270.5 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 25.8 | 36.2 | 36.2 |
| preview after release, merged copy (bicubic) | 78.0 | 80.4 | 80.4 |
| begin with the merged copy (merge + cache, once per transform) | 214.1 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 802.0 | 802.0 | 802.0 |
| apply: commit (exchange tile references + one group undo entry, 108 MiB kept by history) + composite | 37.3 | - | - |
| undo the transform (all layers) + composite | 27.6 | 27.6 | 27.6 |
| redo the transform (all layers) + composite | 27.2 | 27.2 | 27.2 |

Transform session memory 238 MiB (sources and reductions); preview layers 108 MiB of their own tiles; process private memory grew by 472 MiB during the transform; peak private 807 MiB.

### Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 537.4 | 537.4 | 537.4 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 1117.4 | 1157.7 | 1157.7 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 155.3 | 171.0 | 171.0 |
| preview after release, merged copy (bicubic) | 452.0 | 471.7 | 471.7 |
| begin with the merged copy (merge + cache, once per transform) | 944.1 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 3048.6 | 3048.6 | 3048.6 |
| apply: commit (exchange tile references + one group undo entry, 0 MiB kept by history) + composite | 120.1 | - | - |
| undo the transform (all layers) + composite | 75.9 | 75.9 | 75.9 |
| redo the transform (all layers) + composite | 72.7 | 72.7 | 72.7 |

Transform session memory 1021 MiB (sources and reductions); preview layers 447 MiB of their own tiles; process private memory grew by 1999 MiB during the transform; peak private 3498 MiB.

## transforms, round 3, milestone 9

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 112.1 | 112.1 | 112.1 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 201.6 | 211.7 | 211.7 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 24.9 | 26.4 | 26.4 |
| preview after release, merged copy (bicubic) | 72.5 | 74.1 | 74.1 |
| begin with the merged copy (merge + cache, once per transform) | 187.3 | - | - |
| apply: prepare all layers (bicubic) | 664.0 | 664.0 | 664.0 |
| apply: commit (copy in + one group undo entry, 108 MiB kept by history) + composite | 168.5 | - | - |
| undo the transform (all layers) + composite | 71.7 | 71.7 | 71.7 |
| redo the transform (all layers) + composite | 63.3 | 63.3 | 63.3 |

Transform session memory 238 MiB (sources and reductions); preview layers 108 MiB; process private memory grew by 691 MiB during the transform; peak private 826 MiB.

### Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 432.7 | 432.7 | 432.7 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 840.4 | 880.5 | 880.5 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 153.7 | 172.7 | 172.7 |
| preview after release, merged copy (bicubic) | 425.9 | 427.8 | 427.8 |
| begin with the merged copy (merge + cache, once per transform) | 883.5 | - | - |
| apply: prepare all layers (bicubic) | 2613.3 | 2613.3 | 2613.3 |
| apply: commit (copy in + one group undo entry, 0 MiB kept by history) + composite | 764.3 | - | - |
| undo the transform (all layers) + composite | 74.3 | 74.3 | 74.3 |
| redo the transform (all layers) + composite | 76.2 | 76.2 | 76.2 |

Transform session memory 1021 MiB (sources and reductions); preview layers 448 MiB; process private memory grew by 2435 MiB during the transform; peak private 3492 MiB.

## transforms, round 3, milestone 10

Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 140.2 | 140.2 | 140.2 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 252.6 | 265.3 | 265.3 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 27.4 | 35.3 | 35.3 |
| preview after release, merged copy (bicubic) | 80.8 | 81.4 | 81.4 |
| begin with the merged copy (merge + cache, once per transform) | 215.2 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 804.0 | 804.0 | 804.0 |
| apply: commit (exchange tile references + one group undo entry, 108 MiB kept by history) + composite | 42.5 | - | - |
| undo the transform (all layers) + composite | 34.8 | 34.8 | 34.8 |
| redo the transform (all layers) + composite | 31.2 | 31.2 | 31.2 |

Transform session memory 238 MiB (sources and reductions); preview layers 108 MiB of their own tiles; process private memory grew by 471 MiB during the transform; peak private 807 MiB.

### Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 475.5 | 475.5 | 475.5 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 1010.0 | 1231.1 | 1231.1 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 149.3 | 188.4 | 188.4 |
| preview after release, merged copy (bicubic) | 442.6 | 448.6 | 448.6 |
| begin with the merged copy (merge + cache, once per transform) | 825.1 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 3290.7 | 3290.7 | 3290.7 |
| apply: commit (exchange tile references + one group undo entry, 0 MiB kept by history) + composite | 116.5 | - | - |
| undo the transform (all layers) + composite | 87.4 | 87.4 | 87.4 |
| redo the transform (all layers) + composite | 80.8 | 80.8 | 80.8 |

Transform session memory 1021 MiB (sources and reductions); preview layers 447 MiB of their own tiles; process private memory grew by 1998 MiB during the transform; peak private 3498 MiB.


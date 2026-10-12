# Milestone 10, first pass (before optimisation), bench_m9 20 repetitions, one process per section

<!-- section doc512 -->

### 512 x 512, 3 layer(s)

Dense equivalent: 3 MiB of layers + 1 MiB composite. Allocated tiles: layers 3.0 MiB, composite 1.0 MiB. Private memory added by the document: 4 MiB (process 7 MiB, working set 15 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 7 | - | - |
| full composite rebuild | 0.52 | 0.58 | 0.58 |
| render the view (fit, 1920 x 1080 window) | 0.969 | 7.484 | 7.484 |
| render the view (100 %, 1920 x 1080 window) | 0.993 | 2.776 | 2.776 |
| render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) | 1.3 | - | - |
| render the view as the canvas does (fit), later frames | 0.597 | 0.737 | 0.737 |
| render the view as the canvas does (5 %) | 0.285 | 1.588 | 1.588 |
| (view pyramid memory after these frames: 0.3 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0087 | 0.0310 | 0.1073 |
| undo that stroke (0.5 MiB of tiles) | 0.00 | 0.01 | 0.01 |
| redo that stroke | 0.00 | 0.01 | 0.01 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.1098 | 0.1709 | 0.4312 |
| save project (encode + atomic write) | 33.7 | 33.7 | 33.7 |
| open project (read + verify + decode) | 14.8 | 14.8 | 14.8 |
| recovery: snapshot on the UI thread | 0.0003 | 0.0023 | 0.0023 |
| recovery: encode (worker) | 29.1 | 29.1 | 29.1 |

File 0.1 MiB. Process peak private memory 27 MiB, peak working set 36 MiB. MemoryBudget peak 7 MiB.

<!-- section doc512x1 -->

### 512 x 512, 1 layer(s)

Dense equivalent: 1 MiB of layers + 1 MiB composite. Allocated tiles: layers 1.0 MiB, composite 1.0 MiB. Private memory added by the document: 2 MiB (process 4 MiB, working set 12 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 2 | - | - |
| full composite rebuild | 0.25 | 0.27 | 0.27 |
| render the view (fit, 1920 x 1080 window) | 0.917 | 6.130 | 6.130 |
| render the view (100 %, 1920 x 1080 window) | 0.980 | 2.739 | 2.739 |
| render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) | 1.3 | - | - |
| render the view as the canvas does (fit), later frames | 0.577 | 0.726 | 0.726 |
| render the view as the canvas does (5 %) | 0.284 | 1.336 | 1.336 |
| (view pyramid memory after these frames: 0.3 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0081 | 0.0278 | 0.0985 |
| undo that stroke (0.5 MiB of tiles) | 0.00 | 0.00 | 0.00 |
| redo that stroke | 0.00 | 0.00 | 0.00 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.1012 | 0.1829 | 0.3328 |
| save project (encode + atomic write) | 10.9 | 10.9 | 10.9 |
| open project (read + verify + decode) | 17.3 | 17.3 | 17.3 |
| recovery: snapshot on the UI thread | 0.0002 | 0.0010 | 0.0010 |
| recovery: encode (worker) | 8.6 | 8.6 | 8.6 |

File 0.0 MiB. Process peak private memory 24 MiB, peak working set 32 MiB. MemoryBudget peak 3 MiB.

<!-- section doc2048 -->

### 2048 x 1536, 10 layer(s)

Dense equivalent: 120 MiB of layers + 12 MiB composite. Allocated tiles: layers 119.9 MiB, composite 12.0 MiB. Private memory added by the document: 135 MiB (process 137 MiB, working set 145 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 357 | - | - |
| full composite rebuild | 26.32 | 36.56 | 36.56 |
| render the view (fit, 1920 x 1080 window) | 9.440 | 16.439 | 16.439 |
| render the view (100 %, 1920 x 1080 window) | 5.712 | 7.767 | 7.767 |
| render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) | 7.6 | - | - |
| render the view as the canvas does (fit), later frames | 4.128 | 5.633 | 5.633 |
| render the view as the canvas does (5 %) | 0.515 | 16.274 | 16.274 |
| (view pyramid memory after these frames: 4.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 1806 dabs | 0.0397 | 0.0762 | 0.1747 |
| undo that stroke (1.6 MiB of tiles) | 0.01 | 0.01 | 0.01 |
| redo that stroke | 0.01 | 0.01 | 0.01 |
| stroke input event, Textured Paint 40 px, 3028 dabs | 0.3198 | 0.5171 | 0.9443 |
| save project (encode + atomic write) | 1469.3 | 1469.3 | 1469.3 |
| open project (read + verify + decode) | 647.1 | 647.1 | 647.1 |
| recovery: snapshot on the UI thread | 0.0010 | 0.0344 | 0.0344 |
| recovery: encode (worker) | 1228.3 | 1228.3 | 1228.3 |

File 5.8 MiB. Process peak private memory 282 MiB, peak working set 289 MiB. MemoryBudget peak 258 MiB.

<!-- section doc4096x8 -->

### 4096 x 4096, 8 layer(s)

Dense equivalent: 512 MiB of layers + 64 MiB composite. Allocated tiles: layers 505.1 MiB, composite 64.0 MiB. Private memory added by the document: 578 MiB (process 580 MiB, working set 587 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 2431 | - | - |
| full composite rebuild | 67.95 | 69.93 | 69.93 |
| render the view (fit, 1920 x 1080 window) | 28.535 | 33.564 | 33.564 |
| render the view (100 %, 1920 x 1080 window) | 4.288 | 6.026 | 6.026 |
| render the view as the canvas does (fit, level 1 of the pyramid), first frame (builds the level) | 46.3 | - | - |
| render the view as the canvas does (fit), later frames | 3.587 | 4.617 | 4.617 |
| render the view as the canvas does (5 %) | 0.462 | 13.854 | 13.854 |
| (view pyramid memory after these frames: 21.2 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0458 | 0.0817 | 0.1652 |
| undo that stroke (3.7 MiB of tiles) | 0.01 | 0.02 | 0.02 |
| redo that stroke | 0.01 | 0.01 | 0.01 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.4439 | 0.7312 | 1.4828 |
| save project (encode + atomic write) | 3319.2 | 3319.2 | 3319.2 |
| open project (read + verify + decode) | 954.7 | 954.7 | 954.7 |
| recovery: snapshot on the UI thread | 0.0059 | 0.0130 | 0.0130 |
| recovery: encode (worker) | 3354.0 | 3354.0 | 3354.0 |

File 12.8 MiB. Process peak private memory 1120 MiB, peak working set 1123 MiB. MemoryBudget peak 1086 MiB.

<!-- section doc4096 -->

### 4096 x 4096, 20 layer(s)

Dense equivalent: 1280 MiB of layers + 64 MiB composite. Allocated tiles: layers 1261.1 MiB, composite 64.0 MiB. Private memory added by the document: 1343 MiB (process 1345 MiB, working set 1350 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 6594 | - | - |
| full composite rebuild | 146.95 | 168.50 | 168.50 |
| render the view (fit, 1920 x 1080 window) | 27.465 | 33.284 | 33.284 |
| render the view (100 %, 1920 x 1080 window) | 4.253 | 5.961 | 5.961 |
| render the view as the canvas does (fit, level 1 of the pyramid), first frame (builds the level) | 42.2 | - | - |
| render the view as the canvas does (fit), later frames | 3.391 | 4.178 | 4.178 |
| render the view as the canvas does (5 %) | 0.695 | 13.141 | 13.141 |
| (view pyramid memory after these frames: 21.2 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0586 | 0.1172 | 0.2530 |
| undo that stroke (3.7 MiB of tiles) | 0.01 | 0.02 | 0.02 |
| redo that stroke | 0.01 | 0.01 | 0.01 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.4864 | 0.8040 | 1.9031 |
| save project (encode + atomic write) | 8547.2 | 8547.2 | 8547.2 |
| open project (read + verify + decode) | 3874.4 | 3874.4 | 3874.4 |
| recovery: snapshot on the UI thread | 0.0007 | 0.0183 | 0.0183 |
| recovery: encode (worker) | 13454.6 | 13454.6 | 13454.6 |

File 34.4 MiB. Process peak private memory 2693 MiB, peak working set 2665 MiB. MemoryBudget peak 2618 MiB.

<!-- section doc8192 -->

### 8192 x 8192, 2 layer(s)

Dense equivalent: 512 MiB of layers + 256 MiB composite. Allocated tiles: layers 464.3 MiB, composite 256.0 MiB. Private memory added by the document: 726 MiB (process 729 MiB, working set 735 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 5276 | - | - |
| full composite rebuild | 100.91 | 118.15 | 118.15 |
| render the view (fit, 1920 x 1080 window) | 134.477 | 152.636 | 152.636 |
| render the view (100 %, 1920 x 1080 window) | 6.242 | 7.836 | 7.836 |
| render the view as the canvas does (fit, level 2 of the pyramid), first frame (builds the level) | 286.3 | - | - |
| render the view as the canvas does (fit), later frames | 6.008 | 6.815 | 6.815 |
| render the view as the canvas does (5 %) | 1.404 | 19.348 | 19.348 |
| (view pyramid memory after these frames: 85.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.1086 | 0.2028 | 0.4769 |
| undo that stroke (6.1 MiB of tiles) | 0.03 | 0.06 | 0.06 |
| redo that stroke | 0.03 | 0.03 | 0.03 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 1.3467 | 2.5012 | 4.6479 |
| save project (encode + atomic write) | 4394.7 | 4394.7 | 4394.7 |
| open project (read + verify + decode) | 952.5 | 952.5 | 952.5 |
| recovery: snapshot on the UI thread | 0.0002 | 0.0022 | 0.0022 |
| recovery: encode (worker) | 2307.5 | 2307.5 | 2307.5 |

File 5.6 MiB. Process peak private memory 1215 MiB, peak working set 1218 MiB. MemoryBudget peak 1183 MiB.

<!-- section doc8192x10 -->

### 8192 x 8192, 10 layer(s)

Dense equivalent: 2560 MiB of layers + 256 MiB composite. Allocated tiles: layers 2136.3 MiB, composite 256.0 MiB. Private memory added by the document: 2416 MiB (process 2419 MiB, working set 2421 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 36165 | - | - |
| full composite rebuild | 294.90 | 312.92 | 312.92 |
| render the view (fit, 1920 x 1080 window) | 164.574 | 187.887 | 187.887 |
| render the view (100 %, 1920 x 1080 window) | 6.897 | 8.226 | 8.226 |
| render the view as the canvas does (fit, level 2 of the pyramid), first frame (builds the level) | 347.9 | - | - |
| render the view as the canvas does (fit), later frames | 6.380 | 7.821 | 7.821 |
| render the view as the canvas does (5 %) | 1.417 | 22.588 | 22.588 |
| (view pyramid memory after these frames: 85.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.1123 | 0.1873 | 0.2722 |
| undo that stroke (6.1 MiB of tiles) | 0.03 | 0.08 | 0.08 |
| redo that stroke | 0.03 | 0.08 | 0.08 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.9417 | 1.7342 | 3.7744 |
| save project (encode + atomic write) | 13913.0 | 13913.0 | 13913.0 |
| open project (read + verify + decode) | 7321.0 | 7321.0 | 7321.0 |
| recovery: snapshot on the UI thread | 0.0008 | 0.4412 | 0.4412 |
| recovery: encode (worker) | 19401.5 | 19401.5 | 19401.5 |

File 42.9 MiB. Process peak private memory 4560 MiB, peak working set 3543 MiB. MemoryBudget peak 4482 MiB.

<!-- section sparse16384 -->

### 16384 x 16384, 4 layer(s), sparse (a few long strokes per layer)

Dense equivalent: 4096 MiB of layers + 1024 MiB composite. Allocated tiles: layers 54.6 MiB, composite 54.6 MiB. Private memory added by the document: 117 MiB (process 119 MiB, working set 124 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 2749 | - | - |
| full composite rebuild | 14.96 | 15.78 | 15.78 |
| render the view (fit, 1920 x 1080 window) | 313.828 | 361.886 | 361.886 |
| render the view (100 %, 1920 x 1080 window) | 5.625 | 7.125 | 7.125 |
| render the view as the canvas does (fit, level 3 of the pyramid), first frame (builds the level) | 152.7 | - | - |
| render the view as the canvas does (fit), later frames | 5.260 | 7.184 | 7.184 |
| render the view as the canvas does (5 %) | 4.215 | 8.905 | 8.905 |
| (view pyramid memory after these frames: 41.6 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 18190 dabs | 0.2790 | 0.5164 | 0.6555 |
| undo that stroke (0.2 MiB of tiles) | 0.04 | 0.11 | 0.11 |
| redo that stroke | 0.04 | 0.04 | 0.04 |
| stroke input event, Textured Paint 40 px, 30517 dabs | 1.7907 | 3.4883 | 5.2929 |
| save project (encode + atomic write) | 681.3 | 681.3 | 681.3 |
| open project (read + verify + decode) | 137.2 | 137.2 | 137.2 |
| recovery: snapshot on the UI thread | 0.0003 | 0.0018 | 0.0018 |
| recovery: encode (worker) | 711.0 | 711.0 | 711.0 |

File 2.4 MiB. Process peak private memory 1153 MiB, peak working set 1157 MiB. MemoryBudget peak 1133 MiB.

<!-- section brushes -->

### Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers)

Engine + undo capture + composite refresh per input event (4 ms apart, simulated pressure). Dab throughput = dabs / total processing time of the stroke.

| Brush | median ms | p95 ms | max ms | dabs | dabs per s | engine buffers KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Solid Round 12 px (milestone 6 default) | 0.0194 | 0.0694 | 0.251 | 3611 | 131842 | 792 | 1.5 |
| Solid Round 128 px | 0.0005 | 0.4431 | 1.096 | 339 | 4421 | 1584 | 2.7 |
| Clean Ink (preset) | 0.0405 | 0.0970 | 0.324 | 8047 | 181780 | 784 | 1.5 |
| Sketch Pencil (preset, textured) | 0.0674 | 0.1220 | 0.621 | 8060 | 118524 | 768 | 1.4 |
| Textured Paint 40 px (stroke texture) | 0.1786 | 0.2879 | 1.083 | 3028 | 16553 | 1184 | 1.8 |
| Textured Paint 40 px, texture each dab + rotated grain | 0.2171 | 0.3815 | 0.893 | 3028 | 13685 | 1184 | 1.8 |
| Dense textured 120 px, build-up, each dab | 0.5604 | 0.9057 | 1.843 | 808 | 1612 | 2192 | 2.8 |
| Soft Airbrush 90 px (preset) | 0.2430 | 0.3920 | 0.779 | 896 | 4114 | 1568 | 2.6 |
| Smudge 36 px (preset) | 0.1644 | 0.3145 | 0.801 | 2191 | 13183 | 25 | 1.7 |
| Smudge 36 px, dulling + colour rate | 0.1765 | 0.3165 | 0.713 | 2191 | 12330 | 25 | 1.7 |
| Wet paint 40 px | 0.0888 | 0.1650 | 0.433 | 1084 | 13140 | 30 | 1.7 |
| Wet paint 120 px | 0.0005 | 0.9659 | 2.135 | 362 | 2079 | 240 | 2.6 |
| Spray 80 px, 60 particles | 0.0003 | 0.0972 | 0.384 | 271 | 13064 | 1088 | 2.1 |
| Hatching 80 px | 0.0382 | 0.1802 | 0.517 | 542 | 10197 | 1224 | 2.2 |
| Colour dynamics 30 px (hue jitter: per-pixel colour) | 0.0279 | 0.0649 | 1.910 | 1445 | 48031 | 2688 | 1.7 |
| Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors) | 0.0246 | 0.0678 | 0.118 | 2889 | 99346 | 856 | 1.5 |
| Count 4 + scatter 30 px | 0.0690 | 0.1179 | 0.233 | 5780 | 86388 | 1088 | 2.0 |
| End taper 200 px, 24 px | 0.0215 | 0.0463 | 0.955 | 1952 | 89917 | 880 | 1.6 |

<!-- section long -->

### Long strokes: 10 000 input events (2048 x 1536, 10 layers)

| Brush | total ms | per event median ms | p95 ms | max ms | dabs | engine buffers MiB | undo snapshot MiB | process private MiB after |
|---|---|---|---|---|---|---|---|---|
| Solid Round 24 px | 257 | 0.0247 | 0.0495 | 0.44 | 14810 | 2.3 | 4.4 | 145 |
| Textured Paint 40 px | 2142 | 0.2037 | 0.3721 | 1.23 | 28551 | 2.4 | 4.5 | 146 |
| Smudge 36 px | 1706 | 0.1630 | 0.3442 | 3.42 | 20863 | 0.0 | 4.5 | 146 |
| Wet paint 40 px | 683 | 0.0726 | 0.1516 | 0.87 | 8886 | 0.0 | 4.5 | 146 |

<!-- section transforms -->

### Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 188.9 | 188.9 | 188.9 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 326.9 | 378.0 | 378.0 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 80.5 | 86.2 | 86.2 |
| preview after release, merged copy (bicubic) | 190.2 | 212.2 | 212.2 |
| begin with the merged copy (merge + cache, once per transform) | 406.1 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 1279.2 | 1279.2 | 1279.2 |
| apply: commit (exchange tile references + one group undo entry, 108 MiB kept by history) + composite | 28.5 | - | - |
| undo the transform (all layers) + composite | 31.4 | 31.4 | 31.4 |
| redo the transform (all layers) + composite | 34.1 | 34.1 | 34.1 |

Transform session memory 238 MiB (sources and reductions); preview layers 108 MiB of their own tiles; process private memory grew by 469 MiB during the transform; peak private 800 MiB.

### Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 784.0 | 784.0 | 784.0 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 2071.7 | 2334.6 | 2334.6 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 401.8 | 462.7 | 462.7 |
| preview after release, merged copy (bicubic) | 716.7 | 946.4 | 946.4 |
| begin with the merged copy (merge + cache, once per transform) | 1759.6 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 4482.2 | 4482.2 | 4482.2 |
| apply: commit (exchange tile references + one group undo entry, 0 MiB kept by history) + composite | 94.6 | - | - |
| undo the transform (all layers) + composite | 69.2 | 69.2 | 69.2 |
| redo the transform (all layers) + composite | 69.5 | 69.5 | 69.5 |

Transform session memory 1021 MiB (sources and reductions); preview layers 447 MiB of their own tiles; process private memory grew by 1997 MiB during the transform; peak private 3486 MiB.

<!-- section transforms20 -->

### Multi-layer transform: 19 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 2640.2 | 2640.2 | 2640.2 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 5030.2 | 6523.1 | 6523.1 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 23.9 | 27.0 | 27.0 |
| preview after release, merged copy (bicubic) | 24.2 | 25.1 | 25.1 |
| begin with the merged copy (merge + cache, once per transform) | 541.9 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 17612.1 | 17612.1 | 17612.1 |
| apply: commit (exchange tile references + one group undo entry, 0 MiB kept by history) + composite | 711.7 | - | - |
| undo the transform (all layers) + composite | 258.7 | 258.7 | 258.7 |
| redo the transform (all layers) + composite | 235.2 | 235.2 | 235.2 |

Transform session memory 2771 MiB (sources and reductions); preview layers 1214 MiB of their own tiles; process private memory grew by 5289 MiB during the transform; peak private 7851 MiB.


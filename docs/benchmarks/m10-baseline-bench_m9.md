Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 512 x 512, 3 layer(s)

Layer pixels 3 MiB (+ composite 1 MiB). Private memory added by the document: 4 MiB (process 6 MiB, working set 14 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 4 | - | - |
| full composite rebuild | 0.35 | 0.40 | 0.40 |
| render the view (fit, 1920 x 1080 window) | 0.385 | 5.397 | 5.397 |
| render the view (100 %, 1920 x 1080 window) | 0.361 | 1.774 | 1.774 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0049 | 0.0169 | 0.0832 |
| undo that stroke (0.5 MiB of tiles) | 0.03 | 0.08 | 0.08 |
| redo that stroke | 0.03 | 0.04 | 0.04 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.0649 | 0.1536 | 0.8375 |
| save project (encode + atomic write) | 16.8 | 16.8 | 16.8 |
| open project (read + verify + decode) | 15.2 | 15.2 | 15.2 |

File 0.1 MiB. Process peak private memory 30 MiB, peak working set 38 MiB.
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 2048 x 1536, 10 layer(s)

Layer pixels 120 MiB (+ composite 12 MiB). Private memory added by the document: 132 MiB (process 135 MiB, working set 142 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 102 | - | - |
| full composite rebuild | 14.53 | 15.88 | 15.88 |
| render the view (fit, 1920 x 1080 window) | 1.630 | 7.259 | 7.259 |
| render the view (100 %, 1920 x 1080 window) | 1.046 | 2.651 | 2.651 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 1806 dabs | 0.0194 | 0.0405 | 0.5264 |
| undo that stroke (1.6 MiB of tiles) | 0.15 | 0.42 | 0.42 |
| redo that stroke | 0.15 | 0.17 | 0.17 |
| stroke input event, Textured Paint 40 px, 3028 dabs | 0.2117 | 0.4213 | 0.6439 |
| save project (encode + atomic write) | 831.0 | 831.0 | 831.0 |
| open project (read + verify + decode) | 245.5 | 245.5 | 245.5 |

File 4.8 MiB. Process peak private memory 310 MiB, peak working set 316 MiB.
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 4096 x 4096, 20 layer(s)

Refused by design: 20 layers of this size exceed the 512 MiB document limit (at most 8 layers). Measured with 8 layers instead.

Layer pixels 512 MiB (+ composite 64 MiB). Private memory added by the document: 578 MiB (process 580 MiB, working set 586 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 522 | - | - |
| full composite rebuild | 63.03 | 64.73 | 64.73 |
| render the view (fit, 1920 x 1080 window) | 1.790 | 6.511 | 6.511 |
| render the view (100 %, 1920 x 1080 window) | 1.207 | 2.945 | 2.945 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0384 | 0.0777 | 0.1561 |
| undo that stroke (3.7 MiB of tiles) | 1.10 | 1.26 | 1.26 |
| redo that stroke | 1.16 | 1.22 | 1.22 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.3871 | 0.6517 | 1.5146 |
| save project (encode + atomic write) | 2914.2 | 2914.2 | 2914.2 |
| open project (read + verify + decode) | 1082.5 | 1082.5 | 1082.5 |

File 10.3 MiB. Process peak private memory 1267 MiB, peak working set 1268 MiB.
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 8192 x 8192, 2 layer(s)

Layer pixels 512 MiB (+ composite 256 MiB). Private memory added by the document: 770 MiB (process 772 MiB, working set 778 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 445 | - | - |
| full composite rebuild | 73.47 | 74.58 | 74.58 |
| render the view (fit, 1920 x 1080 window) | 2.186 | 6.549 | 6.549 |
| render the view (100 %, 1920 x 1080 window) | 1.276 | 3.056 | 3.056 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.0642 | 0.1172 | 0.6939 |
| undo that stroke (7.5 MiB of tiles) | 2.27 | 2.87 | 2.87 |
| redo that stroke | 2.32 | 2.42 | 2.42 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.6968 | 1.2272 | 2.1367 |
| save project (encode + atomic write) | 2308.2 | 2308.2 | 2308.2 |
| open project (read + verify + decode) | 964.2 | 964.2 | 964.2 |

File 4.6 MiB. Process peak private memory 1833 MiB, peak working set 1834 MiB.
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers)

Engine + undo capture + composite refresh per input event (4 ms apart, simulated pressure). Dab throughput = dabs / total processing time of the stroke.

| Brush | median ms | p95 ms | max ms | dabs | dabs per s | engine buffers KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Solid Round 12 px (milestone 6 default) | 0.0168 | 0.0626 | 0.090 | 3611 | 153894 | 792 | 1.5 |
| Solid Round 128 px | 0.0004 | 0.4120 | 1.031 | 339 | 4504 | 1584 | 2.7 |
| Clean Ink (preset) | 0.0347 | 0.0937 | 0.205 | 8047 | 201317 | 784 | 1.5 |
| Sketch Pencil (preset, textured) | 0.0639 | 0.1089 | 0.266 | 8060 | 125832 | 768 | 1.4 |
| Textured Paint 40 px (stroke texture) | 0.1736 | 0.2891 | 0.509 | 3028 | 17130 | 1184 | 1.8 |
| Textured Paint 40 px, texture each dab + rotated grain | 0.2122 | 0.3468 | 1.027 | 3028 | 14225 | 1184 | 1.8 |
| Dense textured 120 px, build-up, each dab | 0.5184 | 0.9165 | 2.044 | 808 | 1690 | 2192 | 2.8 |
| Soft Airbrush 90 px (preset) | 0.1949 | 0.3379 | 0.783 | 896 | 4950 | 1568 | 2.6 |
| Smudge 36 px (preset) | 0.1338 | 0.2364 | 0.476 | 2191 | 17084 | 25 | 1.7 |
| Smudge 36 px, dulling + colour rate | 0.1322 | 0.2473 | 0.782 | 2191 | 16283 | 25 | 1.7 |
| Wet paint 40 px | 0.0559 | 0.1193 | 0.411 | 1084 | 18500 | 30 | 1.7 |
| Wet paint 120 px | 0.0004 | 0.7709 | 1.892 | 362 | 2497 | 240 | 2.6 |
| Spray 80 px, 60 particles | 0.0003 | 0.1035 | 0.320 | 271 | 13480 | 1088 | 2.1 |
| Hatching 80 px | 0.0328 | 0.1756 | 0.471 | 542 | 10937 | 1224 | 2.2 |
| Colour dynamics 30 px (hue jitter: per-pixel colour) | 0.0227 | 0.0595 | 0.231 | 1445 | 58623 | 2688 | 1.7 |
| Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors) | 0.0217 | 0.0649 | 0.204 | 2889 | 109271 | 856 | 1.5 |
| Count 4 + scatter 30 px | 0.0660 | 0.1438 | 0.595 | 5780 | 87882 | 1088 | 2.0 |
| End taper 200 px, 24 px | 0.0179 | 0.0418 | 0.982 | 1952 | 103688 | 880 | 1.6 |
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Long strokes: 10 000 input events (2048 x 1536, 10 layers)

| Brush | total ms | per event median ms | p95 ms | max ms | dabs | engine buffers MiB | undo snapshot MiB | process private MiB after |
|---|---|---|---|---|---|---|---|---|
| Solid Round 24 px | 233 | 0.0206 | 0.0483 | 3.35 | 14810 | 2.3 | 4.4 | 139 |
| Textured Paint 40 px | 1935 | 0.1817 | 0.3414 | 3.75 | 28551 | 2.4 | 4.5 | 140 |
| Smudge 36 px | 1304 | 0.1238 | 0.2572 | 0.76 | 20863 | 0.0 | 4.5 | 138 |
| Wet paint 40 px | 660 | 0.0681 | 0.1469 | 0.68 | 8886 | 0.0 | 4.5 | 137 |
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 101.3 | 101.3 | 101.3 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 194.6 | 244.7 | 244.7 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 24.8 | 25.5 | 25.5 |
| preview after release, merged copy (bicubic) | 72.0 | 83.7 | 83.7 |
| begin with the merged copy (merge + cache, once per transform) | 130.3 | - | - |
| apply: prepare all layers (bicubic) | 572.8 | 572.8 | 572.8 |
| apply: commit (copy in + one group undo entry, 108 MiB kept by history) + composite | 99.0 | - | - |
| undo the transform (all layers) + composite | 36.7 | 36.7 | 36.7 |
| redo the transform (all layers) + composite | 37.3 | 37.3 | 37.3 |

Transform session memory 238 MiB (sources and reductions); preview layers 108 MiB; process private memory grew by 691 MiB during the transform; peak private 826 MiB.

### Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 461.1 | 461.1 | 461.1 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 959.1 | 963.2 | 963.2 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 242.1 | 252.5 | 252.5 |
| preview after release, merged copy (bicubic) | 450.9 | 479.7 | 479.7 |
| begin with the merged copy (merge + cache, once per transform) | 1380.5 | - | - |
| apply: prepare all layers (bicubic) | 2828.1 | 2828.1 | 2828.1 |
| apply: commit (copy in + one group undo entry, 0 MiB kept by history) + composite | 823.9 | - | - |
| undo the transform (all layers) + composite | 77.6 | 77.6 | 77.6 |
| redo the transform (all layers) + composite | 77.5 | 77.5 | 77.5 |

Transform session memory 1021 MiB (sources and reductions); preview layers 448 MiB; process private memory grew by 2436 MiB during the transform; peak private 3493 MiB.

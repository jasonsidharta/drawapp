Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 512 x 512, 3 layer(s)

Layer pixels 3 MiB (+ composite 1 MiB). Private memory added by the document: 4 MiB (process 6 MiB, working set 14 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 4 | - | - |
| full composite rebuild | 0.22 | 0.24 | 0.24 |
| render the view (fit, 1920 x 1080 window) | 0.317 | 4.017 | 4.017 |
| render the view (100 %, 1920 x 1080 window) | 0.357 | 1.687 | 1.687 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 569 dabs | 0.0047 | 0.0166 | 0.0823 |
| undo that stroke (0.5 MiB of tiles) | 0.03 | 0.08 | 0.08 |
| redo that stroke | 0.03 | 0.03 | 0.03 |
| stroke input event, Textured Paint 40 px, 947 dabs | 0.0663 | 0.1007 | 0.2157 |
| save project (encode + atomic write) | 16.7 | 16.7 | 16.7 |
| open project (read + verify + decode) | 17.4 | 17.4 | 17.4 |

File 0.1 MiB. Process peak private memory 29 MiB, peak working set 38 MiB.
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 2048 x 1536, 10 layer(s)

Layer pixels 120 MiB (+ composite 12 MiB). Private memory added by the document: 132 MiB (process 135 MiB, working set 142 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 97 | - | - |
| full composite rebuild | 12.44 | 13.42 | 13.42 |
| render the view (fit, 1920 x 1080 window) | 1.750 | 6.241 | 6.241 |
| render the view (100 %, 1920 x 1080 window) | 1.097 | 2.868 | 2.868 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 1806 dabs | 0.0189 | 0.0370 | 0.1248 |
| undo that stroke (1.6 MiB of tiles) | 0.17 | 0.46 | 0.46 |
| redo that stroke | 0.17 | 0.20 | 0.20 |
| stroke input event, Textured Paint 40 px, 3028 dabs | 0.1617 | 0.3156 | 0.8348 |
| save project (encode + atomic write) | 772.5 | 772.5 | 772.5 |
| open project (read + verify + decode) | 238.8 | 238.8 | 238.8 |

File 4.8 MiB. Process peak private memory 310 MiB, peak working set 316 MiB.
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 4096 x 4096, 20 layer(s)

Refused by design: 20 layers of this size exceed the 512 MiB document limit (at most 8 layers). Measured with 8 layers instead.

Layer pixels 512 MiB (+ composite 64 MiB). Private memory added by the document: 578 MiB (process 580 MiB, working set 586 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 480 | - | - |
| full composite rebuild | 62.21 | 62.95 | 62.95 |
| render the view (fit, 1920 x 1080 window) | 1.802 | 5.779 | 5.779 |
| render the view (100 %, 1920 x 1080 window) | 1.186 | 2.854 | 2.854 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 4548 dabs | 0.0365 | 0.0682 | 0.1334 |
| undo that stroke (3.7 MiB of tiles) | 0.82 | 1.07 | 1.07 |
| redo that stroke | 0.77 | 1.01 | 1.01 |
| stroke input event, Textured Paint 40 px, 7628 dabs | 0.3809 | 0.6276 | 4.0162 |
| save project (encode + atomic write) | 2716.5 | 2716.5 | 2716.5 |
| open project (read + verify + decode) | 1111.3 | 1111.3 | 1111.3 |

File 10.3 MiB. Process peak private memory 1267 MiB, peak working set 1268 MiB.
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### 8192 x 8192, 2 layer(s)

Layer pixels 512 MiB (+ composite 256 MiB). Private memory added by the document: 770 MiB (process 772 MiB, working set 778 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 430 | - | - |
| full composite rebuild | 71.99 | 78.37 | 78.37 |
| render the view (fit, 1920 x 1080 window) | 2.455 | 6.626 | 6.626 |
| render the view (100 %, 1920 x 1080 window) | 1.284 | 3.078 | 3.078 |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.0661 | 0.1187 | 0.6007 |
| undo that stroke (7.5 MiB of tiles) | 2.99 | 3.16 | 3.16 |
| redo that stroke | 2.87 | 3.00 | 3.00 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.7085 | 1.2286 | 2.1282 |
| save project (encode + atomic write) | 2928.5 | 2928.5 | 2928.5 |
| open project (read + verify + decode) | 1091.8 | 1091.8 | 1091.8 |

File 4.6 MiB. Process peak private memory 1833 MiB, peak working set 1835 MiB.
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers)

Engine + undo capture + composite refresh per input event (4 ms apart, simulated pressure). Dab throughput = dabs / total processing time of the stroke.

| Brush | median ms | p95 ms | max ms | dabs | dabs per s | engine buffers KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Solid Round 12 px (milestone 6 default) | 0.0210 | 0.0698 | 0.151 | 3611 | 130644 | 792 | 1.5 |
| Solid Round 128 px | 0.0005 | 0.4884 | 0.660 | 339 | 3733 | 1584 | 2.7 |
| Clean Ink (preset) | 0.0405 | 0.1106 | 0.245 | 8047 | 170021 | 784 | 1.5 |
| Sketch Pencil (preset, textured) | 0.0739 | 0.1182 | 0.190 | 8060 | 111010 | 768 | 1.4 |
| Textured Paint 40 px (stroke texture) | 0.1971 | 0.3327 | 0.668 | 3028 | 15147 | 1184 | 1.8 |
| Textured Paint 40 px, texture each dab + rotated grain | 0.2158 | 0.3746 | 1.200 | 3028 | 13501 | 1184 | 1.8 |
| Dense textured 120 px, build-up, each dab | 0.5280 | 0.8716 | 1.940 | 808 | 1693 | 2192 | 2.8 |
| Soft Airbrush 90 px (preset) | 0.1903 | 0.3005 | 1.210 | 896 | 5169 | 1568 | 2.6 |
| Smudge 36 px (preset) | 0.1245 | 0.2292 | 0.635 | 2191 | 17880 | 25 | 1.7 |
| Smudge 36 px, dulling + colour rate | 0.1397 | 0.2811 | 0.695 | 2191 | 15243 | 25 | 1.7 |
| Wet paint 40 px | 0.0618 | 0.1219 | 0.616 | 1084 | 18111 | 30 | 1.7 |
| Wet paint 120 px | 0.0004 | 0.7587 | 1.975 | 362 | 2528 | 240 | 2.6 |
| Spray 80 px, 60 particles | 0.0003 | 0.1011 | 0.435 | 271 | 13149 | 1088 | 2.1 |
| Hatching 80 px | 0.0316 | 0.1689 | 0.773 | 542 | 11262 | 1224 | 2.2 |
| Colour dynamics 30 px (hue jitter: per-pixel colour) | 0.0238 | 0.0598 | 0.337 | 1445 | 55291 | 2688 | 1.7 |
| Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors) | 0.0225 | 0.0615 | 0.288 | 2889 | 104774 | 856 | 1.5 |
| Count 4 + scatter 30 px | 0.0653 | 0.1669 | 0.523 | 5780 | 84488 | 1088 | 2.0 |
| End taper 200 px, 24 px | 0.0192 | 0.0464 | 1.061 | 1952 | 95511 | 880 | 1.6 |
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Long strokes: 10 000 input events (2048 x 1536, 10 layers)

| Brush | total ms | per event median ms | p95 ms | max ms | dabs | engine buffers MiB | undo snapshot MiB | process private MiB after |
|---|---|---|---|---|---|---|---|---|
| Solid Round 24 px | 220 | 0.0214 | 0.0407 | 0.29 | 14810 | 2.3 | 4.4 | 139 |
| Textured Paint 40 px | 1922 | 0.1830 | 0.3307 | 3.56 | 28551 | 2.4 | 4.5 | 139 |
| Smudge 36 px | 1341 | 0.1277 | 0.2652 | 4.26 | 20863 | 0.0 | 4.5 | 137 |
| Wet paint 40 px | 634 | 0.0666 | 0.1367 | 1.01 | 8886 | 0.0 | 4.5 | 137 |
Windows 11 Version 25H2, x86_64; 20 repetitions; Release build (MinGW, -O2)

### Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 97.8 | 97.8 | 97.8 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 214.6 | 282.6 | 282.6 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 33.2 | 36.8 | 36.8 |
| preview after release, merged copy (bicubic) | 92.6 | 95.2 | 95.2 |
| begin with the merged copy (merge + cache, once per transform) | 232.4 | - | - |
| apply: prepare all layers (bicubic) | 899.4 | 899.4 | 899.4 |
| apply: commit (copy in + one group undo entry, 108 MiB kept by history) + composite | 180.4 | - | - |
| undo the transform (all layers) + composite | 54.1 | 54.1 | 54.1 |
| redo the transform (all layers) + composite | 57.9 | 57.9 | 57.9 |

Transform session memory 238 MiB (sources and reductions); preview layers 108 MiB; process private memory grew by 691 MiB during the transform; peak private 826 MiB.

### Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 496.4 | 496.4 | 496.4 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 1098.5 | 1210.3 | 1210.3 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 262.4 | 286.9 | 286.9 |
| preview after release, merged copy (bicubic) | 536.9 | 659.9 | 659.9 |
| begin with the merged copy (merge + cache, once per transform) | 1604.7 | - | - |
| apply: prepare all layers (bicubic) | 3177.2 | 3177.2 | 3177.2 |
| apply: commit (copy in + one group undo entry, 0 MiB kept by history) + composite | 1027.6 | - | - |
| undo the transform (all layers) + composite | 75.2 | 75.2 | 75.2 |
| redo the transform (all layers) + composite | 79.5 | 79.5 | 79.5 |

Transform session memory 1021 MiB (sources and reductions); preview layers 448 MiB; process private memory grew by 2436 MiB during the transform; peak private 3492 MiB.

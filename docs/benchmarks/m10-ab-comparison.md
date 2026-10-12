# Milestone 9 vs milestone 10: comparison tables

Generated from `m10-ab-raw.md` (median of the three per-round medians; all runs listed). Ratios below 1 mean milestone 10 is faster. Rows whose meaning changed between versions are marked with an arrow.

| Workload | Operation | milestone 9 (ms) | milestone 10 (ms) | ratio m10 / m9 | runs (m9; m10) |
|---|---|---|---|---|---|
| 512 x 512, 3 layer(s) | create the test document (setup, not an app operation) | 5.00 | 7.00 | 1.40 | 3.00, 5.00, 5.00; 5.00, 7.00, 7.00 |
| 512 x 512, 3 layer(s) | full composite rebuild | 0.320 | 0.400 | 1.25 | 0.220, 0.320, 0.340; 0.300, 0.400, 0.410 |
| 512 x 512, 3 layer(s) | render the view (fit, 1920 x 1080 window) → m10: render the view as the canvas does (fit), later frames | 0.446 | 0.394 | 0.88 | 0.314, 0.521, 0.446; 0.335, 0.448, 0.394 |
| 512 x 512, 3 layer(s) | render the view (100 %, 1920 x 1080 window) → m10: render the view as the canvas does (100 %), later frames | 0.520 | 0.399 | 0.77 | 0.327, 0.520, 0.548; 0.350, 0.469, 0.399 |
| 512 x 512, 3 layer(s) | stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), N dabs | 0.006 | 0.006 | 1.00 | 0.005, 0.008, 0.006; 0.005, 0.007, 0.006 |
| 512 x 512, 3 layer(s) | undo that stroke (... MiB of tiles) | 0.040 | 0.000 | 0.00 | 0.030, 0.050, 0.040; 0.000, 0.000, 0.000 |
| 512 x 512, 3 layer(s) | redo that stroke | 0.040 | 0.000 | 0.00 | 0.030, 0.050, 0.040; 0.000, 0.000, 0.000 |
| 512 x 512, 3 layer(s) | stroke input event, Textured Paint 40 px, N dabs | 0.076 | 0.071 | 0.94 | 0.064, 0.090, 0.076; 0.066, 0.086, 0.071 |
| 512 x 512, 3 layer(s) | save project (encode + atomic write) | 18.50 | 14.50 | 0.78 | 16.40, 23.20, 18.50; 13.10, 17.20, 14.50 |
| 512 x 512, 3 layer(s) | open project (read + verify + decode) | 18.60 | 21.60 | 1.16 | 18.70, 18.60, 13.70; 20.40, 21.70, 21.60 |
| 512 x 512, 3 layer(s) | render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) (new in milestone 10) | - | 0.788 | | 0.711, 0.934, 0.788 |
| 512 x 512, 3 layer(s) | render the view without the pyramid (100 %) (new in milestone 10) | - | 0.760 | | 0.739, 0.891, 0.760 |
| 512 x 512, 3 layer(s) | render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) (new in milestone 10) | - | 1.10 | | 1.10, 1.10, 1.10 |
| 512 x 512, 3 layer(s) | render the view as the canvas does (fit), while panning 7 px per frame (new in milestone 10) | - | 0.409 | | 0.340, 0.484, 0.409 |
| 512 x 512, 3 layer(s) | render the view as the canvas does (5 %) (new in milestone 10) | - | 0.210 | | 0.186, 0.249, 0.210 |
| 512 x 512, 3 layer(s) | recovery: snapshot on the UI thread (new in milestone 10) | - | 0.000 | | 0.000, 0.001, 0.000 |
| 512 x 512, 3 layer(s) | recovery: encode (worker) (new in milestone 10) | - | 8.50 | | 8.40, 9.10, 8.50 |
| 2048 x 1536, 10 layer(s) | create the test document (setup, not an app operation) | 107 | 240 | 2.24 | 89.00, 113, 107; 228, 248, 240 |
| 2048 x 1536, 10 layer(s) | full composite rebuild | 12.85 | 14.95 | 1.16 | 12.40, 14.13, 12.85; 14.78, 16.25, 14.95 |
| 2048 x 1536, 10 layer(s) | render the view (fit, 1920 x 1080 window) → m10: render the view as the canvas does (fit), later frames | 1.54 | 1.74 | 1.14 | 1.36, 1.90, 1.54; 1.59, 1.84, 1.74 |
| 2048 x 1536, 10 layer(s) | render the view (100 %, 1920 x 1080 window) → m10: render the view as the canvas does (100 %), later frames | 1.08 | 1.13 | 1.05 | 1.08, 1.15, 1.03; 1.02, 1.25, 1.13 |
| 2048 x 1536, 10 layer(s) | stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), N dabs | 0.020 | 0.019 | 0.99 | 0.018, 0.022, 0.020; 0.018, 0.022, 0.019 |
| 2048 x 1536, 10 layer(s) | undo that stroke (... MiB of tiles) | 0.170 | 0.000 | 0.00 | 0.140, 0.210, 0.170; 0.000, 0.000, 0.000 |
| 2048 x 1536, 10 layer(s) | redo that stroke | 0.170 | 0.000 | 0.00 | 0.140, 0.170, 0.190; 0.000, 0.000, 0.000 |
| 2048 x 1536, 10 layer(s) | stroke input event, Textured Paint 40 px, N dabs | 0.163 | 0.171 | 1.05 | 0.162, 0.180, 0.163; 0.164, 0.182, 0.171 |
| 2048 x 1536, 10 layer(s) | save project (encode + atomic write) | 824 | 239 | 0.29 | 806, 849, 824; 231, 243, 239 |
| 2048 x 1536, 10 layer(s) | open project (read + verify + decode) | 248 | 186 | 0.75 | 251, 248, 234; 146, 187, 186 |
| 2048 x 1536, 10 layer(s) | render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) (new in milestone 10) | - | 5.63 | | 5.37, 6.21, 5.63 |
| 2048 x 1536, 10 layer(s) | render the view without the pyramid (100 %) (new in milestone 10) | - | 3.82 | | 3.67, 3.92, 3.82 |
| 2048 x 1536, 10 layer(s) | render the view as the canvas does (fit, level 0 of the pyramid), first frame (builds the level) (new in milestone 10) | - | 6.60 | | 6.20, 7.80, 6.60 |
| 2048 x 1536, 10 layer(s) | render the view as the canvas does (fit), while panning 7 px per frame (new in milestone 10) | - | 1.73 | | 1.61, 1.73, 1.77 |
| 2048 x 1536, 10 layer(s) | render the view as the canvas does (5 %) (new in milestone 10) | - | 0.213 | | 0.213, 0.252, 0.195 |
| 2048 x 1536, 10 layer(s) | recovery: snapshot on the UI thread (new in milestone 10) | - | 0.000 | | 0.000, 0.001, 0.000 |
| 2048 x 1536, 10 layer(s) | recovery: encode (worker) (new in milestone 10) | - | 244 | | 244, 266, 234 |
| 4096 x 4096, 20 layer(s) | create the test document (setup, not an app operation) | 542 | 1980 | 3.65 | 469, 607, 542; 2002, 1980, 1956 |
| 4096 x 4096, 20 layer(s) | full composite rebuild | 62.68 | 53.35 | 0.85 | 60.32, 64.71, 62.68; 53.35, 56.63, 52.71 |
| 4096 x 4096, 20 layer(s) | render the view (fit, 1920 x 1080 window) → m10: render the view as the canvas does (fit), later frames | 1.64 | 1.49 | 0.91 | 1.78, 1.64, 1.57; 1.37, 1.49, 1.66 |
| 4096 x 4096, 20 layer(s) | render the view (100 %, 1920 x 1080 window) → m10: render the view as the canvas does (100 %), later frames | 1.18 | 1.12 | 0.95 | 1.26, 1.18, 1.17; 1.12, 1.11, 1.12 |
| 4096 x 4096, 20 layer(s) | stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), N dabs | 0.037 | 0.036 | 0.98 | 0.036, 0.040, 0.037; 0.036, 0.040, 0.036 |
| 4096 x 4096, 20 layer(s) | undo that stroke (... MiB of tiles) | 0.960 | 0.010 | 0.01 | 0.960, 0.990, 0.770; 0.010, 0.010, 0.010 |
| 4096 x 4096, 20 layer(s) | redo that stroke | 0.950 | 0.010 | 0.01 | 0.950, 0.960, 0.680; 0.010, 0.010, 0.010 |
| 4096 x 4096, 20 layer(s) | stroke input event, Textured Paint 40 px, N dabs | 0.383 | 0.386 | 1.01 | 0.379, 0.398, 0.383; 0.376, 0.386, 0.389 |
| 4096 x 4096, 20 layer(s) | save project (encode + atomic write) | 2751 | 794 | 0.29 | 2682, 2989, 2751; 794, 883, 712 |
| 4096 x 4096, 20 layer(s) | open project (read + verify + decode) | 970 | 630 | 0.65 | 989, 967, 970; 628, 677, 630 |
| 4096 x 4096, 8 layer(s) | render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) (new in milestone 10) | - | 24.49 | | 24.49, 25.02, 23.94 |
| 4096 x 4096, 8 layer(s) | render the view without the pyramid (100 %) (new in milestone 10) | - | 3.77 | | 3.77, 3.93, 3.66 |
| 4096 x 4096, 8 layer(s) | render the view as the canvas does (fit, level 1 of the pyramid), first frame (builds the level) (new in milestone 10) | - | 41.80 | | 41.90, 41.80, 39.60 |
| 4096 x 4096, 8 layer(s) | render the view as the canvas does (fit), while panning 7 px per frame (new in milestone 10) | - | 1.43 | | 1.51, 1.43, 1.43 |
| 4096 x 4096, 8 layer(s) | render the view as the canvas does (5 %) (new in milestone 10) | - | 0.356 | | 0.356, 0.376, 0.286 |
| 4096 x 4096, 8 layer(s) | recovery: snapshot on the UI thread (new in milestone 10) | - | 0.000 | | 0.000, 0.000, 0.000 |
| 4096 x 4096, 8 layer(s) | recovery: encode (worker) (new in milestone 10) | - | 719 | | 719, 918, 679 |
| 8192 x 8192, 2 layer(s) | create the test document (setup, not an app operation) | 420 | 3193 | 7.60 | 420, 520, 417; 3379, 3163, 3193 |
| 8192 x 8192, 2 layer(s) | full composite rebuild | 71.95 | 77.70 | 1.08 | 71.95, 73.87, 70.93; 102, 77.70, 69.97 |
| 8192 x 8192, 2 layer(s) | render the view (fit, 1920 x 1080 window) → m10: render the view as the canvas does (fit), later frames | 2.37 | 1.96 | 0.83 | 2.37, 2.42, 2.20; 2.06, 1.96, 1.90 |
| 8192 x 8192, 2 layer(s) | render the view (100 %, 1920 x 1080 window) → m10: render the view as the canvas does (100 %), later frames | 1.20 | 1.07 | 0.89 | 1.20, 1.28, 1.20; 1.07, 1.36, 1.03 |
| 8192 x 8192, 2 layer(s) | stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), N dabs | 0.063 | 0.064 | 1.02 | 0.063, 0.067, 0.063; 0.065, 0.063, 0.064 |
| 8192 x 8192, 2 layer(s) | undo that stroke (... MiB of tiles) | 2.39 | 0.020 | 0.01 | 2.39, 2.49, 1.89; 0.020, 0.020, 0.010 |
| 8192 x 8192, 2 layer(s) | redo that stroke | 2.33 | 0.020 | 0.01 | 2.33, 2.48, 1.85; 0.020, 0.020, 0.010 |
| 8192 x 8192, 2 layer(s) | stroke input event, Textured Paint 40 px, N dabs | 0.729 | 0.733 | 1.01 | 0.734, 0.715, 0.729; 0.737, 0.733, 0.730 |
| 8192 x 8192, 2 layer(s) | save project (encode + atomic write) | 2132 | 857 | 0.40 | 2241, 2098, 2132; 857, 1031, 826 |
| 8192 x 8192, 2 layer(s) | open project (read + verify + decode) | 861 | 616 | 0.72 | 878, 861, 854; 616, 722, 583 |
| 8192 x 8192, 2 layer(s) | render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) (new in milestone 10) | - | 101 | | 104, 101, 94.35 |
| 8192 x 8192, 2 layer(s) | render the view without the pyramid (100 %) (new in milestone 10) | - | 3.86 | | 3.77, 4.31, 3.86 |
| 8192 x 8192, 2 layer(s) | render the view as the canvas does (fit, level 2 of the pyramid), first frame (builds the level) (new in milestone 10) | - | 170 | | 171, 170, 162 |
| 8192 x 8192, 2 layer(s) | render the view as the canvas does (fit), while panning 7 px per frame (new in milestone 10) | - | 2.04 | | 2.04, 2.02, 2.04 |
| 8192 x 8192, 2 layer(s) | render the view as the canvas does (5 %) (new in milestone 10) | - | 0.663 | | 0.573, 0.663, 0.699 |
| 8192 x 8192, 2 layer(s) | recovery: snapshot on the UI thread (new in milestone 10) | - | 0.000 | | 0.000, 0.000, 0.000 |
| 8192 x 8192, 2 layer(s) | recovery: encode (worker) (new in milestone 10) | - | 864 | | 864, 1254, 820 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Solid Round 12 px (milestone 6 default) | 0.021 | 0.017 | 0.84 | 0.017, 0.033, 0.021; 0.028, 0.017, 0.017 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Solid Round 128 px | 0.001 | 0.000 | 0.60 | 0.001, 0.001, 0.001; 0.000, 0.000, 0.000 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Clean Ink (preset) | 0.044 | 0.037 | 0.84 | 0.037, 0.053, 0.044; 0.037, 0.036, 0.038 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Sketch Pencil (preset, textured) | 0.071 | 0.064 | 0.90 | 0.069, 0.081, 0.071; 0.064, 0.062, 0.064 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Textured Paint 40 px (stroke texture) | 0.189 | 0.177 | 0.94 | 0.189, 0.196, 0.175; 0.184, 0.170, 0.177 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Textured Paint 40 px, texture each dab + rotated grain | 0.215 | 0.210 | 0.98 | 0.242, 0.215, 0.200; 0.249, 0.204, 0.210 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Dense textured 120 px, build-up, each dab | 0.513 | 0.537 | 1.05 | 0.587, 0.508, 0.513; 0.555, 0.537, 0.525 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Soft Airbrush 90 px (preset) | 0.200 | 0.217 | 1.08 | 0.241, 0.197, 0.200; 0.225, 0.210, 0.217 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Smudge 36 px (preset) | 0.121 | 0.127 | 1.05 | 0.151, 0.120, 0.121; 0.129, 0.122, 0.127 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Smudge 36 px, dulling + colour rate | 0.134 | 0.136 | 1.01 | 0.158, 0.134, 0.134; 0.141, 0.136, 0.136 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Wet paint 40 px | 0.055 | 0.059 | 1.07 | 0.067, 0.055, 0.055; 0.059, 0.056, 0.060 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Wet paint 120 px | 0.000 | 0.000 | 1.00 | 0.000, 0.000, 0.000; 0.000, 0.000, 0.000 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Spray 80 px, 60 particles | 0.000 | 0.000 | 1.00 | 0.000, 0.000, 0.000; 0.000, 0.000, 0.000 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Hatching 80 px | 0.033 | 0.035 | 1.08 | 0.033, 0.031, 0.033; 0.035, 0.035, 0.035 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Colour dynamics 30 px (hue jitter: per-pixel colour) | 0.023 | 0.025 | 1.08 | 0.023, 0.022, 0.023; 0.027, 0.024, 0.025 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors) | 0.020 | 0.021 | 1.09 | 0.020, 0.019, 0.020; 0.021, 0.022, 0.021 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | Count 4 + scatter 30 px | 0.063 | 0.065 | 1.04 | 0.063, 0.064, 0.063; 0.067, 0.065, 0.065 |
| Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers) | End taper 200 px, 24 px | 0.019 | 0.019 | 1.04 | 0.019, 0.020, 0.018; 0.020, 0.019, 0.019 |
| Long strokes: 10 000 input events (2048 x 1536, 10 layers) | Solid Round 24 px | 204 | 223 | 1.09 | 212, 204, 201; 227, 223, 223 |
| Long strokes: 10 000 input events (2048 x 1536, 10 layers) | Textured Paint 40 px | 1909 | 2005 | 1.05 | 1990, 1896, 1909; 2077, 1942, 2005 |
| Long strokes: 10 000 input events (2048 x 1536, 10 layers) | Smudge 36 px | 1270 | 1355 | 1.07 | 1317, 1266, 1270; 1444, 1355, 1351 |
| Long strokes: 10 000 input events (2048 x 1536, 10 layers) | Wet paint 40 px | 632 | 634 | 1.00 | 663, 623, 632; 660, 624, 634 |

| Workload | Measure | milestone 9 | milestone 10 |
|---|---|---|---|
| 512 x 512, 3 layer(s) | docmem | 4.00 | 4.00 |
| 512 x 512, 3 layer(s) | file | 0.100 | 0.100 |
| 512 x 512, 3 layer(s) | peak | 29.00 | 31.00 |
| 2048 x 1536, 10 layer(s) | docmem | 132 | 135 |
| 2048 x 1536, 10 layer(s) | file | 4.80 | 5.80 |
| 2048 x 1536, 10 layer(s) | peak | 310 | 290 |
| 4096 x 4096, 20 layer(s) | docmem | 577 | 578 |
| 4096 x 4096, 20 layer(s) | file | 10.30 | 12.80 |
| 4096 x 4096, 20 layer(s) | peak | 1267 | 1131 |
| 8192 x 8192, 2 layer(s) | docmem | 770 | 726 |
| 8192 x 8192, 2 layer(s) | file | 4.60 | 5.60 |
| 8192 x 8192, 2 layer(s) | peak | 1833 | 1226 |

## Transforms (after the content-bounds change)

| Workload | Operation | milestone 9 (ms) | milestone 10 (ms) | ratio m10 / m9 | runs (m9; m10) |
|---|---|---|---|---|---|
| Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection | begin (cache each layer's source and reductions) | 104 | 118 | 1.14 | 89.40, 104, 112; 105, 118, 140 |
| Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection | preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 192 | 253 | 1.31 | 188, 192, 202; 231, 255, 253 |
| Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection | preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 24.50 | 27.40 | 1.12 | 24.50, 23.40, 24.90; 30.80, 25.80, 27.40 |
| Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection | preview after release, merged copy (bicubic) | 71.60 | 78.00 | 1.09 | 66.90, 71.60, 72.50; 71.60, 78.00, 80.80 |
| Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection | begin with the merged copy (merge + cache, once per transform) | 175 | 214 | 1.22 | 120, 175, 187; 160, 214, 215 |
| Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection | apply: prepare all layers (bicubic) → m10: apply: prepare all layers (bicubic; on the worker thread in the app) | 654 | 802 | 1.23 | 582, 654, 664; 706, 802, 804 |
| Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection | apply: commit (copy in + one group undo entry, ... MiB kept by history) + composite → m10: apply: commit (exchange tile references + one group undo entry, ... MiB kept by history) + composite | 168 | 37.30 | 0.22 | 99.70, 182, 168; 20.70, 37.30, 42.50 |
| Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection | undo the transform (all layers) + composite | 49.00 | 27.60 | 0.56 | 43.10, 49.00, 71.70; 17.20, 27.60, 34.80 |
| Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection | redo the transform (all layers) + composite | 50.30 | 27.20 | 0.54 | 37.40, 50.30, 63.30; 18.20, 27.20, 31.20 |
| Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection | begin (cache each layer's source and reductions) | 433 | 476 | 1.10 | 448, 420, 433; 452, 537, 476 |
| Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection | preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 840 | 1056 | 1.26 | 820, 911, 840; 1056, 1117, 1010 |
| Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection | preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 154 | 155 | 1.01 | 177, 150, 154; 217, 155, 149 |
| Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection | preview after release, merged copy (bicubic) | 407 | 452 | 1.11 | 379, 407, 426; 506, 452, 443 |
| Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection | begin with the merged copy (merge + cache, once per transform) | 884 | 944 | 1.07 | 614, 1006, 884; 1002, 944, 825 |
| Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection | apply: prepare all layers (bicubic) → m10: apply: prepare all layers (bicubic; on the worker thread in the app) | 2613 | 3049 | 1.17 | 2305, 2692, 2613; 3033, 3049, 3291 |
| Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection | apply: commit (copy in + one group undo entry, ... MiB kept by history) + composite → m10: apply: commit (exchange tile references + one group undo entry, ... MiB kept by history) + composite | 731 | 116 | 0.16 | 520, 731, 764; 89.60, 120, 116 |
| Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection | undo the transform (all layers) + composite | 74.90 | 75.90 | 1.01 | 93.00, 74.90, 74.30; 73.10, 75.90, 87.40 |
| Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection | redo the transform (all layers) + composite | 76.20 | 72.70 | 0.95 | 103, 68.30, 76.20; 71.70, 72.70, 80.80 |

| Workload | Measure | milestone 9 | milestone 10 |
|---|---|---|---|
| Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection | xpeak | 826 | 807 |
| Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection | xpeak | 3492 | 3498 |

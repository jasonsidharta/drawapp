Windows 11 Version 25H2, x86_64; canvas 2048x1536

### Like-for-like single layer (same content as the milestone 2 benchmark)

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event + undo capture (+ composite refresh), brush 12 px | 0.0072 | 0.0218 | 0.0769 |
| save project, painted dabs (0.63 MiB file) | 84.5 | - | 99.3 |
| open project, painted dabs | 24.5 | - | 33.7 |
| export PNG, painted dabs | 152.5 | - | 157.2 |

### Brush engine: one input event of a 1000-event stroke (stroke processing + undo capture + composite refresh)

| Variant | median ms | p95 ms | max ms | coverage tiles |
|---|---|---|---|---|
| milestone 4 path (QPainter dabs), 12 px | 0.0067 | 0.0199 | 0.1070 | - |
| stroke engine, 12 px, 100 %, smoothing off | 0.0127 | 0.0551 | 0.2952 | 94 |
| stroke engine, 12 px, 50 %, smoothing off | 0.0124 | 0.0540 | 0.3559 | 94 |
| stroke engine, 12 px, 50 %, smoothing 20 px | 0.0135 | 0.0570 | 0.2616 | 95 |
| milestone 4 path (QPainter dabs), 128 px | 0.0032 | 0.0239 | 0.2640 | - |
| stroke engine, 128 px, 50 %, smoothing off | 0.0071 | 0.2664 | 1.2105 | 184 |
| smoothing only, string 0 px (no painting) | 0.0000 | 0.0001 | 0.0002 | - |
| smoothing only, string 20 px (no painting) | 0.0003 | 0.0004 | 0.0020 | - |
| smoothing only, string 80 px (no painting) | 0.0003 | 0.0004 | 0.0048 | - |

### Brush presets: one input event of a 1000-event stroke (engine + undo capture + composite refresh)

Coverage tiles are 64 x 64 x 16-bit (8 KiB); the undo snapshot holds the touched 64 x 64 layer tiles.

1 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.051 | 0.099 | 0.382 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 0.915 | 2.829 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.026 | 0.069 | 0.473 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.065 | 0.740 | 2.509 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.013 | 0.054 | 0.329 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.554 | 2.160 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.146 | 0.255 | 0.957 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.001 | 0.806 | 2.787 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.144 | 0.267 | 0.761 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.746 | 2.723 | 5.715 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.079 | 0.133 | 0.568 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 1.277 | 5.005 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.012 | 0.026 | 0.171 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.463 | 4.011 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.083 | 0.169 | 0.672 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.001 | 0.834 | 2.096 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 2.195 | 2.643 | 3.139 | (next event: median 0.080) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.003 | 0.004 | 0.004 | (next event: median 3.721) | | |

Process private memory with this document: 29 MiB

5 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.060 | 0.140 | 0.599 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 0.986 | 2.516 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.028 | 0.079 | 0.415 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.067 | 0.862 | 5.471 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.014 | 0.055 | 0.202 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.001 | 0.709 | 2.828 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.163 | 0.303 | 1.037 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.002 | 1.582 | 3.470 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.151 | 0.318 | 3.383 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.927 | 2.977 | 5.588 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.102 | 0.195 | 0.639 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 1.705 | 5.243 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.014 | 0.029 | 0.310 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.495 | 1.579 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.091 | 0.167 | 0.621 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.001 | 0.778 | 3.984 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 2.113 | 2.176 | 2.228 | (next event: median 0.069) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.004 | 0.004 | 0.007 | (next event: median 3.963) | | |

Process private memory with this document: 77 MiB

10 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.057 | 0.104 | 2.921 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.001 | 1.455 | 3.318 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.032 | 0.083 | 0.383 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.066 | 0.883 | 2.906 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.016 | 0.062 | 0.647 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.605 | 4.672 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.188 | 0.358 | 1.669 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.001 | 1.624 | 5.776 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.165 | 0.379 | 0.836 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.955 | 2.652 | 5.682 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.106 | 0.217 | 0.886 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 1.987 | 6.680 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.017 | 0.033 | 0.391 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.694 | 2.758 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.120 | 0.285 | 0.971 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.001 | 1.467 | 2.961 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 2.352 | 2.973 | 3.096 | (next event: median 0.116) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.005 | 0.006 | 0.009 | (next event: median 5.064) | | |

Process private memory with this document: 137 MiB

### 1 layer(s): layer pixels 12 MiB + composite 12 MiB; file 0.01 MiB, recovery file 0.01 MiB

Process private memory added by creating the document: 24 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0134 | 0.0627 | 0.3824 |
| full composite rebuild (visibility/opacity/order change) | 1.08 | 1.56 | 1.81 |
| recovery snapshot on UI thread (shallow copy) | 0.0004 | 0.0011 | 0.0012 |
| first dab on the active layer after a snapshot (copies that layer) | 2.110 | 2.540 | 2.623 |
| same dab without a pending snapshot | 0.0108 | 0.0264 | 0.0299 |
| recovery encode + atomic write (worker thread) | 39.4 | 40.5 | 40.5 |
| save project (encode + atomic write) | 39.5 | 39.9 | 39.9 |
| verify encoded project (checksum + decompress each layer) | 7.8 | 8.0 | 8.0 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 49.3 | 51.6 | 51.6 |
| open project | 15.4 | 16.3 | 16.3 |
| export PNG (composite + encode) | 81.3 | 83.0 | 83.0 |

### 5 layer(s): layer pixels 60 MiB + composite 12 MiB; file 2.06 MiB, recovery file 2.06 MiB

Process private memory added by creating the document: 73 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0142 | 0.0609 | 0.5700 |
| full composite rebuild (visibility/opacity/order change) | 5.06 | 6.46 | 6.70 |
| recovery snapshot on UI thread (shallow copy) | 0.0006 | 0.0010 | 0.0013 |
| first dab on the active layer after a snapshot (copies that layer) | 2.085 | 2.346 | 3.154 |
| same dab without a pending snapshot | 0.0088 | 0.0146 | 0.0188 |
| recovery encode + atomic write (worker thread) | 367.3 | 506.9 | 506.9 |
| save project (encode + atomic write) | 382.0 | 406.6 | 406.6 |
| verify encoded project (checksum + decompress each layer) | 74.0 | 77.3 | 77.3 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 457.4 | 549.7 | 549.7 |
| open project | 119.6 | 131.9 | 131.9 |
| export PNG (composite + encode) | 225.2 | 249.5 | 249.5 |

### 10 layer(s): layer pixels 120 MiB + composite 12 MiB; file 4.60 MiB, recovery file 4.60 MiB

Process private memory added by creating the document: 132 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0167 | 0.0615 | 0.3364 |
| full composite rebuild (visibility/opacity/order change) | 11.68 | 16.87 | 21.24 |
| recovery snapshot on UI thread (shallow copy) | 0.0006 | 0.0030 | 0.0059 |
| first dab on the active layer after a snapshot (copies that layer) | 2.098 | 2.312 | 2.340 |
| same dab without a pending snapshot | 0.0109 | 0.0373 | 0.0434 |
| recovery encode + atomic write (worker thread) | 748.1 | 759.9 | 759.9 |
| save project (encode + atomic write) | 746.9 | 752.0 | 752.0 |
| verify encoded project (checksum + decompress each layer) | 155.5 | 157.4 | 157.4 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 907.1 | 919.0 | 919.0 |
| open project | 233.2 | 237.7 | 237.7 |
| export PNG (composite + encode) | 349.2 | 377.1 | 377.1 |

### View rendering (composite cache drawn through the view; independent of layer count)

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| render full 1920x1080 view, zoom 0.5 | 0.9665 | - | 2.5371 |
| render full 1920x1080 view, zoom 1.0 | 1.0879 | - | 2.8161 |
| render 64x64 dirty rect, zoom 1.0 | 0.0033 | - | 0.2453 |

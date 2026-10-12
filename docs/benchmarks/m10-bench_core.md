Windows 11 Version 25H2, x86_64; canvas 2048x1536

### Like-for-like single layer (same content as the milestone 2 benchmark)

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event + undo capture (+ composite refresh), brush 12 px | 0.0111 | 0.0315 | 0.0724 |
| save project, painted dabs (0.73 MiB file) | 26.6 | - | 31.3 |
| open project, painted dabs | 13.6 | - | 40.4 |
| export PNG, painted dabs | 150.4 | - | 188.3 |

### Brush engine: one input event of a 1000-event stroke (stroke processing + undo capture + composite refresh)

| Variant | median ms | p95 ms | max ms | coverage tiles |
|---|---|---|---|---|
| milestone 4 path (QPainter dabs), 12 px | 0.0107 | 0.0323 | 0.3986 | - |
| stroke engine, 12 px, 100 %, smoothing off | 0.0133 | 0.0570 | 0.1703 | 94 |
| stroke engine, 12 px, 50 %, smoothing off | 0.0131 | 0.0570 | 0.3996 | 94 |
| stroke engine, 12 px, 50 %, smoothing 20 px | 0.0140 | 0.0560 | 0.1938 | 95 |
| milestone 4 path (QPainter dabs), 128 px | 0.0058 | 0.0687 | 0.5250 | - |
| stroke engine, 128 px, 50 %, smoothing off | 0.0075 | 0.2770 | 1.0117 | 184 |
| smoothing only, string 0 px (no painting) | 0.0000 | 0.0001 | 0.0001 | - |
| smoothing only, string 20 px (no painting) | 0.0003 | 0.0004 | 0.0171 | - |
| smoothing only, string 80 px (no painting) | 0.0003 | 0.0004 | 0.0048 | - |

### Brush presets: one input event of a 1000-event stroke (engine + undo capture + composite refresh)

Coverage tiles are 64 x 64 x 16-bit (8 KiB); the undo snapshot holds the touched 64 x 64 layer tiles.

1 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.053 | 0.102 | 0.673 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 0.924 | 1.861 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.027 | 0.073 | 0.142 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.059 | 0.698 | 2.543 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.013 | 0.057 | 0.412 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.490 | 1.512 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.153 | 0.250 | 0.590 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.001 | 0.764 | 1.639 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.145 | 0.245 | 0.981 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.832 | 1.806 | 4.495 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.089 | 0.151 | 0.577 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 1.386 | 2.429 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.013 | 0.025 | 0.110 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.460 | 1.473 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.085 | 0.150 | 0.773 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.000 | 0.664 | 1.526 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 0.021 | 0.040 | 0.047 | (next event: median 0.060) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.001 | 0.003 | 0.003 | (next event: median 1.521) | | |

Process private memory with this document: 36 MiB

5 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.052 | 0.096 | 0.771 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 0.980 | 1.910 | 307 | 2456 | 4.6 |
| Clean Ink | 8 | 0.029 | 0.076 | 0.264 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.062 | 0.765 | 1.406 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.014 | 0.057 | 0.101 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.525 | 0.977 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.167 | 0.267 | 0.885 | 209 | 1672 | 3.2 |
| Soft Airbrush | 200 | 0.001 | 0.841 | 1.886 | 360 | 2880 | 5.4 |
| Textured Paint | 40 | 0.146 | 0.252 | 0.707 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.796 | 1.906 | 3.423 | 378 | 3024 | 5.7 |
| Blend / Smudge | 36 | 0.120 | 0.237 | 0.756 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 2.006 | 3.610 | 0 | 650 | 4.7 |
| Hard Eraser | 24 | 0.014 | 0.028 | 0.141 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.512 | 1.423 | 273 | 2184 | 4.1 |
| Soft Eraser | 70 | 0.095 | 0.158 | 0.236 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.000 | 0.762 | 1.650 | 359 | 2872 | 5.4 |
| first event after a snapshot, Clean Ink 8 px | | 0.022 | 0.045 | 0.047 | (next event: median 0.064) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.001 | 0.003 | 0.003 | (next event: median 1.641) | | |

Process private memory with this document: 77 MiB

10 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.054 | 0.101 | 0.504 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 1.067 | 2.783 | 307 | 2456 | 4.6 |
| Clean Ink | 8 | 0.030 | 0.076 | 0.166 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.067 | 0.853 | 2.934 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.018 | 0.059 | 0.269 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.621 | 1.174 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.192 | 0.302 | 0.910 | 209 | 1672 | 3.2 |
| Soft Airbrush | 200 | 0.001 | 0.970 | 2.349 | 360 | 2880 | 5.4 |
| Textured Paint | 40 | 0.158 | 0.266 | 0.806 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.868 | 2.148 | 3.983 | 378 | 3024 | 5.7 |
| Blend / Smudge | 36 | 0.123 | 0.235 | 0.416 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 2.187 | 7.140 | 0 | 650 | 4.7 |
| Hard Eraser | 24 | 0.017 | 0.032 | 0.109 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.648 | 10.677 | 273 | 2184 | 4.1 |
| Soft Eraser | 70 | 0.112 | 0.180 | 0.437 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.001 | 0.913 | 2.142 | 359 | 2872 | 5.4 |
| first event after a snapshot, Clean Ink 8 px | | 0.030 | 0.058 | 0.106 | (next event: median 0.068) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.002 | 0.004 | 0.005 | (next event: median 1.775) | | |

Process private memory with this document: 130 MiB

### 1 layer(s): layer pixels 12 MiB + composite 12 MiB; file 0.04 MiB, recovery file 0.04 MiB

Process private memory added by creating the document: 24 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0134 | 0.0566 | 0.1086 |
| full composite rebuild (visibility/opacity/order change) | 1.94 | 2.57 | 4.03 |
| recovery snapshot on UI thread (shallow copy) | 0.0001 | 0.0002 | 0.0007 |
| first dab on the active layer after a snapshot (copies that layer) | 0.009 | 0.011 | 0.041 |
| same dab without a pending snapshot | 0.0047 | 0.0061 | 0.0084 |
| recovery encode + atomic write (worker thread) | 17.8 | 21.0 | 21.0 |
| save project (encode + atomic write) | 19.5 | 20.1 | 20.1 |
| verify encoded project (checksum + decompress each layer) | 2.5 | 8.8 | 8.8 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 20.9 | 21.5 | 21.5 |
| open project | 10.6 | 16.5 | 16.5 |
| export PNG (composite + encode) | 78.5 | 100.0 | 100.0 |

### 5 layer(s): layer pixels 60 MiB + composite 12 MiB; file 2.44 MiB, recovery file 2.44 MiB

Process private memory added by creating the document: 65 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0147 | 0.0577 | 0.1320 |
| full composite rebuild (visibility/opacity/order change) | 6.10 | 6.98 | 7.47 |
| recovery snapshot on UI thread (shallow copy) | 0.0001 | 0.0002 | 0.0015 |
| first dab on the active layer after a snapshot (copies that layer) | 0.009 | 0.010 | 0.044 |
| same dab without a pending snapshot | 0.0048 | 0.0090 | 0.0106 |
| recovery encode + atomic write (worker thread) | 125.5 | 168.9 | 168.9 |
| save project (encode + atomic write) | 102.8 | 125.4 | 125.4 |
| verify encoded project (checksum + decompress each layer) | 32.0 | 36.9 | 36.9 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 136.1 | 145.8 | 145.8 |
| open project | 68.1 | 72.1 | 72.1 |
| export PNG (composite + encode) | 206.6 | 224.8 | 224.8 |

### 10 layer(s): layer pixels 120 MiB + composite 12 MiB; file 5.44 MiB, recovery file 5.44 MiB

Process private memory added by creating the document: 118 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0169 | 0.0603 | 0.3857 |
| full composite rebuild (visibility/opacity/order change) | 12.79 | 14.22 | 14.24 |
| recovery snapshot on UI thread (shallow copy) | 0.0002 | 0.0002 | 0.0009 |
| first dab on the active layer after a snapshot (copies that layer) | 0.008 | 0.010 | 0.046 |
| same dab without a pending snapshot | 0.0047 | 0.0055 | 0.0081 |
| recovery encode + atomic write (worker thread) | 195.1 | 242.7 | 242.7 |
| save project (encode + atomic write) | 236.2 | 255.9 | 255.9 |
| verify encoded project (checksum + decompress each layer) | 85.9 | 96.8 | 96.8 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 322.8 | 341.7 | 341.7 |
| open project | 147.5 | 164.0 | 164.0 |
| export PNG (composite + encode) | 330.9 | 370.7 | 370.7 |

### View rendering (composite cache drawn through the view; independent of layer count)

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| render full 1920x1080 view, zoom 0.5 | 5.2919 | - | 8.8024 |
| render full 1920x1080 view, zoom 1.0 | 3.8489 | - | 5.2285 |
| render 64x64 dirty rect, zoom 1.0 | 0.0051 | - | 0.1374 |

Windows 11 Version 25H2, x86_64; canvas 2048x1536

### Like-for-like single layer (same content as the milestone 2 benchmark)

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event + undo capture (+ composite refresh), brush 12 px | 0.0089 | 0.0275 | 0.1970 |
| save project, painted dabs (0.63 MiB file) | 110.6 | - | 131.7 |
| open project, painted dabs | 31.7 | - | 45.8 |
| export PNG, painted dabs | 169.2 | - | 221.8 |

### Brush engine: one input event of a 1000-event stroke (stroke processing + undo capture + composite refresh)

| Variant | median ms | p95 ms | max ms | coverage tiles |
|---|---|---|---|---|
| milestone 4 path (QPainter dabs), 12 px | 0.0069 | 0.0228 | 0.1294 | - |
| stroke engine, 12 px, 100 %, smoothing off | 0.0137 | 0.0568 | 0.1293 | 94 |
| stroke engine, 12 px, 50 %, smoothing off | 0.0131 | 0.0575 | 0.1703 | 94 |
| stroke engine, 12 px, 50 %, smoothing 20 px | 0.0149 | 0.0573 | 0.4973 | 95 |
| milestone 4 path (QPainter dabs), 128 px | 0.0038 | 0.0270 | 2.5164 | - |
| stroke engine, 128 px, 50 %, smoothing off | 0.0071 | 0.2890 | 0.6983 | 184 |
| smoothing only, string 0 px (no painting) | 0.0000 | 0.0001 | 0.0003 | - |
| smoothing only, string 20 px (no painting) | 0.0003 | 0.0004 | 0.0038 | - |
| smoothing only, string 80 px (no painting) | 0.0003 | 0.0004 | 0.0034 | - |

### Brush presets: one input event of a 1000-event stroke (engine + undo capture + composite refresh)

Coverage tiles are 64 x 64 x 16-bit (8 KiB); the undo snapshot holds the touched 64 x 64 layer tiles.

1 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.053 | 0.099 | 0.273 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 0.917 | 4.344 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.027 | 0.072 | 0.564 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.061 | 0.704 | 2.310 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.013 | 0.058 | 0.295 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.502 | 1.353 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.152 | 0.270 | 0.681 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.001 | 0.838 | 2.027 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.147 | 0.294 | 1.356 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.734 | 1.863 | 3.488 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.077 | 0.137 | 0.416 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 1.278 | 2.935 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.013 | 0.029 | 0.239 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.477 | 1.288 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.084 | 0.166 | 0.567 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.000 | 0.699 | 1.965 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 2.102 | 2.410 | 2.903 | (next event: median 0.065) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.003 | 0.005 | 0.007 | (next event: median 3.721) | | |

Process private memory with this document: 29 MiB

5 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.054 | 0.107 | 0.377 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 0.982 | 4.648 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.029 | 0.077 | 0.470 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.063 | 0.750 | 1.836 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.014 | 0.059 | 0.346 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.543 | 1.673 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.160 | 0.339 | 0.935 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.001 | 0.900 | 4.014 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.155 | 0.273 | 0.759 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.789 | 2.020 | 4.221 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.100 | 0.193 | 0.584 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 1.731 | 6.769 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.014 | 0.029 | 0.175 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.501 | 1.575 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.089 | 0.189 | 0.664 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.001 | 0.784 | 2.401 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 2.106 | 2.497 | 3.264 | (next event: median 0.070) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.003 | 0.004 | 0.005 | (next event: median 3.817) | | |

Process private memory with this document: 77 MiB

10 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.057 | 0.106 | 0.568 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.001 | 1.103 | 3.865 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.031 | 0.080 | 0.255 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.068 | 0.841 | 2.420 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.017 | 0.066 | 0.340 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.626 | 2.436 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.190 | 0.354 | 1.042 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.001 | 1.144 | 4.565 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.163 | 0.356 | 0.941 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.909 | 2.231 | 8.767 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.106 | 0.205 | 0.716 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 1.869 | 6.835 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.017 | 0.033 | 0.169 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.619 | 1.496 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.111 | 0.199 | 3.830 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.001 | 0.922 | 2.621 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 2.346 | 3.133 | 3.272 | (next event: median 0.080) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.004 | 0.005 | 0.005 | (next event: median 4.612) | | |

Process private memory with this document: 137 MiB

### 1 layer(s): layer pixels 12 MiB + composite 12 MiB; file 0.01 MiB, recovery file 0.01 MiB

Process private memory added by creating the document: 24 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0146 | 0.0635 | 0.2002 |
| full composite rebuild (visibility/opacity/order change) | 1.12 | 1.70 | 1.71 |
| recovery snapshot on UI thread (shallow copy) | 0.0004 | 0.0007 | 0.0012 |
| first dab on the active layer after a snapshot (copies that layer) | 2.279 | 2.563 | 2.563 |
| same dab without a pending snapshot | 0.0115 | 0.0277 | 0.0298 |
| recovery encode + atomic write (worker thread) | 42.6 | 43.7 | 43.7 |
| save project (encode + atomic write) | 41.6 | 47.2 | 47.2 |
| verify encoded project (checksum + decompress each layer) | 8.9 | 9.6 | 9.6 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 51.6 | 55.0 | 55.0 |
| open project | 17.4 | 18.3 | 18.3 |
| export PNG (composite + encode) | 87.3 | 90.7 | 90.7 |

### 5 layer(s): layer pixels 60 MiB + composite 12 MiB; file 2.06 MiB, recovery file 2.06 MiB

Process private memory added by creating the document: 73 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0164 | 0.0632 | 0.2943 |
| full composite rebuild (visibility/opacity/order change) | 6.07 | 6.92 | 7.43 |
| recovery snapshot on UI thread (shallow copy) | 0.0006 | 0.0014 | 0.0023 |
| first dab on the active layer after a snapshot (copies that layer) | 2.374 | 2.983 | 3.368 |
| same dab without a pending snapshot | 0.0129 | 0.0233 | 0.0240 |
| recovery encode + atomic write (worker thread) | 359.4 | 381.9 | 381.9 |
| save project (encode + atomic write) | 368.5 | 393.1 | 393.1 |
| verify encoded project (checksum + decompress each layer) | 78.7 | 81.3 | 81.3 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 432.1 | 451.6 | 451.6 |
| open project | 117.8 | 120.3 | 120.3 |
| export PNG (composite + encode) | 221.4 | 231.3 | 231.3 |

### 10 layer(s): layer pixels 120 MiB + composite 12 MiB; file 4.60 MiB, recovery file 4.60 MiB

Process private memory added by creating the document: 132 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0175 | 0.0685 | 0.4143 |
| full composite rebuild (visibility/opacity/order change) | 13.00 | 14.12 | 14.40 |
| recovery snapshot on UI thread (shallow copy) | 0.0006 | 0.0009 | 0.0024 |
| first dab on the active layer after a snapshot (copies that layer) | 2.275 | 2.436 | 2.529 |
| same dab without a pending snapshot | 0.0085 | 0.0169 | 0.0225 |
| recovery encode + atomic write (worker thread) | 786.3 | 801.1 | 801.1 |
| save project (encode + atomic write) | 773.4 | 785.4 | 785.4 |
| verify encoded project (checksum + decompress each layer) | 167.7 | 172.5 | 172.5 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 936.0 | 942.0 | 942.0 |
| open project | 252.4 | 257.6 | 257.6 |
| export PNG (composite + encode) | 343.6 | 349.7 | 349.7 |

### View rendering (composite cache drawn through the view; independent of layer count)

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| render full 1920x1080 view, zoom 0.5 | 0.8257 | - | 5.7400 |
| render full 1920x1080 view, zoom 1.0 | 1.0112 | - | 2.5684 |
| render 64x64 dirty rect, zoom 1.0 | 0.0034 | - | 0.0984 |

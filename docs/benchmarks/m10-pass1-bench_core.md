Windows 11 Version 25H2, x86_64; canvas 2048x1536

### Like-for-like single layer (same content as the milestone 2 benchmark)

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event + undo capture (+ composite refresh), brush 12 px | 0.0135 | 0.0385 | 0.8624 |
| save project, painted dabs (0.73 MiB file) | 116.2 | - | 134.5 |
| open project, painted dabs | 36.4 | - | 46.2 |
| export PNG, painted dabs | 195.7 | - | 233.8 |

### Brush engine: one input event of a 1000-event stroke (stroke processing + undo capture + composite refresh)

| Variant | median ms | p95 ms | max ms | coverage tiles |
|---|---|---|---|---|
| milestone 4 path (QPainter dabs), 12 px | 0.0129 | 0.0360 | 0.1500 | - |
| stroke engine, 12 px, 100 %, smoothing off | 0.0160 | 0.0640 | 0.1896 | 94 |
| stroke engine, 12 px, 50 %, smoothing off | 0.0155 | 0.0635 | 0.2216 | 94 |
| stroke engine, 12 px, 50 %, smoothing 20 px | 0.0175 | 0.0642 | 0.1669 | 95 |
| milestone 4 path (QPainter dabs), 128 px | 0.0066 | 0.0832 | 0.3046 | - |
| stroke engine, 128 px, 50 %, smoothing off | 0.0087 | 0.3171 | 0.8105 | 184 |
| smoothing only, string 0 px (no painting) | 0.0000 | 0.0001 | 0.0002 | - |
| smoothing only, string 20 px (no painting) | 0.0003 | 0.0004 | 0.0136 | - |
| smoothing only, string 80 px (no painting) | 0.0003 | 0.0004 | 0.0043 | - |

### Brush presets: one input event of a 1000-event stroke (engine + undo capture + composite refresh)

Coverage tiles are 64 x 64 x 16-bit (8 KiB); the undo snapshot holds the touched 64 x 64 layer tiles.

1 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.059 | 0.113 | 0.261 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 1.014 | 2.291 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.030 | 0.081 | 0.292 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.063 | 0.771 | 1.494 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.015 | 0.062 | 0.163 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.561 | 1.575 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.170 | 0.292 | 0.950 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.001 | 1.098 | 2.741 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.154 | 0.279 | 0.621 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.778 | 1.914 | 3.380 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.106 | 0.181 | 0.438 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 1.743 | 3.150 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.014 | 0.031 | 0.180 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.530 | 1.279 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.094 | 0.172 | 0.697 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.000 | 0.751 | 2.144 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 0.023 | 0.080 | 0.091 | (next event: median 0.070) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.002 | 0.003 | 0.020 | (next event: median 1.699) | | |

Process private memory with this document: 29 MiB

5 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.055 | 0.106 | 0.607 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 1.010 | 2.393 | 307 | 2456 | 4.6 |
| Clean Ink | 8 | 0.029 | 0.077 | 0.506 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.070 | 0.822 | 1.965 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.017 | 0.062 | 0.304 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.573 | 1.556 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.175 | 0.291 | 0.718 | 209 | 1672 | 3.2 |
| Soft Airbrush | 200 | 0.001 | 0.911 | 1.923 | 360 | 2880 | 5.4 |
| Textured Paint | 40 | 0.153 | 0.262 | 0.796 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.954 | 2.391 | 8.986 | 378 | 3024 | 5.7 |
| Blend / Smudge | 36 | 0.123 | 0.234 | 0.673 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 2.032 | 3.006 | 0 | 650 | 4.7 |
| Hard Eraser | 24 | 0.018 | 0.037 | 0.159 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.669 | 1.628 | 273 | 2184 | 4.1 |
| Soft Eraser | 70 | 0.108 | 0.190 | 0.403 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.001 | 0.829 | 2.703 | 359 | 2872 | 5.4 |
| first event after a snapshot, Clean Ink 8 px | | 0.026 | 0.041 | 0.041 | (next event: median 0.073) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.003 | 0.004 | 0.004 | (next event: median 1.850) | | |

Process private memory with this document: 74 MiB

10 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.064 | 0.119 | 0.328 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.001 | 1.250 | 2.376 | 307 | 2456 | 4.6 |
| Clean Ink | 8 | 0.041 | 0.100 | 0.271 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.093 | 1.226 | 3.716 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.022 | 0.074 | 0.232 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.662 | 2.063 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.241 | 0.384 | 0.845 | 209 | 1672 | 3.2 |
| Soft Airbrush | 200 | 0.001 | 1.185 | 2.474 | 360 | 2880 | 5.4 |
| Textured Paint | 40 | 0.187 | 0.328 | 0.795 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 1.083 | 2.689 | 7.194 | 378 | 3024 | 5.7 |
| Blend / Smudge | 36 | 0.136 | 0.251 | 0.960 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 2.188 | 3.743 | 0 | 650 | 4.7 |
| Hard Eraser | 24 | 0.019 | 0.037 | 0.201 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.636 | 1.568 | 273 | 2184 | 4.1 |
| Soft Eraser | 70 | 0.116 | 0.195 | 0.706 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.001 | 1.066 | 3.020 | 359 | 2872 | 5.4 |
| first event after a snapshot, Clean Ink 8 px | | 0.034 | 0.041 | 0.056 | (next event: median 0.079) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.003 | 0.004 | 0.005 | (next event: median 1.996) | | |

Process private memory with this document: 129 MiB

### 1 layer(s): layer pixels 12 MiB + composite 12 MiB; file 0.04 MiB, recovery file 0.04 MiB

Process private memory added by creating the document: 25 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0156 | 0.0639 | 0.3137 |
| full composite rebuild (visibility/opacity/order change) | 2.36 | 3.52 | 3.73 |
| recovery snapshot on UI thread (shallow copy) | 0.0001 | 0.0001 | 0.0003 |
| first dab on the active layer after a snapshot (copies that layer) | 0.010 | 0.016 | 0.073 |
| same dab without a pending snapshot | 0.0055 | 0.0073 | 0.0141 |
| recovery encode + atomic write (worker thread) | 29.5 | 30.1 | 30.1 |
| save project (encode + atomic write) | 29.8 | 31.0 | 31.0 |
| verify encoded project (checksum + decompress each layer) | 10.9 | 11.2 | 11.2 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 42.5 | 43.1 | 43.1 |
| open project | 17.8 | 19.3 | 19.3 |
| export PNG (composite + encode) | 94.9 | 123.0 | 123.0 |

### 5 layer(s): layer pixels 60 MiB + composite 12 MiB; file 2.44 MiB, recovery file 2.44 MiB

Process private memory added by creating the document: 66 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0185 | 0.0697 | 0.2962 |
| full composite rebuild (visibility/opacity/order change) | 7.88 | 9.31 | 10.16 |
| recovery snapshot on UI thread (shallow copy) | 0.0001 | 0.0003 | 0.0013 |
| first dab on the active layer after a snapshot (copies that layer) | 0.010 | 0.018 | 0.049 |
| same dab without a pending snapshot | 0.0059 | 0.0086 | 0.0103 |
| recovery encode + atomic write (worker thread) | 445.7 | 542.9 | 542.9 |
| save project (encode + atomic write) | 420.9 | 511.2 | 511.2 |
| verify encoded project (checksum + decompress each layer) | 87.4 | 89.6 | 89.6 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 521.8 | 525.9 | 525.9 |
| open project | 115.3 | 122.2 | 122.2 |
| export PNG (composite + encode) | 235.5 | 236.1 | 236.1 |

### 10 layer(s): layer pixels 120 MiB + composite 12 MiB; file 5.44 MiB, recovery file 5.44 MiB

Process private memory added by creating the document: 118 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0371 | 0.1574 | 0.2741 |
| full composite rebuild (visibility/opacity/order change) | 18.30 | 26.51 | 36.00 |
| recovery snapshot on UI thread (shallow copy) | 0.0002 | 0.0010 | 0.0035 |
| first dab on the active layer after a snapshot (copies that layer) | 0.012 | 0.051 | 0.067 |
| same dab without a pending snapshot | 0.0067 | 0.0239 | 0.0369 |
| recovery encode + atomic write (worker thread) | 988.8 | 1528.2 | 1528.2 |
| save project (encode + atomic write) | 1135.7 | 1458.6 | 1458.6 |
| verify encoded project (checksum + decompress each layer) | 178.8 | 193.3 | 193.3 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 1142.1 | 1205.7 | 1205.7 |
| open project | 242.7 | 260.7 | 260.7 |
| export PNG (composite + encode) | 407.1 | 509.0 | 509.0 |

### View rendering (composite cache drawn through the view; independent of layer count)

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| render full 1920x1080 view, zoom 0.5 | 6.0565 | - | 23.8134 |
| render full 1920x1080 view, zoom 1.0 | 4.2315 | - | 7.3015 |
| render 64x64 dirty rect, zoom 1.0 | 0.0066 | - | 0.1238 |

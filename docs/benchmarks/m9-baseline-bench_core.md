Windows 11 Version 25H2, x86_64; canvas 2048x1536

### Like-for-like single layer (same content as the milestone 2 benchmark)

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event + undo capture (+ composite refresh), brush 12 px | 0.0070 | 0.0214 | 0.0549 |
| save project, painted dabs (0.63 MiB file) | 86.3 | - | 92.2 |
| open project, painted dabs | 37.0 | - | 41.9 |
| export PNG, painted dabs | 151.9 | - | 160.4 |

### Brush engine: one input event of a 1000-event stroke (stroke processing + undo capture + composite refresh)

| Variant | median ms | p95 ms | max ms | coverage tiles |
|---|---|---|---|---|
| milestone 4 path (QPainter dabs), 12 px | 0.0065 | 0.0223 | 0.1105 | - |
| stroke engine, 12 px, 100 %, smoothing off | 0.0118 | 0.0556 | 0.2125 | 94 |
| stroke engine, 12 px, 50 %, smoothing off | 0.0123 | 0.0530 | 0.2132 | 94 |
| stroke engine, 12 px, 50 %, smoothing 20 px | 0.0124 | 0.0559 | 0.2834 | 95 |
| milestone 4 path (QPainter dabs), 128 px | 0.0034 | 0.0266 | 0.1302 | - |
| stroke engine, 128 px, 50 %, smoothing off | 0.0065 | 0.2496 | 0.8619 | 184 |
| smoothing only, string 0 px (no painting) | 0.0001 | 0.0001 | 0.0002 | - |
| smoothing only, string 20 px (no painting) | 0.0003 | 0.0007 | 0.0397 | - |
| smoothing only, string 80 px (no painting) | 0.0002 | 0.0004 | 0.0026 | - |

### Brush presets: one input event of a 1000-event stroke (engine + undo capture + composite refresh)

Coverage tiles are 64 x 64 x 16-bit (8 KiB); the undo snapshot holds the touched 64 x 64 layer tiles.

1 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.057 | 0.110 | 0.262 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 1.080 | 2.273 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.028 | 0.076 | 0.244 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.054 | 0.652 | 1.575 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.013 | 0.056 | 0.372 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.465 | 1.435 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.142 | 0.263 | 1.036 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.000 | 0.743 | 2.229 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.146 | 0.263 | 0.819 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.762 | 1.941 | 3.451 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.074 | 0.142 | 0.489 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.000 | 1.213 | 2.526 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.012 | 0.024 | 0.179 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.461 | 1.103 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.082 | 0.171 | 0.426 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.000 | 0.712 | 5.913 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 2.237 | 2.584 | 3.316 | (next event: median 0.069) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.003 | 0.005 | 0.011 | (next event: median 3.788) | | |

Process private memory with this document: 28 MiB

5 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.056 | 0.103 | 0.674 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 1.091 | 2.258 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.028 | 0.077 | 0.355 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.057 | 0.685 | 2.045 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.013 | 0.055 | 0.350 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.508 | 1.698 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.162 | 0.271 | 0.754 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.001 | 0.846 | 2.249 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.146 | 0.267 | 0.608 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.819 | 2.075 | 3.766 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.097 | 0.188 | 0.512 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 1.662 | 3.926 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.014 | 0.028 | 0.086 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.491 | 1.434 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.093 | 0.178 | 0.524 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.000 | 0.772 | 1.859 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 2.186 | 2.391 | 2.978 | (next event: median 0.068) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.004 | 0.007 | 0.787 | (next event: median 3.908) | | |

Process private memory with this document: 77 MiB

10 layer(s):

| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |
|---|---|---|---|---|---|---|---|
| Sketch Pencil | 5 | 0.059 | 0.117 | 0.305 | 94 | 752 | 1.5 |
| Sketch Pencil | 200 | 0.000 | 1.246 | 2.819 | 307 | 2456 | 4.8 |
| Clean Ink | 8 | 0.030 | 0.077 | 0.390 | 92 | 736 | 1.4 |
| Clean Ink | 200 | 0.067 | 0.857 | 2.525 | 232 | 1856 | 3.6 |
| Solid Round | 12 | 0.017 | 0.061 | 0.180 | 94 | 752 | 1.5 |
| Solid Round | 200 | 0.000 | 0.600 | 1.911 | 233 | 1864 | 3.6 |
| Soft Airbrush | 90 | 0.200 | 0.370 | 2.120 | 209 | 1672 | 3.3 |
| Soft Airbrush | 200 | 0.001 | 1.005 | 1.979 | 360 | 2880 | 5.6 |
| Textured Paint | 40 | 0.161 | 0.313 | 0.815 | 155 | 1240 | 2.4 |
| Textured Paint | 200 | 0.929 | 2.286 | 4.102 | 378 | 3024 | 5.9 |
| Blend / Smudge | 36 | 0.099 | 0.197 | 0.564 | 0 | 25 | 2.0 |
| Blend / Smudge | 200 | 0.001 | 1.817 | 3.388 | 0 | 650 | 4.9 |
| Hard Eraser | 24 | 0.016 | 0.039 | 0.364 | 110 | 880 | 1.7 |
| Hard Eraser | 200 | 0.000 | 0.646 | 1.839 | 273 | 2184 | 4.3 |
| Soft Eraser | 70 | 0.110 | 0.217 | 0.817 | 187 | 1496 | 2.9 |
| Soft Eraser | 200 | 0.000 | 0.908 | 2.178 | 359 | 2872 | 5.6 |
| first event after a snapshot, Clean Ink 8 px | | 2.291 | 2.818 | 3.228 | (next event: median 0.071) | | |
| first event after a snapshot, Textured Paint 200 px | | 0.003 | 0.006 | 0.007 | (next event: median 4.221) | | |

Process private memory with this document: 136 MiB

### 1 layer(s): layer pixels 12 MiB + composite 12 MiB; file 0.01 MiB, recovery file 0.01 MiB

Process private memory added by creating the document: 24 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0119 | 0.0579 | 0.2903 |
| full composite rebuild (visibility/opacity/order change) | 1.02 | 1.72 | 2.06 |
| recovery snapshot on UI thread (shallow copy) | 0.0002 | 0.0004 | 0.0010 |
| first dab on the active layer after a snapshot (copies that layer) | 2.247 | 2.586 | 3.140 |
| same dab without a pending snapshot | 0.0053 | 0.0088 | 0.0127 |
| recovery encode + atomic write (worker thread) | 39.3 | 40.5 | 40.5 |
| save project (encode + atomic write) | 41.2 | 41.6 | 41.6 |
| verify encoded project (checksum + decompress each layer) | 8.3 | 8.9 | 8.9 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 49.1 | 50.8 | 50.8 |
| open project | 21.2 | 21.5 | 21.5 |
| export PNG (composite + encode) | 83.3 | 85.7 | 85.7 |

### 5 layer(s): layer pixels 60 MiB + composite 12 MiB; file 2.06 MiB, recovery file 2.06 MiB

Process private memory added by creating the document: 72 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0140 | 0.0602 | 0.1517 |
| full composite rebuild (visibility/opacity/order change) | 5.22 | 6.92 | 7.01 |
| recovery snapshot on UI thread (shallow copy) | 0.0006 | 0.0016 | 0.0025 |
| first dab on the active layer after a snapshot (copies that layer) | 2.198 | 2.586 | 3.062 |
| same dab without a pending snapshot | 0.0060 | 0.0110 | 0.0116 |
| recovery encode + atomic write (worker thread) | 361.2 | 366.0 | 366.0 |
| save project (encode + atomic write) | 389.6 | 409.9 | 409.9 |
| verify encoded project (checksum + decompress each layer) | 75.8 | 78.4 | 78.4 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 446.5 | 483.2 | 483.2 |
| open project | 132.6 | 154.2 | 154.2 |
| export PNG (composite + encode) | 219.8 | 223.1 | 223.1 |

### 10 layer(s): layer pixels 120 MiB + composite 12 MiB; file 4.60 MiB, recovery file 4.60 MiB

Process private memory added by creating the document: 132 MiB.

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | 0.0155 | 0.0594 | 0.2254 |
| full composite rebuild (visibility/opacity/order change) | 11.87 | 13.22 | 13.47 |
| recovery snapshot on UI thread (shallow copy) | 0.0007 | 0.0017 | 0.0050 |
| first dab on the active layer after a snapshot (copies that layer) | 2.266 | 2.683 | 3.162 |
| same dab without a pending snapshot | 0.0062 | 0.0123 | 0.0144 |
| recovery encode + atomic write (worker thread) | 755.2 | 764.6 | 764.6 |
| save project (encode + atomic write) | 761.7 | 775.8 | 775.8 |
| verify encoded project (checksum + decompress each layer) | 157.7 | 161.7 | 161.7 |
| app save to a local file (SafeSave: encode + verify + space check + atomic write) | 946.1 | 1006.4 | 1006.4 |
| open project | 261.7 | 306.9 | 306.9 |
| export PNG (composite + encode) | 337.5 | 362.1 | 362.1 |

### View rendering (composite cache drawn through the view; independent of layer count)

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| render full 1920x1080 view, zoom 0.5 | 0.9134 | - | 3.4424 |
| render full 1920x1080 view, zoom 1.0 | 1.0868 | - | 2.6813 |
| render 64x64 dirty rect, zoom 1.0 | 0.0037 | - | 0.1234 |

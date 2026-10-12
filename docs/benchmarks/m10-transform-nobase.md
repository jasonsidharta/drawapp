# Transform session without a base copy (no selection), 2026-10-12

Raw output of `bench_m9 30 transforms` after the change found on the Android emulator (TransformSession keeps no transparent "base" copy when there is no selection). Same machine as the other milestone 10 runs; one run, with the emulator shut down (a first attempt while it ran was refused at 4096 x 4096 by the device-memory guard, as intended).

Windows 11 Version 25H2, x86_64; 30 repetitions; Release build (MinGW, -O2)

### Multi-layer transform: 9 layers of 2048 x 1536 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 70.2 | 72.4 | 72.4 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 226.4 | 254.9 | 254.9 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 25.6 | 28.9 | 28.9 |
| preview after release, merged copy (bicubic) | 69.8 | 73.8 | 73.8 |
| begin with the merged copy (merge + cache, once per transform) | 111.7 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 603.6 | 611.4 | 611.4 |
| apply: commit (exchange tile references + one group undo entry, 108 MiB kept by history) + composite | 18.8 | - | - |
| undo the transform (all layers) + composite | 17.0 | 17.1 | 17.1 |
| redo the transform (all layers) + composite | 18.0 | 19.4 | 19.4 |

Transform session memory 136 MiB (sources and reductions); preview layers 108 MiB of their own tiles; process private memory grew by 368 MiB during the transform; peak private 618 MiB.

### Multi-layer transform: 7 layers of 4096 x 4096 (all but the background), no selection

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| begin (cache each layer's source and reductions) | 359.3 | 359.5 | 359.5 |
| preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite | 819.0 | 1041.5 | 1041.5 |
| preview frame, merged copy (milestone 9): fast render of one merged layer + composite | 148.9 | 238.2 | 238.2 |
| preview after release, merged copy (bicubic) | 378.4 | 391.7 | 391.7 |
| begin with the merged copy (merge + cache, once per transform) | 497.4 | - | - |
| apply: prepare all layers (bicubic; on the worker thread in the app) | 2969.3 | 3099.3 | 3099.3 |
| apply: commit (exchange tile references + one group undo entry, 0 MiB kept by history) + composite | 110.7 | - | - |
| undo the transform (all layers) + composite | 80.3 | 90.2 | 90.2 |
| redo the transform (all layers) + composite | 79.4 | 87.2 | 87.2 |

Transform session memory 583 MiB (sources and reductions); preview layers 447 MiB of their own tiles; process private memory grew by 1563 MiB during the transform; peak private 2594 MiB.

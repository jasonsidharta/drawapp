Windows 11 Version 25H2, x86_64; 15 repetitions; Release build (MinGW, -O2)

| Workload | 64 | 128 | 256 |
|---|---|---|---|
| 3000 dabs r = 12 on 2048 x 1536 (median / p95) | 3.86 / 5.76 ms | 3.94 / 5.98 ms | 3.97 / 5.53 ms |
| 300 dabs r = 100 on 2048 x 1536 | 15.14 / 16.78 ms | 14.95 / 16.34 ms | 14.32 / 15.88 ms |
| memory: 40 strokes (24 px) on 16384^2 (dense would be 1024 MiB) | 333.8 MiB, 21360 tiles | 468.0 MiB, 7488 tiles | 591.5 MiB, 2366 tiles |
| undo: snapshot + one 24 px stroke on a full 2048 x 1536 layer (time; bytes copied) | 1.69 / 2.84 ms; 1.91 MiB | 1.67 / 2.63 ms; 3.19 MiB | 1.57 / 2.50 ms; 5.25 MiB |
| composite of 10 full 2048 x 1536 layers (QPainter per tile) | 17.34 / 22.92 ms | 15.85 / 20.47 ms | 18.64 / 21.59 ms |
| copy a 1920 x 1080 region (view assembly) | 2.35 / 2.59 ms | 2.45 / 3.43 ms | 2.49 / 3.03 ms |
| fromImage of a full 4096 x 4096 image | 28.23 / 30.61 ms | 29.04 / 35.41 ms | 18.69 / 23.68 ms |
| snapshot of a 16384^2 layer + first write (grid detach) | 0.03 / 0.15 ms | 0.01 / 0.02 ms | 0.01 / 0.02 ms |
| memory: 1 px diagonal line across 2048 x 1536 | 0.75 MiB | 1.50 MiB | 3.00 MiB |

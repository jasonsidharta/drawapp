# Heavy workloads re-run with the device memory guard (milestone 10, 2026-10-12)

The first run of these sections (`m10-only-workloads.md`) happened with about 0.9 GB of physical memory free (other programs held the rest of 15.4 GB) and ran into paging: one 8192² x 10 save took 3 815 806 ms. After the guard (`MemoryBudget::physicalFloor`) was added they were run again; free physical memory before each run is noted.

## 8192 x 8192, 10 layers (4949 MB free before the run)


### 8192 x 8192, 10 layer(s)

Dense equivalent: 2560 MiB of layers + 256 MiB composite. Allocated tiles: layers 2136.3 MiB, composite 256.0 MiB. Private memory added by the document: 2417 MiB (process 2419 MiB, working set 2422 MiB).

| Operation | median ms | p95 ms | max ms |
|---|---|---|---|
| create the test document (setup, not an app operation) | 28380 | - | - |
| full composite rebuild | 261.39 | 321.54 | 321.54 |
| render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path) | 99.587 | 121.066 | 121.066 |
| render the view without the pyramid (100 %) | 3.819 | 5.457 | 5.457 |
| render the view as the canvas does (fit, level 2 of the pyramid), first frame (builds the level) | 171.4 | - | - |
| render the view as the canvas does (fit), later frames | 1.907 | 5.562 | 5.562 |
| render the view as the canvas does (100 %), later frames | 1.082 | 1.372 | 1.372 |
| render the view as the canvas does (fit), while panning 7 px per frame | 1.993 | 2.932 | 2.932 |
| render the view as the canvas does (5 %) | 0.589 | 11.116 | 11.116 |
| (view pyramid memory after these frames: 85.0 MiB) | - | - | - |
| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), 9095 dabs | 0.0709 | 0.1205 | 0.2322 |
| undo that stroke (6.1 MiB of tiles) | 0.01 | 0.05 | 0.05 |
| redo that stroke | 0.01 | 0.02 | 0.02 |
| stroke input event, Textured Paint 40 px, 15258 dabs | 0.8464 | 1.9118 | 3.4744 |
| save project (encode + atomic write) | 5700.8 | 5700.8 | 5700.8 |
| open project (read + verify + decode) | 2525.4 | 2525.4 | 2525.4 |
| recovery: snapshot on the UI thread | 0.0101 | 0.0343 | 0.0343 |
| recovery: encode (worker) | 4781.3 | 4781.3 | 4781.3 |

File 42.9 MiB. Process peak private memory 4187 MiB, peak working set 4163 MiB. MemoryBudget peak 4100 MiB.

## 19 layers of 4096² transformed (5395 MB free before the run; bench fixed to check refusals)

Result: begin 1379.8 ms; per-layer preview frame 2872.7 ms (median); the merged copy and Apply were **refused** by the device-memory guard (508 and 473 MiB left), nothing changed, no paging. The previous run of this section without the check crashed in the benchmark itself (it ignored the refusal).

# Performance hypotheses for milestone 10

Each hypothesis is stated before measuring, with the measurement that tests it. The outcomes are
recorded in `docs/m10-performance.md`, including the hypotheses that turned out false.

Baseline: milestone 9, reproduced on 2026-10-12 before any change
(`docs/benchmarks/m10-baseline-*.md`):
- 337 test cases pass;
- first frame 155–201 ms; idle working set 89.1–90.4 MiB;
- 9-layer 2048 × 1536 merged preview 24.8 ms;
- 7-layer 4096² preview 242 ms and Apply 2.8 s;
- 4096² × 20 refused.

| # | Hypothesis | Why we expect it | How it is measured | Would falsify |
|---|---|---|---|---|
| H1 | A tile size of 64 is the best default; 128 and 256 win only on whole-canvas passes | brush dabs and undo touch small areas, and per-tile overhead is small at 64 (Krita, libmypaint use 64) | storage benchmark: random dabs, strokes, full composite, sparse memory, snapshot and undo capture for T = 64/128/256 | 128 or 256 clearly faster on brush/undo with similar memory |
| H2 | A mostly empty large document costs memory roughly proportional to its painted area | unallocated tiles are null | live tile bytes and process private memory for 8192² and 16 384² documents with sparse strokes vs. their dense size | memory near the dense size |
| H3 | Snapshots (recovery, transform begin, worker inputs) become O(1) and undo capture stops copying pixels | the table is shared, and capture holds references | time and bytes of `snapshot()` and of a stroke's undo capture before vs. after | capture as slow as copying |
| H4 | Async Apply reduces the maximum UI-thread block of a 7 × 4096² transform from 2.3–3.2 s to under 100 ms | the computation moves to a worker; commit swaps pointers | UI-thread time of Apply start + commit, and the worker's wall-clock time, in the benchmark and in `tst_canvas` | commit ≥ 100 ms |
| H5 | Composite rebuilds cost about the same for full documents and much less for sparse ones | null tiles skip blending | full composite rebuild, baseline vs. tiled, per workload | tiled rebuild > 1.2× baseline on full documents |
| H6 | Pan and zoom frame time stops depending on canvas size | the view assembles only the visible area from a pyramid level | render time at fit and 100 % zoom for 512² … 16 384² | fit-zoom render grows with the canvas size |
| H7 | Reduced-resolution transform previews reach ≥ 30 fps (≤ 33 ms) for 7 × 4096² at fit zoom | 1/4 resolution means about 1/16 of the pixels | preview frame time at fit zoom, merged and per layer | > 33 ms |
| H8 | A dab cache is not needed | jitter and dynamics change every dab; per-event costs are already 0.02–0.2 ms | per-event brush costs before/after (no cache built) | a brush exceeds 2 ms per event at 2048 × 1536 |
| H9 | Brushes on tiled layers cost the same as on dense images within ±10 % | row pointers per tile segment replace scan lines | `bench_core 30 presets` and `bench_m9 brushes` before/after | > 10 % slower median on ordinary presets |
| H10 | Saving and loading sparse documents gets much faster; full documents stay about the same | only occupied tiles are encoded | save/open per workload, before/after | slower on full documents by > 20 % |
| H11 | Idle memory stays within about 3 MiB of the baseline | small documents allocate only the tiles they use | `DRAWAPP_STARTUP_PROBE`, 5 runs | > 3 MiB above |

Measurement rules:
- Same machine, Release build, same generated documents and input traces.
- Median and p95 with the sample count.
- Processing times are not called latency.
- Private bytes (commit), working set and live tile bytes are reported separately and never
  compared with each other.

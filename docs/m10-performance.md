# Milestone 10: performance and memory

All numbers are CPU processing time or process memory on one machine: Intel Core Ultra 7 155H
(16 cores / 22 threads), 15.4 GB RAM, Windows 11 25H2, MinGW 15.1 release builds, 2026-10-12. Not
latency to the screen, not a phone. Raw output is in `docs/benchmarks/`; every table here can be
traced to a file there.

## Method

- **Workloads** (`tests/bench_m9.cpp`, the milestone 9 benchmark program extended): the
  milestone 9 content generator (a white background and layers of brush dabs scaled to the
  area), plus a sparse workload (a few long strokes per layer on 16384²).
- **Milestone 9 vs milestone 10 on equal terms.** Run-to-run variance on this laptop is large
  (thermal and power state: the same milestone 9 render took 317 ms in one run and 547 ms in the
  next). So milestone 9's own benchmark (commit 7469a69, built from a separate worktree) and
  milestone 10's were run **alternately, section by section, three rounds, in the same session**,
  20 repetitions, one process per section (`docs/benchmarks/m10-ab-raw.md`). Tables show the
  median of the three per-round medians; the comparison file lists every run.
- **What does not compare.** Milestone 9 refused documents over 512 MiB of dense layers, so
  4096² × 20, 8192² × 10 and 16384² have no milestone 9 numbers. The view rows changed meaning:
  milestone 9 drew straight from a dense image; milestone 10 draws through the view pyramid and a
  persistent view buffer.
- **Process.** A first pass found regressions (`m10-pass1-*`); they were fixed and measured
  again; the earlier numbers are kept, not replaced.

## Milestone 9 vs milestone 10 (same session, interleaved)

From `docs/benchmarks/m10-ab-comparison.md`; ratio = milestone 10 / milestone 9 (below 1 is
faster).

| Workload | Operation | M9 (ms) | M10 (ms) | Ratio |
|---|---|---|---|---|
| 2048 × 1536 × 10 | stroke event, Solid Round 24 px (median) | 0.020 | 0.019 | 0.99 |
| 2048 × 1536 × 10 | stroke event, Textured Paint 40 px (median) | 0.163 | 0.171 | 1.05 |
| 2048 × 1536 × 10 | undo / redo of that stroke | 0.17 / 0.17 | < 0.01 | ≈ 0 |
| 2048 × 1536 × 10 | full composite rebuild | 12.85 | 14.95 | 1.16 |
| 2048 × 1536 × 10 | view at fit, later frames | 1.54 | 1.74 | 1.14 |
| 2048 × 1536 × 10 | save (encode, verify, write) | 824 | 239 | 0.29 |
| 2048 × 1536 × 10 | open | 248 | 186 | 0.75 |
| 4096² × 8 | stroke event, Solid Round (median) | 0.037 | 0.036 | 0.98 |
| 4096² × 8 | undo of that stroke | 0.96 | 0.01 | 0.01 |
| 4096² × 8 | full composite rebuild | 62.7 | 53.4 | 0.85 |
| 4096² × 8 | view at fit, later frames | 1.64 | 1.49 | 0.91 |
| 4096² × 8 | save / open | 2751 / 970 | 794 / 630 | 0.29 / 0.65 |
| 8192² × 2 | stroke event, Textured Paint (median) | 0.729 | 0.733 | 1.01 |
| 8192² × 2 | undo of a stroke | 2.39 | 0.02 | 0.01 |
| 8192² × 2 | save / open | 2132 / 861 | 857 / 616 | 0.40 / 0.72 |
| 512² × 3 | full composite rebuild | 0.32 | 0.40 | 1.25 |
| 512² × 3 | open | 18.6 | 21.6 | 1.16 |
| brushes (18 engine cases, 2048 × 1536 × 10) | one input event | 0.000–0.513 | 0.000–0.537 | 0.60–1.09 |
| long strokes, 10 000 events | Solid Round / Textured / Smudge / Wet | 204 / 1909 / 1270 / 632 | 223 / 2005 / 1355 / 634 | 1.09 / 1.05 / 1.07 / 1.00 |
| 9 × 2048 × 1536 transform | begin | 104 | 118 | 1.14 |
| 9 × 2048 × 1536 transform | preview frame, per layer | 192 | 253 | 1.31 |
| 9 × 2048 × 1536 transform | preview frame, merged copy | 24.5 | 27.4 | 1.12 |
| 9 × 2048 × 1536 transform | Apply: prepare + commit | 654 + 168 | 802 + 37 | 1.02 total |
| 9 × 2048 × 1536 transform | undo / redo | 49 / 50 | 28 / 27 | 0.56 / 0.54 |
| 7 × 4096² transform | begin | 433 | 476 | 1.10 |
| 7 × 4096² transform | preview frame, per layer | 840 | 1056 | 1.26 |
| 7 × 4096² transform | preview frame, merged copy | 154 | 155 | 1.01 |
| 7 × 4096² transform | Apply: prepare + commit | 2613 + 731 | 3049 + 116 | 0.95 total |

Memory (whole process, same runs):

| Workload | Peak private M9 | Peak private M10 | File M9 | File M10 |
|---|---|---|---|---|
| 512² × 3 | 29 MiB | 31 MiB | 0.1 MiB | 0.1 MiB |
| 2048 × 1536 × 10 | 310 MiB | 290 MiB | 4.8 MiB | 5.8 MiB |
| 4096² × 8 | 1267 MiB | 1131 MiB | 10.3 MiB | 12.8 MiB |
| 8192² × 2 | 1833 MiB | 1226 MiB | 4.6 MiB | 5.6 MiB |

### What this shows

- **Brushes and strokes: no regression** beyond noise (ratios 0.98–1.09 per event; long strokes
  1.00–1.09). The pixels are identical (tests).
- **Faster:** save (0.29–0.40×, tiles compressed in parallel), open of large files (0.65–0.75×),
  undo/redo of strokes (from 0.17–2.4 ms to ≤ 0.02 ms), transform commit (0.16–0.22×) and undo
  of a transform at 2048 × 1536 (0.55×), composite rebuild at 4096² (0.85×), peak memory at
  8192² × 2 (−33 %).
- **Slower (remaining regressions):** the per-layer transform preview (1.26–1.31×) and its
  merged variant at 2048 × 1536 (1.12×); transform begin (1.10–1.14×); full composite rebuild of
  small documents (1.16–1.25×: 2.1 ms more at 2048 × 1536); opening very small files (1.16×,
  3 ms); files are 20–25 % larger for these fully painted workloads (per-tile compression). Apply
  as a whole is at parity (0.95–1.02×) but now runs off the UI thread.
- **Not met:** a 30 frames-per-second transform preview at 4096² × 7. The merged preview frame
  takes 155 ms (about 6 fps) and the per-layer preview about 1 s, the same order as milestone 9.
  A reduced-resolution preview was not built (`docs/m10-known-limitations.md`).

### After the A/B runs: transform without a base copy

On the Android emulator a five-layer 4096² transform kept 868 of its 980 MiB, so Apply was then
refused (`BUILD.md`, milestone 10 emulator results). Without a selection the session kept, per
layer, a "base" (layer minus source) that is entirely transparent; it is no longer kept. One run of
the transform section afterwards (`docs/benchmarks/m10-transform-nobase.md`; the emulator shut
down), against the milestone 10 medians above:

| Workload | Session memory before → after | begin before → after (ms) |
|---|---|---|
| 9 × 2048 × 1536, no selection | 238 → 136 MiB | 118 → 70 |
| 7 × 4096², no selection | 1021 → 583 MiB | 476 → 359 |

The memory saving follows from the change (one dense copy per layer instead of two, plus the
reductions); the begin times are from a single run and were not re-measured interleaved with
milestone 9, so the "begin 1.10–1.14×" regression above is probably gone but is not claimed as
measured. With a selection nothing changed (the remainder must stay behind).

## Workloads only milestone 10 can open

`docs/benchmarks/m10-only-workloads.md` and `m10-heavy-rerun.md`.

| Workload | Layer tiles (dense equivalent) | Stroke event, Solid Round (median) | View at fit, later frames | Save / open | Peak private |
|---|---|---|---|---|---|
| 512² × 1 | 1.0 MiB (1 MiB) | 0.008 ms | 0.50 ms | 7.4 / 8.5 ms | 27 MiB |
| 4096² × 20 | 1261 MiB (1280 MiB) | 0.049 ms | 1.53 ms | 2.4 / 1.7 s | 2704 MiB |
| 8192² × 10 (re-run, 4.9 GB free) | 2136 MiB (2560 MiB) | 0.071 ms | 1.91 ms | 5.7 / 2.5 s | 4187 MiB |
| 16384² × 4, sparse | 54.6 MiB (4096 MiB) | 0.116 ms | 2.30 ms | 0.37 / 0.10 s | 1153 MiB* |

\* The sparse document itself takes 117 MiB (layers and composite); the peak comes from the
benchmark's own "view without the pyramid" row, which flattens the whole visible canvas (1 GiB)
and is not what the canvas does.

Zoomed out, the first frame builds the pyramid level: 40 ms (4096², level 1), 171 ms (8192², level
2), 84 ms (16384² sparse, level 3); later frames and pans take 1.4–2.4 ms.

**Memory pressure.** With other programs holding most of the RAM (0.9 GB free), the 8192² × 10
workload paged heavily (one save took 63 minutes) although DrawApp was within its own limit.
This led to the device-memory guard (`docs/m10-memory-budget.md`). Re-run with 4.9 GB free:
normal times, no paging. The 19-layer 4096² transform (sources alone 2.8 GB) is now refused at
the merged copy and at Apply with "the device's free memory (473 MiB left)", nothing changed,
instead of paging; with more free memory it would run (begin 1.4 s, per-layer preview 2.9 s).

## Start-up

`docs/benchmarks/m10-startup.txt` (5 runs, offscreen, `DRAWAPP_STARTUP_PROBE=1`) against
`m10-baseline-startup.txt`:

| | Milestone 9 baseline | Milestone 10 |
|---|---|---|
| first frame | 154.5–200.6 ms | 152.1–171.6 ms |
| idle private memory | 75.4–76.0 MiB | 55.5–56.0 MiB |
| idle working set | 89.5–90.4 MiB | 69.8–70.2 MiB |

The 20 MiB less is the blank 2048 × 1536 layer and composite, no longer allocated.

## File-open memory (the milestone 9 claim)

`docs/benchmarks/m10-open-memory.md`: opening alone, fresh process, builds of eb47f39 (before the
milestone 9 fix), 7469a69 and milestone 10: 1033.6 / 7.7 / 7.6 MiB peak private for a 512² × 3
project and 1076.5 / 50.6 / 26.5 MiB for a 2048 × 1536 project. The fix was real; "29 MiB" was a
whole-benchmark peak, not opening alone.

## Hypotheses (`research/m10-performance-hypotheses.md`) and outcomes

Each hypothesis was written before the implementation with a result that would falsify it.

| | Hypothesis | Outcome |
|---|---|---|
| H1 | 64-pixel tiles are the best default | **Confirmed** (`m10-bench_tiles.md`): times within 10 % of 128/256, memory 334 vs 468/592 MiB |
| H2 | Sparse documents cost memory in proportion to the painted area | **Confirmed**: 16384² × 4 sparse, 54.6 MiB of layer tiles vs 4096 MiB dense |
| H3 | Snapshots O(1), undo capture stops copying | **Confirmed**: recovery snapshot ≤ 0.01 ms; stroke undo/redo ≤ 0.02 ms (was 0.17–2.4 ms) |
| H4 | Apply blocks the UI thread under 100 ms at 7 × 4096² (was 2.3–3.2 s) | **Narrowly not met**: the commit on the UI thread takes 89.6–120 ms (median 116 ms); the 3 s of rendering moved to the worker |
| H5 | Full-document composite rebuild within 1.2× | **Mostly**: 1.16× (2048 × 1536), 0.85× (4096²), 1.08× (8192²), but 1.25× at 512² × 3 (0.08 ms more) |
| H6 | Pan/zoom frame time stops depending on canvas size | **Confirmed for later frames**: 0.4–2.3 ms from 512² to 16384²; the first frame at a new level grows with size (up to 171 ms at 8192²) |
| H7 | Reduced-resolution preview ≥ 30 fps at 7 × 4096² | **Not met, not built**: merged preview 155 ms, per-layer about 1 s |
| H8 | No dab cache needed (no event above 2 ms at 2048 × 1536) | **Medians and p95 yes, single events no**: largest median 0.54 ms, largest p95 0.89 ms; but each run had 1–2 isolated events of 2.1–4.5 ms, as milestone 9 had in the same session (1–3 per run, up to 4.2 ms), which points at scheduling noise rather than the engine. No cache was built |
| H9 | Brushes on tiles within ±10 % | **Confirmed**: 0.60–1.09× per event across 18 cases |
| H10 | Sparse saves faster, full ones not more than 20 % slower | **Confirmed and better**: full documents save 0.29–0.40× and open 0.65–0.75× |
| H11 | Idle memory within 3 MiB of the baseline | **Confirmed**: 20 MiB lower |

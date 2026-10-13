# Milestone 11 performance

Measured 2026-10-13 on the development machine: Intel Core Ultra 7 155H, 15.4 GB RAM, Windows 11
25H2, MinGW release build (-O2). The desktop was in use: a browser and the editor were open, with
about 3 GB of RAM free, and no Codex build or benchmark was running (checked before each run).
All times are CPU processing time on the calling thread. They are **not** input-to-photon latency
and **not** physical-device or emulator performance.

## Method

`tests/bench_m11.cpp` builds against both trees from one source:
- the milestone 10 baseline (`dcde383`, built in a scratch worktree);
- milestone 11, where `DRAWAPP_M11` adds the new engines.

Per workload, a fresh process builds the document as bench_m9 does. It then draws a 300-event
wavy stroke (4 ms apart, simulated pressure, 0.8 × the canvas width, at most 1600 px) with every
brush. Each brush runs at 12, 60 and 200 px (the milestone 10 presets), or at its preset size and
200 px (the milestone 11 presets).

A **stroke event** is what the canvas does per input event: the engine call plus the composite
refresh of the changed area. For each brush the benchmark records:
- event median, p95 and maximum (the maximum includes pen-up);
- time from pen-down to the first changed pixel;
- 64 × 64 tiles changed per event;
- stroke-buffer and undo-snapshot memory, and undo time.

After the brushes, it measures:
- long strokes of 2000 events;
- watercolour on a canvas-owned wet layer, without and with the canvas time budgets;
- save and reopen of the document;
- the process's peak private memory.

The A/B ran the two builds alternately, three rounds per workload, in one session. Each figure
below is the median of the per-round medians (`tools/m11_ab_compare.py`).

| Workload | Layers | Notes |
|---|---|---|
| 2048 × 1536 | 10 | painted layers (milestone 9 workload) |
| 4096² | 8 | painted layers (milestone 9 limit) |
| 8192² | 2 | sparse: a few long strokes per layer |
| 16384² | 2 | sparse |

Raw output is in `docs/benchmarks/`:
- `m11-pass1-ab-raw.md` and `m11-pass1-ab-comparison.md`: before the stage 10 fixes;
- `m11-pass2-ab-raw.md` and `m11-pass2-ab-comparison.md`: after the procedural fix;
- `m11-final-raw.md`: milestone 11 only, after all fixes.

Reproduce (after building `bench_m11` in both trees):

```
bench_m11 doc2048 300      # and doc4096, sparse8192, sparse16384; alternate the two builds
python tools/m11_ab_compare.py <directory with <section>-<m10|m11>-r<round>.md>
```

## Milestone 10 presets: A/B

The milestone 10 presets render the same pixels as before
(`tst_brushengine::milestone10EngineReference`, 330 exact renders). The question is whether the
refactor made them slower.

Geometric mean over the 22 rows per workload of (milestone 11 ÷ milestone 10). The 8 presets ×
3 sizes give 24 rows; the 2 rows whose median is under 0.01 ms (no dab in most events) are left
out.

| Workload | Event median, pass 1 | Event median, pass 2 | p95, pass 1 | p95, pass 2 |
|---|---|---|---|---|
| 2048 × 1536 × 10 | 1.08 | 1.10 | 1.14 | 1.09 |
| 4096² × 8 | 1.01 | 1.15 | 1.01 | 1.13 |
| sparse 8192² | 0.95 | 0.95 | 0.94 | 0.91 |
| sparse 16384² | 0.93 | 0.97 | 0.93 | 0.95 |

Individual rows range from 0.53 to 1.96, and the same row changes between passes: Textured Paint
200 px was 0.96 in pass 1 and 1.41 in pass 2; Hard Eraser 60 px was 0.79 and 1.96. The
measurement noise on this busy desktop is about ±40 % per row.

**Reading:** no material regression is shown. A small one, up to about 10–15 % per event on
dense documents, cannot be ruled out: the dense workloads lean above 1 in both passes, the sparse
ones below. In absolute terms the milestone 10 presets stay under about 0.2 ms median and 2 ms
p95 at 12 and 60 px.

What else stayed the same:
- the time to the first pixel;
- tiles changed per event;
- undo-snapshot sizes (identical);
- peak private memory (274 / 1115 / 70 / 120 MiB);
- file sizes.

Long strokes (2000 events) were 0.75–1.26 × the milestone 10 time. Save and open times were
within the noise. One exception: on sparse 8192² the milestone 11 process saved more slowly
(1.22–1.69 ×) after running the extra engine sections, and its peak private memory was 86 vs
70 MiB for the same reason. It is the same document.

## Milestone 11 engines (final run)

Stroke event in ms, p95, on the four workloads (min–max over them). At the preset size:

| Brush (size) | Median | p95 |
|---|---|---|
| Graphite HB (4 px) | 0.22–0.44 | 0.57–0.85 |
| Compressed Charcoal (16) | 0.20–0.38 | 0.58–0.85 |
| Soft Pastel (30) | 0.31–1.01 | 0.70–1.68 |
| G-Pen (10) | 0.17–0.41 | 0.40–0.99 |
| Brush Pen (18) | 0.17–0.43 | 0.40–0.94 |
| Dry Ink (9) | 0.24–0.51 | 0.45–1.05 |
| Oil Blender (40) | 0.61–1.88 | 1.14–3.36 |
| Colour Mixer (32) | 0.67–1.45 | 1.45–2.63 |
| Smear Knife (36) | 0.31–0.73 | 0.77–1.13 |
| Round Bristle (34) | 0.30–0.53 | 0.79–1.36 |
| Flat Bristle (44) | 0.25–0.34 | 0.55–0.79 |
| Dry Brush (48) | 0.20–0.38 | 0.75–1.07 |
| Hair (30) | 0.29–0.56 | 0.56–0.87 |
| Fur (24) | 0.17–0.28 | 0.30–0.44 |
| Grass (40) | 0.26–0.29 | 0.47–0.52 |
| Foliage (50) | 0.51–0.57 | 1.71–2.11 |
| Splatter (80) | ≈ 0 | 0.14–0.19 |
| Stitches (14), Chevron Trim (16) | ≈ 0.02 | 0.05–0.07 |
| Crosshatch Pen (60) | 0.43–0.44 | 0.91–1.22 |
| Airbrush Spray (80) | 0.21–0.30 | 0.44–0.56 |

At 200 px:
- the dry media reach p95 5–14 ms;
- the mixing brushes reach 4–16 ms (the milestone 10 smudge is in the same range: 5–8 ms);
- bristles reach 1.0–2.2 ms;
- the procedural brushes reach 0.2–8 ms.

The maximum includes the live end taper's rebuild at pen-up: up to 55 ms for Dry Ink at 200 px
(`m11-final-raw.md`).

### Watercolour

With the canvas budgets (`CanvasItemWet.cpp`): each stroke event steps for at most 4 ms, and the
drying timer runs at 20 Hz with at most 8 ms of stepping per tick. Time that does not fit is
dropped, so the paint dries more slowly instead of stalling. The test case is Watercolour Wash
50 px on a 1600 × 240 px stroke region (79 wet tiles):

| Workload | Stroke event median / p95 | Drying tick median / p95 | Drying (timer / simulated) |
|---|---|---|---|
| 2048 × 1536 × 10 | 3.0 / 22.2 ms | 36.5 / 41.9 ms | 11.8 s / 8.9 s |
| 4096² × 8 | 3.7 / 21.1 ms | 31.1 / 37.1 ms | 13.2 s / 8.9 s |
| sparse 8192² | 2.9 / 12.4 ms | 22.5 / 27.4 ms | 12.8 s / 8.9 s |
| sparse 16384² | 3.1 / 15.1 ms | 22.6 / 26.8 ms | 13.1 s / 8.9 s |

A drying tick on 2048 × 1536 × 10 breaks down into:
- stepping: 9.8 ms (the budget);
- computing the wet tiles' pixels: 8.5 ms (8 threads);
- the document's composite refresh: 16.8 ms. This is the milestone 10 composite path, unchanged
  here.

Watercolour Round 16 px: stroke p95 7.6–17.8 ms, ticks 12.7–27.4 ms, drying in 2.5 s for 2.4 s
simulated.

A stroke or preview without a canvas wet layer (the scratchpad, thumbnails, the brush sheet)
dries at pen-up with bounded work. For the 1600 px test stroke that took 0.4–1.9 s, down from
0.6–31 s before the fixes.

## Stage 10 changes, measured (doc2048, before → after)

| Change | Measure | Before | After |
|---|---|---|---|
| Procedural: capture and recomposite only the parts' boxes; scratch cleared only on touched spans | Hair 200 px p95 | 20.3 ms | 1.6–5.5 ms |
| Same change | Foliage 50 px p95 | 1.7 ms | 1.7–2.1 ms (unchanged) |
| Watercolour: dormant dry tiles, interior fast paths, per-tile settling-rate cache, both passes on up to 8 threads, pixels on up to 8 threads | Large wash, drying tick (2 steps) | 78 ms | 36 ms |
| Same change | Stroke event p95 | 54 ms | 27 ms |
| Watercolour canvas budgets (4 ms per event, 8 ms per 50 ms tick) | UI-thread time per tick | unbounded: 78 ms every 33 ms | about 36 ms every 50 ms |
| Bounded preview drying (30 M cell-steps, then settle) | Pen-up, Wash 200 px, own wet layer | 30.6 s | 1.3 s |

All of these give the same pixels as before, except two: budgeted canvas stepping (by design it
may fall behind), and previews that hit the work cap (they settle early). Exactness is checked by
the watercolour and procedural tests and goldens, unchanged, and by the milestone 10 reference.

## Hypotheses (research/m11-paint-simulation.md)

| | Criterion (falsified if) | Result |
|---|---|---|
| H-B1 | bristle median above 2 ms at default size | **Holds:** median 0.30–0.53 ms at 34 px (60 bristles, not the ≈ 200 assumed), 0.71–1.18 ms at 200 px |
| H-W1 | watercolour median event above 16 ms at default size | **Holds by its criterion:** median 2.2–3.8 ms. But p95 reaches 22 ms on dense 10-layer documents, and drying ticks take 22–37 ms of every 50 ms on a large wash |
| H-C1 | rasterisation over 50 % of a procedural event | **Not profiled.** Recompositing the whole reach was the cost (fixed above); no shape cache was added |
| H-R1 | any existing golden or exact pixel test changes | **Holds:** 330 exact renders identical, no legacy golden rewritten |

## Not measured

- Input-to-display latency.
- Frame rate on screen (scene-graph upload).
- Physical Android or iOS devices. Android was checked on an emulator, which is not a
  performance measure.
- Real pen pressure: the pressure here is simulated.
- Krita and Procreate side by side (not installed; see the milestone 9 procedure).
- Battery.
- The small-dab overhead on dense documents is measured but not profiled: its cause is not
  isolated.

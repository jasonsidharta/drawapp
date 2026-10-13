# PhotoCraft painting code (milestone 11 research)

Static source reading only. PhotoCraft is Rust and no Rust toolchain is installed here, so nothing
was compiled or run, and no runtime behaviour is claimed.

## Revision and licence

| Item | Value |
|---|---|
| Repository | `https://github.com/storytold/photocraft` |
| Revision read | `20cb95bc272feb9691537bccb46e84e583f5a04a` (2026-10-12), local checkout `C:\Users\Asus\OneDrive\Documents\cp lagi\photocraft` |
| Licence | `MIT OR Apache-2.0` (`Cargo.toml:9`, `LICENSE-MIT`, `LICENSE-APACHE`, `NOTICE`) |
| Exceptions | ArtCraft name, wordmark and logos in `docs/brand/` are trademarks, not open source (`NOTICE`); bundled fonts under the OFL (`NOTICE`, `ATTRIBUTION.md`) |

The licence would permit adapting code with the MIT notice kept. **No PhotoCraft code is copied
into DrawApp in milestone 11**: the ideas below are re-expressed in DrawApp's own C++ and data
model. If that changes, `research/m11-licenses.md` says which notice is required.

## Paint crate layout

`crates/paint/src`: `brush.rs` (settings), `dynamics.rs`, `render.rs` (dab rasterisation,
coverage map, stroke renderer), `mixer.rs` (Mixer Brush), `procedural.rs` (generated tips and
patterns), `retouch.rs` (dab-stroke driver for tools that edit pixels), `tile.rs`, `presets.rs`
and `brush_tests.rs`.

## 1. Dab deposition and coverage

**Where:** `render.rs`. `CoverageMap` (line 391) is a hash map of coverage tiles with optional
per-pixel colour and an optional "depth" channel; `accumulate` (line 421); `StrokeRenderer`
(line 532); `raster_pending` (around line 607).

**Observed:**
- The design matches DrawApp's milestone 6: coverage accumulates per stroke and is composited
  once.
- Flow builds up towards the dab's ceiling (`c ← c + v·(ceil − c)`, the doc comment at line 418).
- Per-dab colour is optional.
- Tiles the dab does not touch are skipped.
- Dabs are buffered and rasterised in batches (`raster_pending`).

**For DrawApp:** confirms the coverage-and-ceiling model DrawApp already uses; nothing to change.

## 2. Wet Edges

**Where:** `render.rs:76` (`wet_factor`), `render.rs:208`, comment at about line 612.

**Observed:**
- `wet_factor(rn) = 0.5 + 0.5 · smoothstep(0.5, 1.0, rn)`, where `rn` is how deep inside the
  nearest dab a pixel lies (0 at the centre, 1 at the rim). The interior keeps half the paint and
  the rim all of it.
- Since an issue fix (the comment cites #2088), the depth is recorded per stroke, so the stroke as
  a whole gets a darker rim rather than every dab.

**For DrawApp:** this is a **static** edge-darkening effect, not a simulation. It is useful as a
cheap "wet edges" property for ordinary brushes, and DrawApp's registry already plans a
"wet edges" property. It is explicitly **not** what the milestone 11 watercolour engine may claim
to be (specification §8).

## 3. Mixer Brush (reservoir and pickup)

**Where:** `mixer.rs` (240 lines). The model, in its own words: "a reservoir of loaded paint plus
a pickup well that mixes in canvas colour".

**Observed:**
- Settings: `wet`, `load`, `mix`, `flow`, `sample_all_layers` (all 0..1).
- Each dab samples the canvas colour under its footprint, alpha-weighted. The **pickup** colour
  moves towards it by `PICKUP_RATE (0.25) × wet × coverage alpha`.
- The deposited colour mixes the reservoir R, with weight `amount × (1 − mix·wet)`, and the pickup
  P, with weight `mix·wet`. It lands at `flow × total weight`.
- The reservoir drains per dab by `amount *= 1 − 0.08 × (1 − load)`, so a low load dries out.
- `MixerState` (reservoir, amount, pickup) is **session tool state** that persists **between
  strokes** ("Paint carried between strokes").
- A dry brush (`wet = 0`) paints only reservoir colour; an empty wet brush smears.

**For DrawApp:** clarifies the four behaviours the specification asks to keep distinct (§9):
- *smearing*: moving existing pixels;
- *mixing*: one picked-up colour blended with the brush colour;
- *depositing*: reservoir paint;
- *blending wet media*.

DrawApp's smudge carries a pixel patch (smearing). Milestone 11 adds a separate single-colour
pickup ("mix") and a draining reservoir to its mixing engine. It does **not** keep paint on the
brush between strokes: a stroke's result must depend only on its own input and the canvas, so
replaying a stroke (tests, recovery) gives the same pixels. This is a deliberate difference.

## 4. Procedural tips and patterns

**Where:** `procedural.rs`. Tileable value noise and fBm (`value_noise`, `fbm`), generated
patterns, and generated tips: `chalk_tip` (line 76), `spatter_tip` (91), `bristle_tip` (112),
`charcoal_tip` (128), `leaf_tip` (148), `grass_tip` (164), `sponge_tip`, `star_tip`, `rake_tip`.

**Observed:**
- Everything is generated from seeded hashes, so "no bundled brush files" (the file header).
- Examples:
  - *charcoal:* a squat ellipse of broken horizontal streaks with a gritty edge;
  - *chalk:* a noisy rim with grain;
  - *spatter:* droplets of power-distributed size.

**For DrawApp:** the same principle, procedural and seeded with no third-party assets, is already
DrawApp's (`BrushTips`). Milestone 11 goes further than static tips: dry media's grain and gaps
come from the **paper** (document space) and from **pressure-dependent penetration**, not only
from a noisy tip image. A noisy tip repeats the same pattern every dab ("random noise attached to
a round brush", which the specification §5 rejects).

## 5. Performance notes (from source and benchmarks in the tree)

- `crates/paint/examples/bench_brush.rs` exists; it was not run (no toolchain).
- Dabs are rasterised in batches with reused scratch buffers (`self.scratch`,
  `self.depth_scratch`), so there are no allocations per dab.

**For DrawApp:** DrawApp's smudge and wet paths allocate a `QImage` per dab (`StrokeEngine::carry`).
Milestone 11 removes that allocation (specification §10: "avoid unnecessary allocations per
dab").

## What is not taken

- Session state between strokes (reservoir) — see §3.
- Photoshop-style dual brush and its blend modes: not required by milestone 11.
- GPU composition paths.

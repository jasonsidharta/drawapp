# Paint simulation, mixing and stroke geometry (milestone 11 research)

Findings are labelled:
- **Observed:** read in source code at the given revision.
- **Documented:** stated by a cited publication or page that was opened for this research.
- **Background:** standard literature known to the author but not re-read for this milestone;
  cited so it can be checked, and not relied on for any measured claim.
- **Hypothesis:** to be tested by DrawApp's own measurements (listed at the end).

## 1. libmypaint (ISC licence)

`https://github.com/mypaint/libmypaint`, revision `d5a88fbe6649d5ec776bc42ec8c1f4bb29d7fd7f`
(2026-04-14), local checkout. Licence (`COPYING`): ISC, "Permission to use, copy, modify, and/or
distribute this software for any purpose … provided that the above copyright notice and this
permission notice appear in all copies."

**Observed:**
- *Settings and inputs* (`brushsettings.json`):
  - inputs: pressure, random, stroke, direction, tilt declination/ascension, speed1/speed2, custom,
    direction angle, attack angle, tilt declination x/y, gridmap x/y, view zoom, brush radius,
    barrel rotation;
  - settings: 64, among them `smudge`, `smudge_length`, `smudge_length_log`, `smudge_radius_log`,
    `smudge_bucket`, `smudge_transparency`, `paint_mode`, `slow_tracking`,
    `slow_tracking_per_dab`, `tracking_noise`, `offset_by_speed`, `direction_filter`,
    `stroke_threshold`, `opaque_linearize`.
- *Smudge as one carried colour* (`mypaint-brush.c`: `update_smudge_color` line 921,
  `apply_smudge` line 1000).
  - The brush holds one smudge colour (RGBA) per bucket.
  - At most every few dabs, controlled by smudge length through a "recentness" value, it samples
    the canvas average in a radius of `radius × e^(smudge_radius_log)`.
  - It mixes that into the carried colour by `update_factor = max(0.01, smudge_length)`.
  - The dab colour is `mix(smudge colour, brush colour, smudge)`.
  - A transparent smudge colour makes the dab **erase towards that transparency**
    (`eraser_target_alpha = (1 − smudge) + smudge × smudge_alpha`).
- *Spectral pigment mixing* (`helpers.c:521-606`):
  - `rgb_to_spectral` upsamples RGB to 10 spectral bands (three 10-entry basis tables,
    `spectral_r_small` and so on, offset by `WGM_EPSILON`);
  - `mix_colors` mixes two colours by a **weighted geometric mean** per band;
  - `spectral_to_rgb` converts back with a 3 × 10 matrix (`T_MATRIX_SMALL`).
  - `paint_mode` blends between this subtractive mix (1) and ordinary linear RGB mixing (0).
  - Blue and yellow mix to green, not grey.
  - `brushmodes.c` uses the same method for pigment-mode dab compositing (around line 80).
- *Stroke tracking:* `slow_tracking` is an exponential follow of the input position, and
  `tracking_noise` adds noise. **Background:** the same family as DrawApp's pull string and
  Krita's simple smoothing.

**For DrawApp:**
- **Adopt the smudge-length concept** (how long a picked-up colour persists) and the
  **erase-towards-transparency** rule. Both give alpha-aware mixing over transparent regions
  (specification §9: "smudging across transparent regions").
- **Spectral (pigment) mixing.**
  - The weighted geometric mean of spectral reflectances is a published, simple technique.
  - The 10-band basis and matrix are data in libmypaint. Adapting them is permitted under ISC
    with the copyright and permission notice kept.
  - **Decision** (`research/m11-brush-architecture.md`, A6): DrawApp mixes pigment-like colours
    with **per-channel Kubelka–Munk in linear RGB**. Reflectance R becomes K/S = (1 − R)² / 2R,
    the K/S values mix linearly by weight, and the result converts back with
    R = 1 + K/S − √((K/S)² + 2 K/S). This is textbook theory needing no third-party tables, so no
    notice is required.
  - **Worked example:** an ultramarine-like (0.10, 0.20, 0.80) and a cadmium-yellow-like
    (0.95, 0.85, 0.10) mixed 50/50 give about (0.17, 0.30, 0.17), a dark green. RGB averaging gives
    a grey (0.53, 0.53, 0.45).
  - **Limitation:** pure RGB primaries (for example (0, 0, 1) with (1, 1, 0)) mix towards black
    rather than green, because three bands cannot represent the overlap of real pigment spectra.
    A 10-band spectral mix would fix this. Adopting libmypaint's tables with the ISC notice is the
    documented fallback if artists need it (`research/m11-licenses.md`).

## 2. Pigment mixing libraries

| Library | Method | Licence | Usable? |
|---|---|---|---|
| libmypaint spectral WGM | 10-band spectral upsampling, weighted geometric mean | ISC | yes, with notice |
| spectral.js (Ronald van Wijnen), `github.com/rvanwijnen/spectral.js` | Kubelka–Munk with 7 spectral primaries | MIT (documented on its npm/GitHub pages, searched 2026-10-12) | yes, with notice; not used |
| Mixbox (Sochorová & Jamriška, SIGGRAPH Asia 2021), `scrtwpns.com/mixbox` | Kubelka–Munk with a latent pigment space | **CC BY-NC** (documented) | **no**: non-commercial only |

**Background:** Kubelka–Munk theory treats paint as absorbing (K) and scattering (S) layers, and
mixtures add K and S weighted by concentration. Single-constant K/S per spectral band reproduces
the "blue + yellow = green" behaviour; RGB averaging does not.

## 3. Watercolour simulation

**Documented** (pages opened 2026-10-12):
- **Curtis, Anderson, Seims, Fleischer, Salesin, "Computer-Generated Watercolor", SIGGRAPH 1997**
  (`grail.cs.washington.edu/projects/watercolor/`).
  - Watercolour is an ordered set of translucent glazes, each created by a **shallow-water fluid
    simulation**.
  - Superimposed glazes are composited optically with the **Kubelka–Munk** model.
  - Effects reproduced include edge darkening, backruns, granulation, glazing, wet-in-wet and
    dry-brush.
- **Van Laerhoven, Van Reeth, "Real-time simulation of watery paint", Computer Animation and
  Virtual Worlds 16(3-4), 2005, pp. 429-439.**
  - Real-time thin paint: watercolour, gouache, ink.
  - Paint motion follows physical and heuristic rules on a **layered canvas**, rendered by
    Kubelka–Munk.
  - Shows dark edges, glazing, wet-on-wet and pigment types.
  - **All on graphics hardware.**
- **Chu, Tai, "MoXi: Real-Time Ink Dispersion in Absorbent Paper", SIGGRAPH 2005, ACM TOG
  pp. 504-511.**
  - Ink percolation in paper with a **lattice Boltzmann** flow model.
  - Real time **on the GPU**; the CPU runs the brush.

**Background** (not re-read):
- Curtis's model has three layers: a shallow-water layer (velocity, water, suspended pigment), a
  pigment-deposition layer (absorption and desorption, granulation by paper height), and a
  capillary layer (water spreading into dry paper, giving backruns).
- Edge darkening comes from faster evaporation at the edge of the wet area, which drives water,
  and the pigment it carries, outwards.

**What this means for DrawApp:**
- *Precedent:* every real-time precedent ran the simulation on a GPU. DrawApp renders on the CPU
  (QPainter, `QQuickPaintedItem`), and the specification forbids GPU or cloud dependencies for
  this milestone's scope.
- *Bounded CPU model:* a full shallow-water solver over large canvases is not practical on a phone
  CPU at interactive rates. **Hypothesis H-W1:** a reduced, cellular model on the CPU, simulated
  only in the 64 × 64 tiles a stroke wets, at half resolution, reproduces the recognisable
  watercolour effects at interactive rates. The effects:
  - wet-on-wet spreading;
  - hard edges on dry paper;
  - edge darkening;
  - pigment granulation;
  - glazing;
  - drying;
  - lifting.

  The model:
  - diffusion of water and suspended pigment between wet cells;
  - outward pigment transport at evaporating edges;
  - deposition modulated by paper height;
  - evaporation over simulation time.
- *Precision:* the model is a deliberately simplified **approximation**, not Curtis's
  shallow-water equations, and is documented as such (specification §8: report what it is).
- *Seams:* diffusion across tile boundaries needs neighbour cells. Simulating on a working set of
  tiles with their neighbours present avoids seams (specification: "do not introduce cross-tile
  seams").
- *Memory:* sparse per-tile simulation state, released when the tile has dried, keeps memory
  bounded and proportional to the wet area.

## 4. Bristles and paint loading

**Observed** (Krita `hairy`, `research/m11-krita-engines.md` §1):
- bristles sampled from the tip;
- a connected path per bristle;
- contact by bristle length against pressure;
- per-bristle ink depletion;
- soak-ink pickup.

**Background:** research bristle models (for example Baxter et al.'s deformable 3D brushes,
IMPaSTo) simulate bristle bending and a paint volume per bristle. They are far too heavy for this
milestone.

**Hypothesis H-B1:** a 2D contact approximation reads as a bristle brush, at a cost proportional
to (bristles × path length), and stays under 2 ms per input event for up to about 200 bristles at
typical sizes. The approximation:
- bristles at seeded offsets in a flat or round footprint;
- splay with pressure;
- soft contact by pressure;
- an anti-aliased path per bristle;
- a per-bristle reservoir that depletes and picks up canvas colour.

## 5. Dry media

**Observed:** Krita height texturing (`research/m11-krita-engines.md` §4); PhotoCraft's procedural
charcoal and chalk tips (`research/m11-photocraft.md` §4).
**Documented:** Procreate's "Texturized" grain stays behind the stroke (`research/m11-procreate.md`).

**Hypothesis H-D1:** a mark that responds coherently to pressure, direction and scale comes from
the **interaction** of a pressure-dependent penetration depth with a document-space paper height
field, plus direction-aligned grain for charcoal and pastel. That gives:
- light marks catch the tooth;
- heavy marks fill it;
- repeated strokes build up in the same tooth;
- charcoal streaks run along the stroke;
- broken, granular edges.

A noise image multiplied into a round tip (the specification's "random noise attached to an
ordinary round brush") does none of this. It is tested by:
- comparing coverage of the paper's valleys versus peaks across pressures;
- repeated-stroke build-up;
- direction anisotropy of the grain.

## 6. Smudge and mixing: four distinct operations

The specification (§9) asks for four behaviours that are visibly and mathematically distinct.
From the sources:

| Operation | Source precedent | DrawApp definition (milestone 11) |
|---|---|---|
| Smearing existing pixels | Krita smearing (canvas as carrier); DrawApp's carried patch (milestone 6) | per-pixel patch moved along the stroke; no averaging |
| Mixing picked-up colour | libmypaint smudge bucket; PhotoCraft pickup well | **one** carried colour, mixed with each dab's alpha-weighted canvas sample by smudge length; deposited mixed with the brush colour; optionally spectral |
| Depositing new paint | coverage engines | unchanged (coverage, ceiling, flow) |
| Blending wet or dry media | Procreate Wet Mix (documented); watercolour literature | wet paint and watercolour: paint that stays mobile (wet) or not (dry) |

## 7. Stroke geometry for ink

**Background** (standard techniques, not re-read):
- **Catmull–Rom splines** through input points. The centripetal parameterisation (Yuksel,
  Schaefer, Keyser, 2011) avoids cusps and self-intersections between unevenly spaced points.
- **Bézier fitting** (Schneider, "An Algorithm for Automatically Fitting Digitized Curves",
  Graphics Gems, 1990) needs a window of future points, so it adds latency.
- **Arc-length resampling:** dabs at equal distances along the curve. DrawApp's `StrokeSpacer`
  already spaces dabs by distance along straight segments between samples.

**Hypotheses:**
- **H-I1:** centripetal Catmull–Rom between smoothed samples makes fast curves smooth (fewer
  visible polygon corners). It adds exactly one input event of latency. Corner detection (turn
  angle above a threshold) must keep the polyline there to preserve sharp corners.
- **H-I2:** a retroactive end taper removes the milestone 9 lag (the live line trailing by the
  taper length). The tail is re-rendered at pen-up from the stroke's recorded dabs, over only the
  tiles the tail touched.

## 8. Dab caching

**Observed** (milestone 9 research, Krita `kis_dab_cache_base.cpp`): Krita reuses a dab mask
when scale, rotation and subpixel offset match.

**For DrawApp:** milestone 10 measured per-event medians under 0.6 ms with no dab cache (H8,
`docs/m10-performance.md`). The new engines that repeat geometry are procedural strands, leaves
and bristles. **Hypothesis H-C1:** caching per-stroke strand and leaf shapes is only worth adding
if profiling shows rasterisation dominates. Measured in stage 10, not assumed.

## 9. GEGL and Skia

Covered in milestone 10 (`research/m10-gegl-skia-comparison.md`): tile-segment access as in
libmypaint, and no new dependencies. Nothing further is needed for milestone 11.

## Hypotheses to measure

| Id | Hypothesis | Measured in | Falsified if |
|---|---|---|---|
| H-D1 | Pressure × paper height gives tooth fill and build-up | stage 3 tests | valley/peak coverage ratio does not change with pressure |
| H-I1 | Catmull–Rom smooths fast curves, one event of latency | stage 4 tests and benchmark | polygon corners remain, or latency over one event |
| H-I2 | Retroactive taper: no live lag, identical taper shape | stage 4 tests | live line still trails, or tail differs from the held-back taper |
| H-B1 | Bristle contact model under 2 ms per event (≈ 200 bristles) | stage 6 benchmark | median above 2 ms at default size |
| H-W1 | Tile-local CPU watercolour at interactive rates | stage 7 benchmark | median event above 16 ms on the development machine at default size |
| H-C1 | No dab cache needed | stage 10 profile | rasterisation over 50 % of a procedural brush's event time |
| H-R1 | Existing presets render identically | every stage (golden images, exact compare) | any existing golden or pixel test changes |

# Brush engine architecture (milestones 9 and 11)

How a stroke becomes pixels in DrawApp, which parts are shared by every brush, and where each
milestone 9 and milestone 11 feature lives (milestone 11: the last sections). Everything here exists in the code; planned work is labelled. The
property-by-property reference is `docs/brush-settings-specification.md` (generated from the
code). The preset files are described in `docs/brush-preset-format.md`.

Research behind the design: `research/milestone9-krita-brush-engine.md`,
`research/milestone9-procreate-brush-studio.md` and the decisions D2–D6, D10 and D11 in
`research/milestone9-architecture-decisions.md`.

## One engine, data-driven brushes

There is one renderer, `StrokeEngine` (`src/core`, QtGui only). Every preset, the canvas, the
quick-panel scratchpad, the Brush Studio preview and pad, the preset thumbnails and the brush
sheet go through it. Brushes differ only in `BrushSettings` data. There is no per-preset code
path and no plugin registry. Krita registers paint-op plugins; we chose not to (decision D2).

"Engines", as the Brush Studio and the specification name them, are combinations of two
settings:

| Engine | `blend` | `stamp` | What a dab does |
|---|---|---|---|
| Paint | paint | tip | adds coverage; composited with the colour by the blend mode |
| Erase | erase | tip | adds coverage; removes alpha |
| Spray | paint or erase | spray | many small round particles over the dab's disc |
| Hatching | paint or erase | hatching | the tip, masked by parallel lines fixed in document space |
| Smudge | smudge | (tip) | a carried patch of canvas paint dragged along |
| Wet paint (prototype) | wet | (tip) | a carried patch charged with brush colour that deposits and picks up |

## Pipeline

Separated into these stages (milestone 9 section 3, "Core architecture"):

| # | Stage | Where |
|---|---|---|
| 1 | Pointer and stylus input | `CanvasItem` (mouse, tablet via the window event filter, touch), `BrushScratchpad` |
| 2 | Normalisation | `effectivePressure` (`Pressure.h`), `StrokeSample` in document pixels with `hasPressure` and `hasTilt` |
| 3 | Input filtering | `StrokeEngine::acceptInput`: jitter filter (holds back moves shorter than N px), pressure smoothing (exponential, by event time) |
| 4 | Path construction | `StrokeSmoother` (pull-string, distance-based); then `StrokeEngine::annotate` adds path sensors per smoothed sample: distance, speed, acceleration and direction |
| 5 | End-taper hold-back | samples within `taperEnd` px of the newest one wait in `m_held` until the end of the stroke is known (pen up) |
| 6 | Dab placement | `StrokeSpacer`: distance-spaced dabs with the input interpolated; the diameter and the step after each dab come from callbacks into the engine (size and spacing dynamics) |
| 7 | Sensor dynamics | `CompiledDynamics::evaluate` per dab (`BrushDynamics`) |
| 8 | Dab parameters | `StrokeEngine::paramsFor`: scatter, ceiling, flow, angle, roundness, flips, texture strength, colour |
| 9 | Tip and stamp | `DabShape` (round or mip-mapped image tip, flips), spray particles, hatching lines, density |
| 10 | Accumulation | coverage tiles (16-bit, 64 × 64, sparse) with optional colour tiles; or the carried patch (smudge, wet) |
| 11 | Selection masking | applied once per pixel when compositing, or as `before + (work − before) × s` for smudge and wet paint |
| 12 | Composite and invalidation | per touched tile, once per input event, recomputed from the undo snapshot; then `LayeredDocument::refreshComposite(rect)` |
| 13 | History and persistence | the stroke's `TileSnapshot` → one `UndoCommand`; brush settings in the brush library and `.drawbrush` files (not in projects) |

## Sensors and dynamics (`BrushDynamics`)

A parameter can follow up to four sensors. Each mapping is `{sensor, curve, range}`:

```
value = minimum + (1 − minimum) × combine(curve_1(sensor_1), …)     in 0..1
```

- **Sensors.** pressure, tilt X, tilt Y, tilt amount, tilt direction, stroke direction, speed,
  acceleration, distance (repeating), fade (0 → 1 over a length), time, random per dab, random
  per stroke. Speed, acceleration, distance, fade and time take a range in their unit.
- **Curves.** 2 to 16 points joined by a monotone cubic (Fritsch–Carlson), so a curve never
  overshoots its points. They are tabulated once per stroke into 256 entries (as Krita's
  `floatTransfer(256)`), with linear interpolation between entries. The presets are linear,
  inverted, threshold, soft and firm (exponential) and clamped.
- **Combination.** multiply (default), add (limited to 1), max, min and difference. These are
  Krita's five curve modes.
- **Targets and what the value does.** size, opacity, flow, spacing, roundness, scatter, texture
  strength and smudge strength (wet paint: attack) are multiplied by the value. Rotation adds
  value × 360°. Hue turns by value × 360°, so a full reading is a full turn: the chosen hue.
  Saturation and lightness are multiplied.
- **The basic pressure responses** (`pressureSize`, `pressureOpacity`, `pressureFlow`:
  enabled, minimum, firmness) from milestone 6 are kept as they were. They multiply with the
  dynamics of the same parameter. Milestone 6 presets therefore render exactly as before
  (decision D3, revised).

**No invented data.** A sensor without data is left out of the combination, and a parameter none
of whose sensors has data keeps its base value. This covers pressure from a mouse, touch or a pen
without pressure (`StrokeSample::hasPressure == false`), tilt from a device that reports none,
speed without event times, and direction before the stroke has moved. Mouse and touch therefore
behave as in milestone 6 whatever the curves say, as AGENTS.md requires. Random sensors always
have data.

**Numerical rules.**
- Pressure is clamped to 0..1, and NaN reads as 1.
- Speed is distance over time since the last sample with a later time. Duplicate or out-of-order
  timestamps (dt ≤ 0) keep the previous speed. Instantaneous speed is capped at 100 px/ms and
  smoothed with a 25 ms time constant; acceleration is clamped to ±10 px/ms².
- Every random value is `hash(seed, dab index, salt)`, with one salt per use. Changing one setting
  never reshuffles another's randomness, and the same input and seed give identical pixels.
- The minimum size (≥ 1 px) is applied last, so a zero or negative size cannot occur.

Tested in `tst_brushengine` (`dynamics*`, `randomSeedReproducibility`). Pressure and tilt there
are simulated, which shows the engine's response. It is not evidence about any physical pen.

## Dab parameters

Per dab (`paramsFor`, after the spacer placed it), with `r` the sensor readings:

- **diameter** = size × per-stroke size jitter × pressure→size × tilt factor × size dynamics ×
  (1 − size jitter × random) × taper, at least the minimum size. Computed when the spacer places
  the dab, so spacing follows the actual size.
- **step after the dab** = spacing × diameter × spacing dynamics × (1 ± spacing jitter), at least
  0.5 px. Flow normalisation uses this actual step, so neither spacing jitter nor spacing dynamics
  change the stroke's darkness.
- **centre** += scatter (across the travel direction, and along it with "both axes") × diameter
  × random × scatter dynamics.
- **ceiling** = opacity × pressure→opacity × tilt factor × opacity dynamics × (1 − opacity
  jitter × random) × (1 − taper opacity × (1 − taper)), rounded to a whole 8-bit level.
- **pass flow** = flow × pressure→flow × flow dynamics × (1 − flow jitter × random); per-dab
  flow `f = 1 − (1 − pass flow)^(step / L)` as in milestone 6.
- **angle** = angle + (travel direction or pen azimuth, when following direction; azimuth when
  tilted more than the threshold) + rotation dynamics × 360° + angle jitter.
- **roundness**, **flips**, **texture strength** and **colour** follow their settings, jitter and
  dynamics.
- **count**: each spacing step renders `count` dabs (fewer with count jitter), each with its own
  randomness.

## Accumulation and compositing (Paint, Erase, Spray, Hatching)

This is unchanged from milestone 6. Coverage per pixel is `S = max(S, v·C)` (uniform) or
`S += v·f·(C − S)` (build-up), and opacity is applied once. Milestone 9 additions:

- **Per-pixel colour, only when it varies per dab.** Hue, saturation or lightness jitter, or
  colour dynamics, add a colour tile next to each coverage tile (16 KiB per touched tile). It is
  mixed by coverage as `c ← (c·S_old + c_dab·ΔS) / S_new`. Per-stroke colour jitter only changes
  the stroke colour once. Coverage itself never changes with colour (`colorDynamicsVaryPerDab`).
- **Texture per stroke or per dab.**
  - *Per stroke*: the grain is applied to the stroke coverage when compositing; this is the
    milestone 6 default.
  - *Per dab*: "texture each dab", which texture strength dynamics always use. The grain is
    applied to each dab's tip value, so overlapping build-up dabs fill it in, and moving grain
    stamps it relative to the dab.
  - *Adjustments*: rotation, scale, invert, contrast, brightness and a random per-stroke offset
    are applied to the grain value.
- **Blend modes** (paint only), W3C compositing on premultiplied pixels with the dithered
  coverage as source alpha:
  - normal;
  - multiply: `k·cs·(1 − αb) + cb'·(1 − k) + k·cs·cb'`;
  - screen: `k·cs + cb' − k·cs·cb'`;
  - behind: `cb' + k·cs·(1 − αb)`;
  - inside (preserve alpha): colour only where there is paint, alpha unchanged.
- **Spray**: n particles per dab, uniformly distributed over the disc (radius `R√u`). Each is an
  anti-aliased round particle with optional size jitter, deposited with the same accumulation
  rule.
- **Hatching**: the tip value times the coverage of parallel lines at `hatchAngle`, every
  `hatchSpacing` px and `hatchThickness` wide, fixed in document space so successive dabs join.

## Smudge and the wet-paint prototype

Both carry a patch (premultiplied RGBA floats, the size of the largest possible dab) and write
through the selection once.

- **Smudge** (milestone 6 plus additions). Each dab lays the patch down with strength × tip, then
  the patch picks up the pixels at the pickup rate; both are per pass, normalised to the step.
  - *Dulling*: after each pickup, the patch is replaced by its coverage-weighted average, so it
    carries one colour.
  - *Colour rate*: mixes the brush colour into the patch each pass.
  - *Colour only*: keeps each pixel's alpha and moves colour only.
- **Wet paint** (prototype, decision D6). The patch starts charged with the brush colour, thinned
  by dilution and scaled by the load. Each dab:
  1. refills it towards the brush paint by `load × remaining`, where remaining = 1 − distance /
     load length;
  2. composites it over the pixel (source-over with alpha attack × tip), which never lowers alpha;
  3. picks up the pixel at the pull rate.

  Wetness jitter varies the dilution per dab.

  It is *not* a fluid simulation: no diffusion, drying, paper absorption, persistent wetness
  between strokes or pigment (subtractive) mixing. Those are listed as planned properties or
  limitations.

## Taper

- **Start taper** scales size (and, with taper opacity, opacity) by `startSize + (1 − startSize)
  × t^shape` over the first `taperStart` px.
- **End taper** needs the end of the stroke. Samples within `taperEnd` px of the newest sample
  are held back and drawn at pen-up with the then-known total length. The live line therefore
  lags by the taper length.
- A stroke that never moves gets no end taper. With a start taper it is a small dot.

## Property registry and Brush Studio

- `BrushProperties` declares every property once: id, group, name, description, type, range, unit
  and display scale, the engines it applies to, sensors, rendering, cost, tests, any condition for
  it to have an effect, and accessors.
- The registry drives JSON reading and writing (including keeping unknown keys), sanitising,
  equality, `BrushLibrary::setValue`, the Brush Studio rows and the generated specification.
- The 120-property design target is listed in full. 94 properties are implemented; the 26
  planned ones have no accessors and no controls.
- `tst_brushengine::everyImplementedPropertyChangesRendering` renders a standard stroke with the
  default and a changed value of every implemented property, on an engine it applies to, and
  requires different pixels.

`BrushStudio` (`src/app`) edits a copy of one preset:
- *Done* stores it into the preset (`BrushLibrary::storePreset`). *Cancel* drops it.
- Rows come from the registry. Settings the current engine does not use, and planned ones, are
  listed with an explanation instead of silently doing nothing.
- The preview is a thumbnail stroke with simulated pressure, labelled as simulated. The try-out
  pad is a `BrushScratchpad` that paints with the copy.

## Memory and cost per stroke

| Buffer | Size | When |
|---|---|---|
| Coverage tiles | 8 KiB per touched 64 × 64 tile | paint, erase, spray, hatching |
| Colour tiles | 16 KiB per touched tile | per-dab colour only |
| Carried patch | (size × max(1, tilt size) + 4)² × 16 B | smudge, wet paint |
| Work tiles | 16 KiB per touched tile | smudge or wet paint under a selection |
| End-taper hold-back | the samples of the last taperEnd px | taper end > 0 |
| Imported tip | its mip chain (≤ 1.34 × 64 KiB) | imported tip |
| Undo snapshot | 16 KiB per touched layer tile | always (as before) |

Per input event, every touched pixel is recomposited once, whatever the number of dabs. Measured
costs per engine are in `docs/milestone9-performance.md`.

## Milestone 11: deposit strategies and the new engines

Research and decisions: `research/m11-brush-architecture.md` (A1–A10),
`research/m11-paint-simulation.md` (models and hypotheses), `research/m11-licenses.md` (sources;
nothing was copied). Measurements: `docs/m11-performance.md`.

### One pipeline, pluggable deposits

`StrokeEngine` still runs everything every brush shares, in this order: input filters,
smoothing, the optional curve (`StrokeCurve`), path sensors, taper, spacing, dynamics and
`DabParams`. What a placed dab *does* is now a `StrokeDeposit` (`StrokeDeposit.h`), chosen from
`blend` and `stamp` when the stroke starts:

| Deposit | Engines |
|---|---|
| `CoverageDeposit` | paint, erase, spray, hatching: the milestone 6/9 coverage model, moved unchanged |
| `DryDeposit` (coverage) | dry media: paper tooth × pressure-dependent penetration |
| `BristleDeposit` (coverage) | bristles: per-bristle capsules, reservoirs and pickup |
| `ProceduralDeposit` (coverage) | strands, grass, foliage, splatter, pattern |
| `CarriedDeposit` | smudge (smear, dulling, mix) and wet paint |
| `WatercolorDeposit` | watercolour, on a `WetLayer` |

Shared state is `StrokeContext`: settings, colour, seed, texture, clip, the allocation-failure
flag and the canvas wet layer. Raster helpers are in `BrushRaster.h`.

Every deposit follows the contract in `StrokeDeposit.h`:
- capture into the undo snapshot before changing a tile;
- change only the reported area and respect the selection once per pixel;
- stop cleanly when memory is refused;
- be deterministic (seeded).

Coverage-type deposits can redraw dabs from their buffers. That is what the live end taper uses
(`supportsRebuild`); bristles cannot, so they keep the hold-back.

**Compatibility.** The milestone 10 presets and feature scenarios render bit-identically:
`tst_brushengine::milestone10EngineReference` holds 330 exact renders from the unmodified
milestone 10 engine. Milestone 11 settings default to the old behaviour. They are written to
preset files only when used, the file versions stay at 2, and older builds skip new-engine
presets one by one (verified with the milestone 10 decoder; see
`research/m11-brush-architecture.md` A2).

### The engines

- **Dry media** (stamp `dry`):
  - The paper is a document-space height field: the texture, "paper" by default. A dab pixel
    deposits tip × smoothstep(penetration − (1 − height)). Penetration follows pressure
    (`pressureDepth`) and tilt (`dryTiltDepth`): light marks catch the peaks, heavy marks fill
    the valleys, and repeated strokes meet the same tooth.
  - Streaks follow the stroke heading; the rim can crumble (`dryEdgeBreakup`); seeded grit
    adds holes.
  - Presets: Graphite 2H/HB/6B, Vine and Compressed Charcoal, Chalk, Soft Pastel.
- **Ink** (shared pipeline):
  - optional centripetal Catmull–Rom between samples, with one sample of latency; corners
    sharper than `cornerAngle` stay sharp;
  - speed-adaptive smoothing (`smoothingSpeedAdapt`);
  - live end taper (`taperLive`): the line reaches the pen while drawing, and the tail is
    re-spaced with the taper at pen-up, rebuilding only the tiles it touched.
  - Presets: G-Pen, Fine Liner, Brush Pen, Dry Ink.
- **Mixing** (blend `smudge`, mode `mix`):
  - One carried colour, picked up centre-weighted (`smudgeRadius`). It lasts as long as
    `smudgeLength` says, is joined by the brush colour (colour rate, with dynamics), and is
    laid down with depletion.
  - Mixing is premultiplied and alpha-aware. Optional `pigmentMixing` uses per-channel
    Kubelka–Munk in linear light (`PigmentMix`) for smudge, wet paint and bristles.
  - Presets: Oil Blender, Colour Mixer, Smear Knife.
- **Bristles** (stamp `bristle`): a 2D contact approximation, not a physical simulation.
  - Bristles sit at seeded places in the dab's footprint (round or flat by roundness). Each
    draws an anti-aliased capsule from dab to dab.
  - Pressure splays the bristles, and longer bristles touch first.
  - Each bristle has its own reservoir; a drying bristle skips the paper's valleys, then stops.
  - Each bristle picks up the layer's colour from before the stroke.
  - Presets: Round Bristle, Flat Bristle, Dry Brush.
- **Watercolour** (blend `watercolor`; `WetLayer.h`): a bounded cellular approximation, not a
  fluid solver.
  - **State:** per wetted 64 × 64 tile there is water, suspended and settled pigment, and the
    base pixels.
  - **Brush input:** the brush wets its contact area up to its own wetness and exchanges paint
    with the wash.
  - **Each 16 ms step**, double-buffered:
    - water flows only between wet cells, so wet-on-dry edges stay hard;
    - near the edge, lower pressure and faster evaporation leave pigment at the rim;
    - pigment settles faster in the paper's valleys (granulation);
    - new water lifts settled pigment.
  - **Display:** a multiply glaze with a 5-point anti-aliasing average.
  - **Canvas session:** `CanvasItemWet.cpp`. The paint stays wet after pen-up and dries on a
    20 Hz timer with time budgets. The stroke's undo step stays open until the paint dries.
    The next watercolour stroke on the layer paints wet-on-wet. Any other edit, undo or redo
    fixes the paint as it looks, and saving says the file holds the paint as it looks now.
  - **Performance:** steps and pixel computation run on up to 8 threads with results identical
    to one thread. Dry tiles go dormant.
  - Presets: Watercolour Wash, Watercolour Round.
- **Procedural** (stamps `strands`, `grass`, `foliage`, `splatter`, `pattern`):
  - Geometry is generated per dab from the seed and dab index: tapered capsule polylines,
    ellipses and leaf lenses.
  - Each element is rasterised into a scratch buffer (largest coverage per pixel), then
    deposited once.
  - Elements point along the stroke, up, or by the brush angle (`procDirection`).
  - Hatching gains cross-hatch passes by pressure (`hatchCrossPasses`); spray gains a Gaussian
    distribution.
  - Presets: Hair, Fur, Grass, Foliage, Splatter, Stitches, Chevron Trim, Crosshatch Pen,
    Airbrush Spray.

### Registry and presets

The registry holds the 120-property milestone 9 design target, of which 96 are implemented, and
45 milestone 11 properties in engine groups: Ink and Stroke, Dry Media, Mixing, Bristles,
Watercolour and Procedural. Each property lists the engines it works with.

`tst_brushengine::everyPropertyChangesRenderingOnEveryEngineItClaims` renders every implemented
property on **every** engine it claims (1035 pairs). The panel shows only settings that apply
(`BrushLibrary::applies`); the Studio dims the others and says they do not apply.

Presets have a category, derived from what they do: Basic, Erasers, Ink, Dry Media, Bristles,
Blending, Watercolour, Nature & Patterns or Effects. The panel groups by category and has
search. There are 38 built-in presets.

## Not built (planned or out of scope)

- Brush engines: a physical (3D) bristle simulation (milestone 11 has a 2D contact model),
  clone, sketch "connect" lines, filters and particle physics.
- Physics: a fluid simulation of watercolour (milestone 11 has a bounded cellular
  approximation), oil paint thickness and impasto, wet paint stored in files.
- Rendering: GPU rendering. Dab rendering is single-threaded; only the watercolour wet layer uses
  threads (`docs/m11-performance.md`).
- Preset import: Krita `.kpp` and Procreate `.brush` (see `docs/brush-preset-format.md`).
- The 26 planned properties in the specification.

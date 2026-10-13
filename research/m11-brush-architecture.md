# Milestone 11 brush architecture: audit, decisions and plan

Written before implementation (stage 1–2). It records the verified baseline, the existing
architecture as it is in the code, the decisions for milestone 11 with their reasons, and the
staged plan. Research inputs: `research/m11-krita-engines.md`, `m11-photocraft.md`,
`m11-procreate.md`, `m11-top30-brushes.md`, `m11-paint-simulation.md`, `m11-licenses.md`.

## 1. Baseline (verified 2026-10-12)

| Item | Value |
|---|---|
| Repository | `C:\Users\Asus\Downloads\YC W27` (git, no remote) |
| Baseline commit | `dcde3838a68d1fce905cd9d6e90d18f79b56bd23` ("Milestone 10 stage 13: final report and documentation") on `main` |
| Working tree at start | clean (nothing uncommitted to preserve) |
| Milestone 11 branch | `m11-brushes`, created from the baseline |
| Build directory (milestone 11) | `C:\Users\Asus\Downloads\YC W27\build\windows-mingw-release` (this worktree's own; MinGW 15.1, Qt 6.12.0) |
| Android build directories | `%USERPROFILE%\drawapp-build\android-{x86_64,arm64}-debug` (this worktree's script, `tools/build-android.ps1`) |
| Codex worktree (`C:\Users\Asus\Downloads\DrawApp-Codex`, branch `m10-preview`) | **not present** when milestone 11 started (no such directory, no such branch in this repository). Its build output is therefore not shared with this worktree |
| Tests at baseline | `ctest` 10/10 suites, 424 QtTest results (`docs/m10-test-results.md`) |
| Brush properties | registry of 120 designed, 94 implemented, 26 planned (`docs/brush-settings-specification.md`, generated) |
| Built-in presets | 8: Sketch Pencil, Clean Ink, Solid Round, Soft Airbrush, Textured Paint, Blend / Smudge, Hard Eraser, Soft Eraser |
| Golden images | 32 files in `tests/golden` (stroke goldens of milestones 6, 9, 10) |

The milestone 10 baseline claims were checked against the repository where they matter to this
milestone:
- brushes paint identically on tiled and dense images (`tst_brushengine::tiledLayersPaintExactlyLikeDenseImages`
  exists and passes);
- 94 of 120 properties are implemented (`implementedCount()` and the generated specification);
- 16 tile-seam goldens exist.

## 2. The engine as it is (audit)

One renderer, `StrokeEngine` (`src/core/StrokeEngine.{h,cpp}`, 1402 lines). Every preset, the
canvas, the scratchpad, the Brush Studio preview and pad, thumbnails and the brush sheet use it.

```
StrokeSample (doc px, pressure, tilt, time)                 CanvasItem::beginStroke/extendStroke
  -> acceptInput: pressure clamp/smoothing, jitter filter   StrokeEngine::acceptInput
  -> StrokeSmoother (pull string)                           StrokeSmoother
  -> annotate: distance, speed, acceleration, direction     StrokeEngine::annotate
  -> end-taper hold-back (last taperEnd px wait)            StrokeEngine::process
  -> StrokeSpacer: distance dabs (+ airbrush time dabs)     StrokeSpacer
  -> paramsFor: dynamics, jitter, taper -> DabParams        StrokeEngine::paramsFor
  -> deposit:
       coverage blends (Paint, Erase):
         rasterize / rasterizeSpray -> 16-bit coverage tiles (+ colour tiles)
         compositeTouched: per touched tile, from the undo snapshot, once per call
       carried blends (Smudge, WetPaint):
         carry -> carryInto (patch) -> write through the selection
  -> TileSnapshot (undo) captured per dab rect               StrokeSurface::capture
```

Strengths to keep:
- **Deterministic** randomness: hash of seed, dab index and salt.
- **Exact compositing:** once per pixel per call, from the undo snapshot, so the result does not
  depend on how input was split.
- **Sparse tiles** throughout: `StrokeSurface` gives segment row pointers, so no dense
  full-layer copies.
- **Exact undo**, and selection masking once.

Weaknesses that matter for milestone 11:
1. **Deposit is hard-coded** in `renderDab`, with `if` chains on blend and stamp. Each new engine
   would add more special cases to `rasterize`, `carry` and `composite`.
2. **The carried-paint path allocates a `QImage` per dab** (`carry`: `QImage region(rect.size(), …)`).
3. **End taper delays the live line** by the taper length (the hold-back).
4. **Paths between input events are straight lines**, so fast curves show polygon corners.
5. **There is no state between strokes**, which wet-on-wet needs.

## 3. Decisions

### A1. One pipeline, pluggable deposit strategies

The specification suggests modular strategies (§4). Krita uses a plugin per engine; DrawApp
milestone 9 decided against plugins (decision D2). Milestone 11 keeps **one pipeline** and adds a
compiled-in **deposit strategy** interface for what differs between engines: how a dab changes
the stroke buffers or the layer. The pipeline covers everything that is shared:
- input filters;
- smoothing and curve interpolation;
- path sensors;
- taper;
- spacing;
- dynamics;
- `DabParams`;
- undo capture;
- selection;
- compositing order.

```
class DabDeposit {            // src/core/BrushDeposit.h (new)
  virtual void begin(DepositContext&) = 0;
  virtual void deposit(DepositContext&, const DabParams&, const QRect& rect) = 0;
  virtual void segment(...)   // engines that paint along the path (bristles, strands) between dabs
  virtual void compositeTouched(...) // or rely on the shared coverage composite
  virtual void finish(...)
};
```

The existing coverage engines (Paint, Erase, Spray, Hatching) and the carried engines (Smudge,
Wet paint) move behind this interface **without pixel changes**. That is verified by the
existing goldens, the exact tiled/dense equality test, and a new exact comparison against the
milestone 10 engine (decision A9). New engines are new strategies.

Rejected: a separate class per engine re-implementing the pipeline. It would duplicate
sensors, taper and spacing, so behaviour would drift between engines.

### A2. Engines as data (`BrushSettings`), versioned

New engines are new values of the existing selectors, so the Brush Studio, the registry and
serialisation keep working:

| Engine | `blend` | `stamp` | Strategy |
|---|---|---|---|
| Dry media (graphite, charcoal, chalk, pastel) | paint / erase | **dry** (new) | coverage, with paper-tooth penetration and directional grain |
| Ink | paint | tip | coverage; the improvements are in the shared path (A4) |
| Bristle | paint | **bristle** (new) | per-bristle paths into coverage plus colour tiles; per-bristle reservoir and pickup |
| Smudge / mixing | smudge | (tip) | carried patch; new **mix** mode (one carried colour), smudge length, pickup radius, pigment mixing |
| Watercolour | **watercolor** (new) | (tip) | wet-layer simulation (A7) |
| Procedural (hair/fur, grass, foliage, splatter, pattern) | paint / erase | **strands, grass, foliage, splatter, pattern** (new) | geometry rasterised into coverage plus colour tiles |
| Hatching | paint / erase | hatching | adds **crosshatch passes** by pressure (as Krita, own mapping) |
| Spray | paint / erase | spray | adds a **distribution** (uniform / Gaussian) |

Presets:
- The file formats **do not change version**: `.drawbrush` and the brush library stay at
  version 2 (`BrushPresetFile.h`, `BrushPresetStore.h`). Their structure is unchanged; milestone 11
  only adds new keys and new option values.
- **Why not bump:** a bump would make milestone 9 and 10 refuse *every* file
  (`BrushPresetStore::decode`: "saved by a newer version"). Without one:
  - presets that use only old features stay fully readable by older builds;
  - their new keys, if any, are kept in `extra`;
  - a preset whose `blend` or `stamp` is a new engine is skipped **individually** by an older
    library reader, with a warning ("Skipped custom brush …: unknown option"), leaving the other
    presets intact;
  - an older build refuses a single `.drawbrush` of a new engine with the same error.
- This is tested by decoding milestone 11 output with milestone 10's rules: the decoder in a
  milestone 10 build, run from the baseline worktree.
- **Verified 2026-10-13 (stage 9)** with the milestone 10 decoder: a scratch program built from
  the baseline worktree (`dcde383`) read the committed samples `tests/golden/m11-library-sample.json`
  and `m11-grass.drawbrush` (written by milestone 11; `tst_brushengine::milestone11LibrarySampleIsStable`
  keeps them stable):
  - library accepted;
  - the old-engine preset was kept;
  - the old-engine preset with milestone 11 keys was kept, with `curveInterpolation`,
    `smoothingSpeedAdapt` and `taperLive` in `extra`;
  - the grass, watercolour and dry-media presets were each skipped with "Brush setting "stamp" /
    "blend" must be a known option", and a favourite and the paint selection pointing at them were
    dropped;
  - the single grass `.drawbrush` was refused with "The brush file has an invalid setting: …".
- **Limitation (downgrade):** milestone 9 and 10 write the library back without the presets they
  skipped. Opening a milestone 11 library in an older build and changing anything there loses that
  library's new-engine presets. The released readers cannot be changed. The final report lists
  this.
- Unknown keys continue to be preserved (`extra`). `BrushSettings::FormatVersion` (written as
  `settingsVersion`, informational) becomes 3.

### A3. Dry media: paper tooth × penetration depth, directional grain

Specification §5; hypothesis H-D1.
- **Paper height field** h(x, y) in 0..1, in document space:
  - seeded, tileable, generated at stroke start;
  - two octaves of value noise plus fine fibre noise;
  - scale and anisotropy set per preset.

  Document space means repeated strokes meet the same tooth.
- **Penetration depth** p = depth(pressure, ceiling): soft and hard pencils differ by the depth
  curve and the maximum density. A dab pixel deposits `v × smooth(p − h)`, a soft step over a
  narrow band:
  - light pressure catches only the peaks;
  - heavy pressure fills the valleys.
- **Grain anisotropy** (charcoal, pastel): the noise is stretched along the stroke direction and
  sampled in a frame turned by the heading, which gives streaks along the stroke.
- **Edge breakup:** near the rim of the dab footprint, p is lowered by a noise term, so edges
  crumble (charcoal) rather than fading uniformly.
- **Build-up:** the coverage engine's build-up accumulation within a stroke, and normal
  compositing across strokes, both in the same paper valleys, so layered strokes darken the
  peaks first.
- **Gaps and scatter:** density and scatter (existing) per preset.
- **Erasing:** dry media erase with the same tooth (an eraser stroke with stamp dry lifts the
  peaks first). The ordinary eraser removes everything, as before.

### A4. Ink: curve interpolation, corner preservation, retroactive end taper, speed width

Specification §6; hypotheses H-I1 and H-I2.
- **Centripetal Catmull–Rom** between smoothed samples before spacing. It needs the next sample,
  so it adds **one input event of latency**: measured, and documented. Optional per preset
  ("curve interpolation"). Off for existing presets, so their pixels do not change.
- **Corner preservation:** where the input turns by more than a threshold angle, the segment is
  kept straight and the pull string is released at the corner instead of being cut by it.
- **Retroactive end taper** ("taper mode: live"):
  - the line is drawn at full width while drawing;
  - at pen-up the dabs within `taperEnd` of the end are re-parameterised with the taper;
  - the coverage of the tiles they touch is rebuilt from the recorded dab list;
  - those tiles are recomposited from the undo snapshot.

  This removes the lag of the milestone 9 hold-back. Memory: about 100 bytes per dab recorded.
  The existing "hold back" mode stays the default for existing presets.
- **Speed → width** with input velocity filtering: the speed sensor exists (smoothed, 25 ms).
  Ink presets map speed to size with a curve.
- **Zoom consistency:** smoothing radii are in view pixels (milestone 6), and width is in
  document pixels, so a line looks the same at every zoom.

### A5. Bristle: 2D contact model

Specification §7; hypothesis H-B1.
- **Bristles:** N bristles at seeded offsets in a **round** (disc) or **flat** (thin ellipse)
  footprint, each with a stiffness (length) and a reservoir.
- **Per input segment:** the footprint is oriented by heading or tilt and splayed by pressure
  (spread grows with pressure). Each bristle draws an **anti-aliased capsule path** from its
  previous to its new position, so there are no gaps at speed, into coverage and colour tiles.
- **Contact:** soft by pressure. A bristle's contact weight rises from 0 to 1 as pressure passes
  its length threshold, so light pressure paints with few bristles.
- **Reservoir:** each bristle's paint depletes with the distance it has painted. "Loaded"
  presets start full; "dry" presets start low and skip, which leaves broken strokes.
- **Pickup:** each bristle mixes in the colour of the layer under it (sampled from the undo
  snapshot, so never its own fresh paint), weighted by a pickup rate. A stroke across existing
  paint drags multicolour streaks.
- **Mixing:** RGB, or pigment (Kubelka–Munk, A6).

### A6. Colour mixing: per-channel Kubelka–Munk ("pigment" option)

`research/m11-paint-simulation.md` §1–2: subtractive mixing (blue + yellow towards green) by
per-channel Kubelka–Munk in linear RGB, as an option wherever colours mix:
- smudge mix colour;
- bristle pickup;
- wet paint;
- watercolour pigment.

No third-party tables. RGB mixing stays the default where existing presets mix, so they render
unchanged.

### A7. Watercolour: tile-local wet layer, live drying, exact undo

Specification §8; hypothesis H-W1.

**State.** `WetLayer` (brush subsystem, `src/core/WetLayer.*`) belongs to one document layer.
It is sparse: one entry per 64 × 64 tile that is wet. Per tile:
- water (float per cell);
- suspended pigment (premultiplied RGB + amount per pixel);
- deposited pigment;
- the layer's pixels before the tile got wet (the "base").

The pixels shown are always `base ⊕ (deposited + suspended)`, so the layer always looks like the
paint. Memory is charged to `MemoryBudget` (category Temporary, the existing API, unmodified).
A tile is released when it has dried.

**Dynamics** (one simulation step, on the wet tiles and their neighbours, so there are no seams):
- water and suspended pigment diffuse between wet cells, giving wet-on-wet blooms; they do not
  diffuse into dry cells, giving hard edges on dry paper (wet-on-dry);
- pigment moves outward where the water gradient is steep and evaporation is fastest, giving edge
  darkening;
- suspended pigment deposits at a rate modulated by the paper height, giving granulation;
- water evaporates; when a cell's water falls below a threshold, its remaining pigment deposits
  and the cell is dry;
- a wet dab over deposited pigment re-suspends some of it (lifting).

**Time.**
- Simulation time advances in fixed steps.
- During a stroke, the steps follow the input events' time stamps.
- After pen-up, a timer advances the simulation in **wall-clock** time (at most N steps per tick,
  bounded) until the paint is dry or a drying time has passed.
- Tests drive the simulation with explicit steps, so results are deterministic.

**Undo.**
- The last wet stroke's undo record stays **open** while its paint dries. Drying changes are
  captured into the same `TileSnapshot`.
- Any other action first **commits** it as one undo step. That includes undo, redo, another
  stroke, a tool or layer change, a transform, a file operation and closing.
- Another watercolour stroke on the same layer while the paint is still wet keeps the wet layer:
  that is wet-on-wet across strokes.
- Undo or redo of anything **drops** the wet state of that layer without touching pixels: the
  paint becomes dry as it is, and the pixels stay exact. Krita drops its between-stroke state in
  the same way when it becomes incompatible (`research/m11-krita-engines.md` §3).
- Undo of a wet stroke restores its pixels exactly.

**Cancellation.** Drying can be stopped ("Dry now" commits as is). A stroke cancelled by memory
failure is restored from its snapshot, and the wet layer is dropped.

**Save and reopen.**
- Saving commits the open wet stroke. The file holds the pixels as they look.
- Wet-layer state is **not** written to the project file in this milestone: that would need
  format changes in `ProjectFile`, which belongs to the persistence subsystem and a possible
  conflict with the parallel milestone (§6).
- The artist is **told** when saving while paint is wet ("Wet paint is saved as it looks now;
  it will be dry when the file is reopened"). The state is never flattened silently.
- After saving, the session keeps the wet state, so painting can continue wet.
- Recorded as a known limitation, with the format change described for a later milestone.

**Performance.**
- Cells at full resolution inside wet tiles only, with work proportional to the wet area.
- Step count per event is bounded.
- On Android the step budget and the maximum number of simultaneously wet tiles are lower
  ("gracefully reduce simulation complexity").

**Honesty.** This is a cellular approximation of the watercolour effects, not a shallow-water
fluid solver. Each effect is verified by a test that measures it:
- spread across wet;
- no spread across dry;
- edge-to-centre density ratio;
- granulation correlation with paper height;
- drying ends mobility;
- lifting lowers deposited pigment.

If an effect cannot be shown, it is reported as missing.

### A8. Procedural engines

Specification §10. Geometry is generated per dab from the seed and dab index (deterministic)
and rasterised into coverage tiles (and colour tiles for per-strand colour) with one shared
anti-aliased primitive set: tapered capsule polylines, filled ellipses and leaf lenses.
- **Strands (hair, fur):** k strands per dab, following the heading with lateral offset,
  length, curl (a constant curvature per strand), taper and colour jitter.
- **Grass:** blades rooted along the stroke, growing towards document "up" (or the tilt) with
  lean jitter, height and width ranges.
- **Foliage:** leaf lenses scattered around the path with rotation following the direction,
  size and colour jitter.
- **Splatter:** a main drop plus satellites with power-law sizes and a stretch along the
  direction.
- **Pattern:** a repeating motif (from the tip) stamped at fixed intervals along the arc length,
  aligned to the path.
- **Hatching:** crosshatch passes. **Spray:** Gaussian distribution.

A per-stroke shape cache is added only if profiling shows rasterisation dominates (H-C1).

### A9. Compatibility rules

- **Existing presets render bit-identically.** This is enforced by:
  - the existing goldens (unchanged, never regenerated);
  - `tiledLayersPaintExactlyLikeDenseImages`;
  - a new test rendering every built-in preset and a set of milestone 9 feature strokes with the
    milestone 10 engine output stored as goldens before the refactor, compared exactly.
- New behaviour is opt-in through new settings whose defaults reproduce milestone 10 exactly.
- New engines get golden images with justified tolerances only where floating point makes
  exactness inappropriate (watercolour). Exact comparison elsewhere.

### A10. Presets and the brush experience

Specification §12:
- presets gain a **category**, so the panel can group them;
- the panel gains **search**;
- duplicate, rename, delete, import and export exist (milestone 9), and are kept and exposed in
  the panel;
- thumbnails exist;
- new built-in presets for each engine, authored for DrawApp;
- the Studio's registry-driven rows pick up the new properties automatically.

## 4. Property registry policy

The 120-property design target was the milestone 9 plan. Milestone 11 adds the properties its
engines need, in new groups (Dry Media, Bristle, Watercolour, Procedural, Stroke Geometry).
Each new property:
- has a control (registry-driven), serialisation (registry) and a rendering effect;
- is listed with the engines it applies to;
- is covered by `everyImplementedPropertyChangesRendering`.

The report gives the counts honestly: old target versus new, implemented versus planned. No
property is counted as implemented because a slider exists.

## 5. Performance and memory plan

- Benchmarks against milestone 10 run like milestone 10's A/B: the baseline built from
  `dcde383` in a separate worktree, interleaved rounds, same session, with no other heavy
  process.
- Workloads: 2048 × 1536 × 10, 4096² × 8, sparse 8192², sparse 16384².
- Measures:
  - per-event median, p95 and maximum;
  - long-stroke totals;
  - time to first pixel;
  - peak memory;
  - simulation memory;
  - dirty-tile counts;
  - save/open;
  - undo memory.
- Brush sizes 12, 60 and 200 px.
- Existing presets must show no material regression (A1 is a refactor). New engines get their
  own budgets (H-B1, H-W1).

## 6. Coordination with the parallel milestone (Codex, milestone 10.5)

Files milestone 11 **does not modify**:
- `TiledImage`, `TileSnapshot`, `MemoryBudget`;
- `TransformSession`, `TransformApply`;
- `ViewPyramid`, `ViewRenderer`;
- `ProjectFile`, `UndoHistory`.

Milestone 11 uses their existing public interfaces only: `StrokeSurface` row access,
`TileSnapshot::capture/storedRow/swapWith`, and `MemoryBudget::tryReserve/release` with an
existing category.

Integration points milestone 11 **does** modify (brush-side, narrow):
- `StrokeEngine`, `BrushSettings`, `BrushProperties`, `BrushPresets`, `BrushTips`;
- new `src/core` brush files;
- `BrushLibrary`, `BrushPanel.qml`, `BrushStudio*`;
- `CanvasItem`, only to keep a wet stroke open while it dries and to commit it before other
  actions (A7), plus the save notice.

**Required interface changes in Codex-owned subsystems: none identified.** If one arises, work
stops before the change and it is reported (specification §2).

## 7. Stages

| Stage | Content | Exit check |
|---|---|---|
| 1 | Verification and research (this document and the six research files) | documents committed |
| 2 | Deposit interface; existing engines behind it; milestone 10 reference goldens; benchmark baseline | all tests pass; exact equality with milestone 10 for every built-in preset and milestone 9 feature stroke |
| 3 | Dry media | tooth, build-up, pressure, seam, seed, transparency, large-canvas tests; goldens |
| 4 | Ink | curves, corners, slow/fast, long, starts/ends, high-frequency input, undo; existing ink presets identical |
| 5 | Smudge and mixing | four operations distinguishable; transparency, seams, selections; no per-dab allocation |
| 6 | Bristle | dry vs loaded, flat vs round, across paint, depletion; per-event budget |
| 7 | Watercolour | the effect tests of A7; save/reopen, undo, memory pressure, reproducibility |
| 8 | Procedural | determinism, direction following, density, pressure |
| 9 | Registry audit, preset categories and search, migration tests | every exposed setting changes pixels |
| 10 | Performance A/B against milestone 10 | documented numbers |
| 11 | Windows and Android builds, emulator workflows | build and test logs, emulator notes |
| 12 | Documentation and report | report with evidence levels |

Each stage ends with tests passing and a local commit; nothing is pushed.

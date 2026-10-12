# Milestone 9 architecture decisions

Each decision records the context, what was decided, the evidence behind it and its
consequences. The research behind them is in `milestone9-krita-brush-engine.md`,
`milestone9-photocraft-architecture.md` and `milestone9-procreate-brush-studio.md`. The baseline
is commit `22ad186` (milestone 8), with 7 suites and 287 tests passing and benchmarks in
`docs/benchmarks/m9-baseline-bench_core.md`.

## D1. Keep full-canvas layer images this milestone

**Context:** Krita and PhotoCraft store layers as sparse copy-on-write tiles. DrawApp stores one
full-canvas `QImage` per layer, with a 512 MiB document limit (`DocumentLimits::MaxPixelBytes`).

**Decision:** no storage rewrite in milestone 9.

**Evidence:**
- Painting, compositing and recovery costs at 2048 × 1536 are measured and acceptable.
- A storage rewrite touches every subsystem (files, recovery, undo, transforms, brushes): very
  high risk for a milestone whose priorities are brushes and multi-layer editing.

**Consequence:**
- 4096 × 4096 × 20 layers (1.28 GiB of layer pixels) exceeds the limit and is **not supported**.
  Benchmarks use the largest allowed configuration (4096 × 4096 × 8).
- Tiled sparse storage is the recommended next structural milestone.

## D2. One stroke engine, several dab renderers (no plugins)

**Context:** Krita registers engines as plugins (`KisPaintOpRegistry`).

**Decision:**
- `StrokeEngine` keeps the path walking, sensors, dynamics, selection and compositing.
- The engine type (`Pixel`, `Smudge`, `WetPaint`, `Spray`, `Hatching`) chooses the dab renderer.
- Erase remains a blend of the Pixel engine.

**Why:** shared input and compositing code means every engine respects the selection once, gets
undo and recovery for free, and is tested by the same harness. A plugin system is not justified
(AGENTS.md: no frameworks for hypothetical needs).

## D3. A data-driven sensor framework (Krita model, DrawApp combination)

**Decision:**
- Parameters are `ParameterDynamics { enabled, minimum, combine, mappings[] }`; a mapping is
  `{ sensor, curve }`.
- Sensors: pressure, tilt X, tilt Y, tilt magnitude, tilt direction, stroke direction, speed,
  acceleration, distance, fade (progress over a length), time, random per dab, random per stroke.
- Curves are up to 16 points, evaluated through a 256-entry table (as Krita's `floatTransfer(256)`).
- Combination rules: multiply (default), add (clamped), max, min, difference (Krita's five).
- Output: `minimum + (1 − minimum) × combined`, clamped to 0..1, then mapped to the parameter's
  range: size × factor, opacity × factor, or rotation + (factor − 0.5) × range for
  additive-angle parameters.

**Numerical rules:**
- Pressure is clamped to 0..1, and NaN reads as 1.
- Speed uses the time between samples. A time step of zero or negative (duplicate or
  out-of-order events) keeps the previous speed, and speed is clamped to the brush's maximum.
- Random values come from the stroke seed and the dab index, with a separate stream per
  parameter (from PhotoCraft).

**Missing hardware data:** without pressure the value is 1.0, as before. Without tilt, tilt
sensors read 0 (upright). Nothing is invented. Previews and tests may simulate pressure and are
labelled as simulated.

**Compatibility (revised during implementation):** the milestone 6 `pressureSize/Opacity/Flow`
responses (minimum, gamma) are kept as they are, as the *basic* pressure response that the quick
brush panel edits. The engine multiplies them with the advanced dynamics of the same parameter.
Converting them into curves would have changed every saved preset slightly (a 16-point curve only
approximates `p^gamma`) and would have forced the simple panel to edit curves. Version 1 presets
therefore load unchanged and render exactly as before (`docs/brush-preset-format.md`).

**Colour targets** need no separate amount: hue turns by value × 360° (a full reading is a full
turn, so the chosen colour), and saturation and lightness are multiplied by the value, like the
other multiplying targets.

## D4. A single property registry drives settings, serialisation, UI and documentation

**Decision:**
- Every brush property is declared once (`BrushProperties`): id, group, name, description, type,
  range, default, unit, engines, whether it accepts dynamics, and whether it is implemented.
- The Brush Studio builds its controls from the registry.
- The settings specification document is generated from the registry, so documentation and code
  cannot drift.

**"Implemented" is enforced by a test.** For each implemented numeric property, rendering a
standard stroke with the default and with a changed value must produce different pixels, using
a test brush on the property's engine. Properties without a demonstrable effect are not marked
implemented. 120 is a design target, not a claim.

## D5. Per-pixel stroke colour only when the colour varies

**Context:** the coverage model keeps one stroke colour (milestone 6). Colour dynamics need the
colour to vary per dab.

**Decision:**
- When any colour dynamic is active, coverage tiles also hold a stroke colour per pixel.
- A dab's contribution `ΔS` blends its colour in as `c ← (c·S_old + c_dab·ΔS) / S_new`.
- The composite uses the per-pixel colour.
- Without colour dynamics, nothing changes (same memory and speed as before).

## D6. Wet paint is a bounded prototype on the smudge machinery

**Decision:**
- `WetPaint` reuses the carried-patch design of smudge.
- The patch starts **charged** with the brush colour and loses paint with distance
  (`load length`).
- Each dab deposits patch paint (*attack*), picks up canvas paint (*pull*), and dilution lowers
  the deposited alpha.
- It writes through the selection once (the smudge work-tile path).
- It is called a "wet paint prototype": no fluid simulation, no drying, no paper absorption.

## D7. Multi-layer transforms: shared geometry, prepare then commit

**Decision:**
- `TransformSession` holds one geometry and one part per layer (base and source). The source
  rectangle is the union over layers, so all layers share one mapping and stay aligned.
- Apply first renders every layer's result into new images, catching `std::bad_alloc`; any
  failure leaves the document untouched.
- Only then does it swap the results in and record **one group undo command** (pixels of every
  layer plus the selection).
- A locked or hidden layer among the selected layers stops the transform with a message naming
  it. Krita skips it silently (`isEditable`), which breaks alignment.
- Masks and clipping groups do not exist in DrawApp, so they are not applicable. This is stated
  in the documentation, not implied.
- Layer selection is view state, not history (as in PhotoCraft).

## D8. Selection tools operate on the mask once

**Decision:**
- Polygon lasso, magic wand, grow, shrink and feather produce or modify the same 8-bit
  coverage mask, and every edit through the selection applies it once.
- Feather is a separable three-pass box blur (approximately Gaussian) applied to the mask
  itself, so it is never applied twice.
- Grow and shrink use an exact Euclidean distance transform (Felzenszwalb–Huttenlocher) of the
  50 % boundary, with an anti-aliased edge.

## D9. Native brush preset format, version 2

**Decision:**
- A preset is JSON (`"format": "drawapp-brush"`, `"version": 2`): engine, flat property map,
  dynamics, resources (custom tips and textures as embedded PNG), metadata.
- Unknown keys are kept and written back unchanged.
- Version 1 data migrates step-wise.
- Malformed input is rejected or clamped per property.
- Missing resources fall back to the built-in round tip, or no texture, with a warning.
- Single presets are exchanged as `.drawbrush` files.
- No `.kpp` or `.brush` importer is claimed.

## D10. Brush Studio edits a copy

**Decision:**
- The studio opens on a copy of the selected preset's working settings. Its drawing pad uses
  that copy.
- **Done** replaces the working settings; Save writes the preset. **Cancel** discards the copy,
  so the preset is unchanged.
- Every setting has its own reset; the preset can be reset as a whole.
- Advanced groups stay hidden until opened; the quick brush panel stays as it is.

## D11. No GPU path and no asynchronous dab queue yet

**Decision:** no GPU work and no asynchronous dab queue this milestone. This is revisited only
with benchmark evidence of a CPU bottleneck in the new engines (dense spray, large textured
brushes).

# Brush settings specification

Generated from the property registry (`src/core/BrushProperties.cpp`) by `tst_brushengine::specificationIsCurrent` (run with `DRAWAPP_WRITE_SPEC=1` to rewrite it). Do not edit by hand.

A property counts as **implemented** only if it changes rendering: `tst_brushengine::everyImplementedPropertyChangesRendering` renders a standard stroke with the default and a changed value of every implemented property, on an engine it applies to, and requires different pixels. **Planned** properties have no control and no effect; they are the remaining part of the 120-property design target.

## Summary

| Group | Implemented | Planned | Total |
|---|---:|---:|---:|
| Brush Tip | 10 | 2 | 12 |
| Stroke Path | 8 | 2 | 10 |
| Pressure and Tilt | 10 | 2 | 12 |
| Opacity and Flow | 5 | 3 | 8 |
| Texture and Grain | 12 | 0 | 12 |
| Shape Dynamics | 11 | 1 | 12 |
| Color Dynamics | 9 | 1 | 10 |
| Blending and Smudge | 7 | 5 | 12 |
| Wet Paint | 6 | 4 | 10 |
| Stabilization | 4 | 4 | 8 |
| Taper | 6 | 2 | 8 |
| Special Effects | 6 | 0 | 6 |
| **Total** | **94** | **26** | **120** |

The two engine selectors (`blend`, `stamp`) are listed under Engine and not counted.

Common rules: values are clamped into their range on load and when set (NaN becomes the default); every random choice comes from the stroke seed and the dab index, so the same input and seed always give the same pixels; no sensor value is invented when the device does not report it (see `docs/brush-engine-architecture.md`).

## Brush Tip

### Tip shape — `tip`

The computed round tip or a generated tip image. An unknown id (from a newer version) draws round and is kept.

- Status: implemented (Brush Studio and quick panel)
- Type: option
- Range: Round / Flat bristle
- Default: Round
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Serialisation: `tip`: one of "", "bristle"
- Rendering: Round: analytic disc with hardness falloff. Image tips: bilinear sample of the nearest larger mip level.
- Cost: Per pixel of each dab: one analytic evaluation or one bilinear sample.
- Tests: tst_brush::generatedTipsAndTextures, tst_brushengine::everyImplementedPropertyChangesRendering

### Imported tip — `tipImage`

A tip made from an imported image (alpha, or darkness for opaque images), stored inside the preset so it can never go missing. Overrides the tip shape.

- Status: implemented (Brush Studio)
- Type: image
- Range: square, up to 256 × 256 px
- Default: none
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Serialisation: `tipImage`: base64 PNG (Grayscale8), "" for none
- Rendering: Like the generated image tips; mip levels and the flow pass length are built once per stroke.
- Cost: Per stroke: mip chain of at most 256 × 256 (well under 1 ms). Per pixel: one bilinear sample.
- Tests: tst_brushengine::importedTipIsUsedAndSaved, tst_brushengine::everyImplementedPropertyChangesRendering

### Size — `size`

Dab diameter at full pressure, in document pixels.

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 1 … 512 px
- Default: 12 px
- Engines: all
- Sensors: —
- Serialisation: `size`: JSON number
- Rendering: Every size change (pressure, tilt, dynamics, jitter, taper) multiplies this diameter.
- Cost: Dab cost grows with the square of the size.
- Tests: tst_brush::pressureRampTapersSize, tst_brushengine::everyImplementedPropertyChangesRendering

### Hardness — `hardness`

Round tip only: 100 % is a crisp anti-aliased edge; lower values fade smoothly from the core to the rim.

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 0 … 100 %
- Default: 100 %
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Has an effect with: Round tip
- Serialisation: `hardness`: JSON number
- Rendering: Falloff (1 − t²)² from the hard core to the rim; the flow pass length follows so flow stays calibrated.
- Cost: None beyond the tip evaluation.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Roundness — `roundness`

Squashes the tip across its angle; 100 % keeps it round.

- Status: implemented (Brush Studio)
- Type: number
- Range: 5 … 100 %
- Default: 100 %
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Serialisation: `roundness`: JSON number
- Rendering: The tip is sampled at (u, w / roundness) in its own rotated frame.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Angle — `angle`

Rotation of the tip. Directions and pen azimuth add to it when the tip follows them.

- Status: implemented (Brush Studio)
- Type: number
- Range: -360 … 360 °
- Default: 0 °
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Serialisation: `angle`: JSON number
- Rendering: Rotates the tip's sampling frame.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Flip horizontally — `flipX`

Mirrors the tip along its travel axis.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Has an effect with: An asymmetric tip image
- Serialisation: `flipX`: true/false
- Rendering: Negates the tip's u coordinate. No visible effect on a symmetric (round) tip.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Flip vertically — `flipY`

Mirrors the tip across its travel axis.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Has an effect with: An asymmetric tip image
- Serialisation: `flipY`: true/false
- Rendering: Negates the tip's w coordinate. No visible effect on a symmetric (round) tip.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Minimum size — `minimumSize`

No dab gets smaller than this, whatever pressure, tilt, taper, jitter or dynamics do (never more than the size).

- Status: implemented (Brush Studio)
- Type: number
- Range: 1 … 512 px
- Default: 1 px
- Engines: all
- Sensors: —
- Serialisation: `minimumSize`: JSON number
- Rendering: Clamps the final dab diameter from below.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Density — `tipDensity`

Share of the tip's pixels each dab paints; the rest are left out at random, differently for every dab.

- Status: implemented (Brush Studio)
- Type: number
- Range: 1 … 100 %
- Default: 100 %
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Serialisation: `tipDensity`: JSON number
- Rendering: Per pixel and dab, a hash of position and dab index decides whether the pixel is painted.
- Cost: Per pixel: one hash when below 100 %.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Spikes — `tipSpikes` (planned)

Planned: star-shaped round tips with this many points (Krita auto brush). Not implemented.

- Status: **planned, not implemented**
- Type: integer

### Edge noise — `tipEdgeNoise` (planned)

Planned: random roughness of the round tip's edge (Krita auto brush 'randomness'). Not implemented.

- Status: **planned, not implemented**
- Type: number

## Stroke Path

### Spacing — `spacing`

Distance between dab centres as a share of the dab diameter (at least 0.5 px).

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 2 … 200 %
- Default: 15 %
- Engines: all
- Sensors: —
- Serialisation: `spacing`: JSON number
- Rendering: Dabs are placed by distance along the input path; flow is normalised to the spacing so it does not change the stroke's darkness.
- Cost: Dabs per pixel of stroke = 1 / (spacing × diameter).
- Tests: tst_core::dabsAreEvenlySpaced, tst_brush::noGapsOnFastStrokes, tst_brushengine::everyImplementedPropertyChangesRendering

### Spacing jitter — `spacingJitter`

Varies each gap between dabs by up to this share of the spacing, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 90 %
- Default: 0 %
- Engines: all
- Sensors: —
- Serialisation: `spacingJitter`: JSON number
- Rendering: Multiplies each step by 1 ± jitter (deterministic per stroke seed).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Scatter — `scatter`

Moves each dab sideways off the path by up to this many dab diameters, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 400 % of size
- Default: 0 % of size
- Engines: paint, erase, wet paint, spray, hatching
- Sensors: —
- Serialisation: `scatter`: JSON number
- Rendering: Offsets the dab centre perpendicular to the direction of travel (and along it with both axes).
- Cost: Dirty area grows with the scatter.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Scatter both axes — `scatterBothAxes`

Scatter also moves dabs along the path, not only across it.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: paint, erase, wet paint, spray, hatching
- Sensors: —
- Has an effect with: Scatter above 0
- Serialisation: `scatterBothAxes`: true/false
- Rendering: Adds an independent random offset along the direction of travel.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Count — `count`

Dabs placed at every spacing step. Useful with scatter or jitter.

- Status: implemented (Brush Studio)
- Type: integer
- Range: 1 … 16 dabs
- Default: 1 dabs
- Engines: paint, erase, wet paint, spray, hatching
- Sensors: —
- Serialisation: `count`: JSON integer
- Rendering: Each step renders count dabs, each with its own random scatter and jitter. Not normalised: more dabs build up faster.
- Cost: Multiplies the dab work.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Count jitter — `countJitter`

Randomly drops up to this share of the count at each step.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: paint, erase, wet paint, spray, hatching
- Sensors: —
- Has an effect with: Count above 1
- Serialisation: `countJitter`: JSON number
- Rendering: Each step renders between count × (1 − jitter) and count dabs (at least one).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Airbrush — `airbrush`

Keeps adding dabs while the pen is held still (build-up).

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: paint, erase, spray, hatching
- Sensors: —
- Serialisation: `airbrush`: true/false
- Rendering: Dabs also come from elapsed time at the airbrush rate, at the last position.
- Cost: Up to rate dabs per second while held.
- Tests: tst_brush::airbrush*, tst_brushengine::everyImplementedPropertyChangesRendering

### Airbrush rate — `airbrushRate`

Dabs per second while the airbrush is held still.

- Status: implemented (Brush Studio)
- Type: number
- Range: 1 … 200 dabs/s
- Default: 30 dabs/s
- Engines: paint, erase, spray, hatching
- Sensors: —
- Has an effect with: Airbrush on
- Serialisation: `airbrushRate`: JSON number
- Rendering: Interval between time dabs = 1000 / rate ms.
- Cost: Linear in the rate.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Spacing in pixels — `pixelSpacing` (planned)

Planned: a fixed distance between dabs, independent of the size. Not implemented.

- Status: **planned, not implemented**
- Type: number

### Path wobble — `pathWobble` (planned)

Planned: smooth (low-frequency) sideways wander of the path, unlike per-dab scatter. Not implemented.

- Status: **planned, not implemented**
- Type: number

## Pressure and Tilt

### Pressure → size — `pressureSize`

Pen pressure changes the size: on/off, the size at the lightest pressure, and firmness.

- Status: implemented (Brush Studio and quick panel)
- Type: pressure response
- Range: minimum 0 … 100 %, firmness (gamma) 0.33 … 3
- Default: {"enabled":true,"gamma":1,"minimum":0.1}
- Engines: all
- Sensors: pressure
- Serialisation: `pressureSize`: {enabled, minimum, gamma}
- Rendering: value = minimum + (1 − minimum) × pressure^firmness; mouse and touch give 1 (no data is invented). Multiplies with the advanced dynamics of the same parameter.
- Cost: Per dab: one pow().
- Tests: tst_brush::pressure*, tst_brushengine::everyImplementedPropertyChangesRendering

### Pressure → opacity — `pressureOpacity`

Pen pressure changes the opacity ceiling (for smudge: the strength).

- Status: implemented (Brush Studio and quick panel)
- Type: pressure response
- Range: minimum 0 … 100 %, firmness (gamma) 0.33 … 3
- Default: {"enabled":false,"gamma":1,"minimum":0}
- Engines: all
- Sensors: pressure
- Serialisation: `pressureOpacity`: {enabled, minimum, gamma}
- Rendering: value = minimum + (1 − minimum) × pressure^firmness; mouse and touch give 1 (no data is invented). Multiplies with the advanced dynamics of the same parameter.
- Cost: Per dab: one pow().
- Tests: tst_brush::pressure*, tst_brushengine::everyImplementedPropertyChangesRendering

### Pressure → flow — `pressureFlow`

Pen pressure changes the flow (build-up brushes).

- Status: implemented (Brush Studio and quick panel)
- Type: pressure response
- Range: minimum 0 … 100 %, firmness (gamma) 0.33 … 3
- Default: {"enabled":false,"gamma":1,"minimum":0}
- Engines: paint, erase, spray, hatching
- Sensors: pressure
- Has an effect with: Build-up or airbrush
- Serialisation: `pressureFlow`: {enabled, minimum, gamma}
- Rendering: value = minimum + (1 − minimum) × pressure^firmness; mouse and touch give 1 (no data is invented). Multiplies with the advanced dynamics of the same parameter.
- Cost: Per dab: one pow().
- Tests: tst_brush::pressure*, tst_brushengine::everyImplementedPropertyChangesRendering

### Size at full tilt — `tiltSize`

Size multiplier when the pen is tilted fully (pens that report tilt only).

- Status: implemented (Brush Studio)
- Type: number
- Range: 25 … 400 %
- Default: 100 %
- Engines: all
- Sensors: tilt amount
- Has an effect with: A pen that reports tilt
- Serialisation: `tiltSize`: JSON number
- Rendering: Factor 1 + (value − 1) × tilt amount.
- Cost: None.
- Tests: tst_brush::tiltScalesOnlyWhenReported, tst_brushengine::everyImplementedPropertyChangesRendering (simulated tilt)

### Opacity at full tilt — `tiltOpacity`

Opacity multiplier when the pen is tilted fully (pens that report tilt only).

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 100 %
- Engines: all
- Sensors: tilt amount
- Has an effect with: A pen that reports tilt
- Serialisation: `tiltOpacity`: JSON number
- Rendering: Factor 1 + (value − 1) × tilt amount, on the ceiling (smudge: strength).
- Cost: None.
- Tests: tst_brush::tiltScalesOnlyWhenReported, tst_brushengine::everyImplementedPropertyChangesRendering (simulated tilt)

### Full tilt angle — `fullTiltAngle`

The tilt from vertical that counts as full tilt, for the tilt factors and the tilt sensors.

- Status: implemented (Brush Studio)
- Type: number
- Range: 20 … 90 °
- Default: 60 °
- Engines: all
- Sensors: tilt
- Has an effect with: A pen that reports tilt
- Serialisation: `fullTiltAngle`: JSON number
- Rendering: tilt amount = min(1, tilt / angle).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering (simulated tilt)

### Tilt needed to steer — `tiltAzimuthThreshold`

A tip that follows direction uses the pen's azimuth only above this tilt; nearer upright it follows the stroke.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 60 °
- Default: 15 °
- Engines: all
- Sensors: tilt
- Has an effect with: Follow direction, a pen that reports tilt
- Serialisation: `tiltAzimuthThreshold`: JSON number
- Rendering: Chooses between pen azimuth and travel direction per dab.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering (simulated tilt)

### Size dynamics — `dynamics.size`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: all
- Sensors: all sensors
- Serialisation: `dynamics` → `size`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Diameter × value (before the minimum size).
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Opacity dynamics — `dynamics.opacity`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: all
- Sensors: all sensors
- Serialisation: `dynamics` → `opacity`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Ceiling × value (smudge: strength; wet paint: deposit).
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Flow dynamics — `dynamics.flow`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: paint, erase, spray, hatching
- Sensors: all sensors
- Has an effect with: Build-up or airbrush
- Serialisation: `dynamics` → `flow`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Per-pass flow × value.
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Barrel rotation — `barrelRotation` (planned)

Planned: pens that report barrel rotation (QTabletEvent::rotation) as a sensor. Not implemented; no test hardware.

- Status: **planned, not implemented**
- Type: dynamics

### Pressure calibration — `pressureCalibration` (planned)

Planned: a per-device pressure curve applied before every brush. Not implemented.

- Status: **planned, not implemented**
- Type: pressure response

## Opacity and Flow

### Opacity — `opacity`

The most paint a stroke can lay down. Overlaps within one stroke never exceed it.

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 1 … 100 %
- Default: 100 %
- Engines: all
- Sensors: —
- Serialisation: `opacity`: JSON number
- Rendering: Ceiling C of the coverage model, rounded to a whole 8-bit level.
- Cost: None.
- Tests: tst_brush::uniformNeverDarkensAndOpacityAppliesOnce, tst_brushengine::everyImplementedPropertyChangesRendering

### Flow — `flow`

How much of the way to the opacity one pass deposits (build-up and airbrush).

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 1 … 100 %
- Default: 100 %
- Engines: paint, erase, spray, hatching
- Sensors: —
- Has an effect with: Build-up or airbrush
- Serialisation: `flow`: JSON number
- Rendering: Per dab f = 1 − (1 − flow)^(step / pass length).
- Cost: Per dab: one pow().
- Tests: tst_brush::buildUpOnePassDepositsFlow, tst_brushengine::everyImplementedPropertyChangesRendering

### Accumulation — `accumulation`

Uniform: overlaps within a stroke never darken. Build up: slow or repeated passes darken towards the opacity.

- Status: implemented (Brush Studio and quick panel)
- Type: option
- Range: Uniform / Build up
- Default: Uniform
- Engines: paint, erase, spray, hatching
- Sensors: —
- Serialisation: `accumulation`: one of "uniform", "buildUp"
- Rendering: Uniform S = max(S, v·C); build-up S += v·f·(C − S) with stochastic rounding.
- Cost: Same.
- Tests: tst_brush::uniformNeverDarkensAndOpacityAppliesOnce, buildUpApproachesButNeverPassesCeiling, tst_brushengine::everyImplementedPropertyChangesRendering

### Opacity jitter — `opacityJitter`

Lowers each dab's opacity by up to this share, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: all
- Sensors: —
- Serialisation: `opacityJitter`: JSON number
- Rendering: Ceiling × (1 − jitter × random).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Flow jitter — `flowJitter`

Lowers each dab's flow by up to this share, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: paint, erase, spray, hatching
- Sensors: —
- Has an effect with: Build-up or airbrush
- Serialisation: `flowJitter`: JSON number
- Rendering: Pass flow × (1 − jitter × random).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Wet edges — `wetEdges` (planned)

Planned: darker stroke rims, like watercolour (Krita 'wet edges'). Not implemented.

- Status: **planned, not implemented**
- Type: number

### Intense glaze — `intenseGlaze` (planned)

Planned: overlaps within a stroke multiply instead of capping at the opacity. Not implemented.

- Status: **planned, not implemented**
- Type: boolean

### Luminance blending — `luminanceBlending` (planned)

Planned: blend the stroke's lightness only. Not implemented.

- Status: **planned, not implemented**
- Type: boolean

## Texture and Grain

### Texture — `texture`

A generated paper or canvas grain that breaks up the paint.

- Status: implemented (Brush Studio and quick panel)
- Type: option
- Range: None / Canvas weave / Paper tooth
- Default: None
- Engines: all
- Sensors: —
- Serialisation: `texture`: one of "", "canvas", "paper"
- Rendering: Per pixel the coverage is reduced in the texture's valleys (document space unless moving).
- Cost: Per pixel: one texture sample (nearest at scale 1 without rotation, bilinear otherwise).
- Tests: tst_brush::textureIsCoherentInDocumentSpace, subtractTextureFillsWithPressure, tst_brushengine::everyImplementedPropertyChangesRendering

### Texture scale — `textureScale`

Size of the grain.

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 10 … 800 %
- Default: 100 %
- Engines: all
- Sensors: —
- Has an effect with: A texture
- Serialisation: `textureScale`: JSON number
- Rendering: Texture coordinates divided by the scale (bilinear).
- Cost: Bilinear sampling when not 100 %.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Texture strength — `textureStrength`

How much the grain removes from the paint.

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: all
- Sensors: —
- Has an effect with: A texture
- Serialisation: `textureStrength`: JSON number
- Rendering: valley = strength × (1 − texture).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Texture mode — `textureMode`

Multiply scales the paint by the grain; tooth removes paint in the valleys first, so light marks break up and heavy ones fill in.

- Status: implemented (Brush Studio and quick panel)
- Type: option
- Range: Multiply / Tooth (pressure fills it)
- Default: Multiply
- Engines: all
- Sensors: —
- Has an effect with: A texture
- Serialisation: `textureMode`: one of "multiply", "subtract"
- Rendering: Multiply: s × (1 − valley). Subtract: opacity × max(0, s/opacity − valley).
- Cost: None.
- Tests: tst_brush::textureIsCoherentInDocumentSpace, subtractTextureFillsWithPressure, tst_brushengine::everyImplementedPropertyChangesRendering

### Texture rotation — `textureRotation`

Turns the grain.

- Status: implemented (Brush Studio)
- Type: number
- Range: -180 … 180 °
- Default: 0 °
- Engines: all
- Sensors: —
- Has an effect with: A texture
- Serialisation: `textureRotation`: JSON number
- Rendering: Rotates the texture coordinates (bilinear sampling).
- Cost: Bilinear sampling.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Texture brightness — `textureBrightness`

Lightens (fewer valleys) or darkens (more valleys) the grain.

- Status: implemented (Brush Studio)
- Type: number
- Range: -100 … 100 %
- Default: 0 %
- Engines: all
- Sensors: —
- Has an effect with: A texture
- Serialisation: `textureBrightness`: JSON number
- Rendering: t + brightness, after contrast, clamped to 0..1.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Texture contrast — `textureContrast`

Flattens (below 100 %) or sharpens (above) the grain.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 300 %
- Default: 100 %
- Engines: all
- Sensors: —
- Has an effect with: A texture
- Serialisation: `textureContrast`: JSON number
- Rendering: (t − 0.5) × contrast + 0.5.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Invert texture — `textureInvert`

Swaps the grain's peaks and valleys.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: all
- Sensors: —
- Has an effect with: A texture
- Serialisation: `textureInvert`: true/false
- Rendering: t → 1 − t before contrast and brightness.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Texture each dab — `textureEachDab`

Applies the grain to every dab instead of the finished stroke, so overlapping dabs fill the grain in (build-up). Texture strength dynamics always work this way.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: paint, erase, spray, hatching
- Sensors: —
- Has an effect with: A texture
- Serialisation: `textureEachDab`: true/false
- Rendering: The grain multiplies each dab's tip value instead of the stroke coverage at composite time.
- Cost: Per pixel of each dab: one texture sample (instead of one per pixel per event).
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Moving grain — `textureMoving`

The grain moves and turns with each dab (stamped), instead of staying fixed on the paper.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: paint, erase, spray, hatching
- Sensors: —
- Has an effect with: A texture, texture each dab
- Serialisation: `textureMoving`: true/false
- Rendering: Texture coordinates relative to the dab centre, rotated with the dab.
- Cost: Bilinear sampling.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Random grain offset — `textureRandomOffset`

Shifts the grain to a new random position for every stroke, so repeated strokes do not line up.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: all
- Sensors: —
- Has an effect with: A texture
- Serialisation: `textureRandomOffset`: true/false
- Rendering: Adds a per-stroke offset (from the stroke seed) to the texture coordinates.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Texture strength dynamics — `dynamics.textureStrength`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: paint, erase, spray, hatching
- Sensors: all sensors
- Has an effect with: A texture
- Serialisation: `dynamics` → `textureStrength`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Each dab's texture strength × value (switches the texture to each-dab).
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

## Shape Dynamics

### Follow direction — `followDirection`

The tip turns with the stroke, or with the pen's azimuth when it is tilted enough.

- Status: implemented (Brush Studio and quick panel)
- Type: boolean
- Range: on / off
- Default: off
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: direction, tilt direction
- Serialisation: `followDirection`: true/false
- Rendering: Adds the smoothed travel direction (or the azimuth) to the angle.
- Cost: None.
- Tests: tst_brush::tipFollowsDirectionOrTilt, tst_brushengine::everyImplementedPropertyChangesRendering

### Angle jitter — `angleJitter`

Turns each dab by up to ± this angle, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 180 °
- Default: 0 °
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Serialisation: `angleJitter`: JSON number
- Rendering: Adds a random angle per dab.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Size jitter — `sizeJitter`

Shrinks each dab by up to this share, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: all
- Sensors: —
- Serialisation: `sizeJitter`: JSON number
- Rendering: Diameter × (1 − jitter × random).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Size jitter per stroke — `strokeSizeJitter`

Shrinks the whole stroke by up to this share, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: all
- Sensors: —
- Serialisation: `strokeSizeJitter`: JSON number
- Rendering: Diameter × (1 − jitter × random per stroke).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Roundness jitter — `roundnessJitter`

Squashes each dab by up to this share, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Serialisation: `roundnessJitter`: JSON number
- Rendering: Roundness × (1 − jitter × random), at least 5 %.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Random flip horizontally — `randomFlipX`

Mirrors about half the dabs along the travel axis, at random.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Has an effect with: An asymmetric tip image
- Serialisation: `randomFlipX`: true/false
- Rendering: Per dab, toggles flip horizontally.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Random flip vertically — `randomFlipY`

Mirrors about half the dabs across the travel axis, at random.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: —
- Has an effect with: An asymmetric tip image
- Serialisation: `randomFlipY`: true/false
- Rendering: Per dab, toggles flip vertically.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Rotation dynamics — `dynamics.rotation`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: all sensors
- Serialisation: `dynamics` → `rotation`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Adds value × 360° to the angle.
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Roundness dynamics — `dynamics.roundness`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: paint, erase, smudge, wet paint, hatching
- Sensors: all sensors
- Serialisation: `dynamics` → `roundness`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Roundness × value (at least 5 %).
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Scatter dynamics — `dynamics.scatter`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: paint, erase, wet paint, spray, hatching
- Sensors: all sensors
- Has an effect with: Scatter above 0
- Serialisation: `dynamics` → `scatter`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Scatter × value.
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Spacing dynamics — `dynamics.spacing`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: all
- Sensors: all sensors
- Serialisation: `dynamics` → `spacing`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: The step after each dab × value (at least 0.5 px); flow stays normalised to the actual step.
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Tip sequence — `tipAnimation` (planned)

Planned: cycle through several tip images along the stroke (Krita animated tips). Not implemented.

- Status: **planned, not implemented**
- Type: text

## Color Dynamics

### Hue jitter — `hueJitter`

Turns each dab's hue by up to ± this angle, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 180 °
- Default: 0 °
- Engines: paint, wet paint, spray, hatching
- Sensors: —
- Serialisation: `hueJitter`: JSON number
- Rendering: Each dab gets its own colour; the stroke keeps a colour per pixel, mixed by coverage.
- Cost: Per touched tile: 16 KiB of colour; per pixel of each dab: a weighted mix.
- Tests: tst_brushengine::colorDynamicsVaryPerDab, tst_brushengine::everyImplementedPropertyChangesRendering

### Saturation jitter — `saturationJitter`

Changes each dab's saturation by up to ± this amount, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: paint, wet paint, spray, hatching
- Sensors: —
- Serialisation: `saturationJitter`: JSON number
- Rendering: Each dab gets its own colour; the stroke keeps a colour per pixel, mixed by coverage.
- Cost: Per touched tile: 16 KiB of colour; per pixel of each dab: a weighted mix.
- Tests: tst_brushengine::colorDynamicsVaryPerDab, tst_brushengine::everyImplementedPropertyChangesRendering

### Lightness jitter — `lightnessJitter`

Changes each dab's lightness by up to ± this amount, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: paint, wet paint, spray, hatching
- Sensors: —
- Serialisation: `lightnessJitter`: JSON number
- Rendering: Each dab gets its own colour; the stroke keeps a colour per pixel, mixed by coverage.
- Cost: Per touched tile: 16 KiB of colour; per pixel of each dab: a weighted mix.
- Tests: tst_brushengine::colorDynamicsVaryPerDab, tst_brushengine::everyImplementedPropertyChangesRendering

### Hue jitter per stroke — `strokeHueJitter`

Turns the whole stroke's hue by up to ± this angle, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 180 °
- Default: 0 °
- Engines: paint, wet paint, spray, hatching
- Sensors: —
- Serialisation: `strokeHueJitter`: JSON number
- Rendering: Changes the stroke colour once, from the stroke seed (no per-pixel colour needed).
- Cost: None.
- Tests: tst_brushengine::colorDynamicsVaryPerDab, tst_brushengine::everyImplementedPropertyChangesRendering

### Saturation jitter per stroke — `strokeSaturationJitter`

Changes the stroke's saturation by up to ± this amount.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: paint, wet paint, spray, hatching
- Sensors: —
- Serialisation: `strokeSaturationJitter`: JSON number
- Rendering: Changes the stroke colour once, from the stroke seed (no per-pixel colour needed).
- Cost: None.
- Tests: tst_brushengine::colorDynamicsVaryPerDab, tst_brushengine::everyImplementedPropertyChangesRendering

### Lightness jitter per stroke — `strokeLightnessJitter`

Changes the stroke's lightness by up to ± this amount.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: paint, wet paint, spray, hatching
- Sensors: —
- Serialisation: `strokeLightnessJitter`: JSON number
- Rendering: Changes the stroke colour once, from the stroke seed (no per-pixel colour needed).
- Cost: None.
- Tests: tst_brushengine::colorDynamicsVaryPerDab, tst_brushengine::everyImplementedPropertyChangesRendering

### Hue dynamics — `dynamics.hue`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: paint, wet paint, spray, hatching
- Sensors: all sensors
- Serialisation: `dynamics` → `hue`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Turns the hue by value × 360° (a full reading is a full turn: the chosen hue).
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Saturation dynamics — `dynamics.saturation`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: paint, wet paint, spray, hatching
- Sensors: all sensors
- Serialisation: `dynamics` → `saturation`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Saturation × value.
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Lightness dynamics — `dynamics.lightness`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: paint, wet paint, spray, hatching
- Sensors: all sensors
- Serialisation: `dynamics` → `lightness`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Lightness × value.
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Secondary colour mix — `secondaryColor` (planned)

Planned: mix towards a second colour by a sensor. DrawApp has no secondary colour yet. Not implemented.

- Status: **planned, not implemented**
- Type: dynamics

## Blending and Smudge

### Blend mode — `paintMode`

How the stroke's colour combines with the layer: over it, multiplied (darkens), screened (lightens), behind existing paint, or only on existing paint without changing its alpha.

- Status: implemented (Brush Studio)
- Type: option
- Range: Normal / Multiply / Screen / Behind / Inside (preserve alpha)
- Default: Normal
- Engines: paint, spray, hatching
- Sensors: —
- Serialisation: `paintMode`: one of "normal", "multiply", "screen", "behind", "preserveAlpha"
- Rendering: W3C compositing formulas on premultiplied pixels with the stroke coverage as source alpha.
- Cost: Per composited pixel: a few multiplies.
- Tests: tst_brushengine::paintModes, tst_brushengine::everyImplementedPropertyChangesRendering

### Smudge strength — `smudgeStrength`

How strongly the carried paint is laid down, per pass.

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 0 … 100 %
- Default: 60 %
- Engines: smudge
- Sensors: —
- Serialisation: `smudgeStrength`: JSON number
- Rendering: result = pixel + (carried − pixel) × strength × tip, normalised to the spacing.
- Cost: Per pixel of each dab: one mix.
- Tests: tst_brush::smudgeCarriesColour, smudgeTransparencyRules, tst_brushengine::everyImplementedPropertyChangesRendering

### Pickup — `smudgePickup`

How quickly the carried paint takes on the colour it passes (short smears at high values).

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 0 … 100 %
- Default: 25 %
- Engines: smudge
- Sensors: —
- Serialisation: `smudgePickup`: JSON number
- Rendering: carried += (pixel − carried) × pickup × tip, normalised to the spacing.
- Cost: Per pixel of each dab: one mix.
- Tests: tst_brush::smudgeCarriesColour, smudgeTransparencyRules, tst_brushengine::everyImplementedPropertyChangesRendering

### Smudge mode — `smudgeMode`

Smear drags the paint pixel by pixel; dulling carries one averaged colour (softer, blends to flat colour).

- Status: implemented (Brush Studio)
- Type: option
- Range: Smear / Dulling
- Default: Smear
- Engines: smudge
- Sensors: —
- Serialisation: `smudgeMode`: one of "smear", "dulling"
- Rendering: Dulling replaces the carried patch by its coverage-weighted average after each pickup.
- Cost: Dulling: one extra pass over the patch per dab.
- Tests: tst_brushengine::smudgeModes, tst_brushengine::everyImplementedPropertyChangesRendering

### Colour rate — `smudgeColorRate`

Mixes the brush colour into the carried paint on every pass (paints while smudging).

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: smudge
- Sensors: —
- Serialisation: `smudgeColorRate`: JSON number
- Rendering: carried += (brush colour − carried) × rate × tip, normalised to the spacing.
- Cost: Per pixel of each dab: one mix.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Smudge colour only — `smudgePreserveAlpha`

Moves colour but keeps each pixel's alpha, so smudging never spreads paint into transparency.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: smudge, wet paint
- Sensors: —
- Serialisation: `smudgePreserveAlpha`: true/false
- Rendering: The result keeps the pixel's alpha; colour is mixed in unpremultiplied form.
- Cost: A divide per pixel.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Smudge strength dynamics — `dynamics.smudgeStrength`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: smudge, wet paint
- Sensors: all sensors
- Serialisation: `dynamics` → `smudgeStrength`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Strength (wet paint: attack) × value.
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

### Sample all layers — `sampleMerged` (planned)

Planned: smudge picks up colour from all visible layers. Not implemented.

- Status: **planned, not implemented**
- Type: boolean

### Smudge radius — `smudgeRadius` (planned)

Planned: dulling samples an area larger or smaller than the dab. Not implemented.

- Status: **planned, not implemented**
- Type: number

### Overlay and colour modes — `moreBlendModes` (planned)

Planned: overlay, soft light, hue, saturation, colour and luminosity blend modes. Not implemented.

- Status: **planned, not implemented**
- Type: option

### Pigment mixing — `pigmentMixing` (planned)

Planned: subtractive (paint-like) colour mixing for smudge and wet paint. Not implemented.

- Status: **planned, not implemented**
- Type: boolean

### Blur mode — `blurMode` (planned)

Planned: soften pixels in place instead of carrying paint. Not implemented.

- Status: **planned, not implemented**
- Type: boolean

## Wet Paint

### Load — `wetLoad`

How much brush colour the brush carries at the start of a stroke.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 100 %
- Engines: wet paint
- Sensors: —
- Serialisation: `wetLoad`: JSON number
- Rendering: The patch is refilled towards the brush colour by load × (remaining share of the load length) per pass.
- Cost: Per pixel of each dab: two mixes (deposit, pick up); per dab: one refill of the patch.
- Tests: tst_brushengine::wetPaint*, tst_brushengine::everyImplementedPropertyChangesRendering

### Load length — `wetLoadLength`

How far the brush paints before the load runs out (then it only drags paint). 0 never runs out.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 10000 px
- Default: 600 px
- Engines: wet paint
- Sensors: —
- Serialisation: `wetLoadLength`: JSON number
- Rendering: remaining = max(0, 1 − distance / length).
- Cost: Per pixel of each dab: two mixes (deposit, pick up); per dab: one refill of the patch.
- Tests: tst_brushengine::wetPaint*, tst_brushengine::everyImplementedPropertyChangesRendering

### Attack — `wetAttack`

How much of the carried paint each pass lays down.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 60 %
- Engines: wet paint
- Sensors: —
- Serialisation: `wetAttack`: JSON number
- Rendering: Carried paint is composited over the pixel with alpha attack × tip, normalised to the spacing.
- Cost: Per pixel of each dab: two mixes (deposit, pick up); per dab: one refill of the patch.
- Tests: tst_brushengine::wetPaint*, tst_brushengine::everyImplementedPropertyChangesRendering

### Pull — `wetPull`

How much canvas paint each pass picks up and drags along.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 30 %
- Engines: wet paint
- Sensors: —
- Serialisation: `wetPull`: JSON number
- Rendering: carried += (pixel − carried) × pull × tip, normalised to the spacing.
- Cost: Per pixel of each dab: two mixes (deposit, pick up); per dab: one refill of the patch.
- Tests: tst_brushengine::wetPaint*, tst_brushengine::everyImplementedPropertyChangesRendering

### Dilution — `wetDilution`

Thins the brush paint (water): lower alpha, so it glazes and mixes more.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: wet paint
- Sensors: —
- Serialisation: `wetDilution`: JSON number
- Rendering: The refill colour's alpha × (1 − dilution).
- Cost: Per pixel of each dab: two mixes (deposit, pick up); per dab: one refill of the patch.
- Tests: tst_brushengine::wetPaint*, tst_brushengine::everyImplementedPropertyChangesRendering

### Wetness jitter — `wetnessJitter`

Varies the dilution from dab to dab, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: wet paint
- Sensors: —
- Serialisation: `wetnessJitter`: JSON number
- Rendering: Dilution + jitter × random per dab, clamped to 1.
- Cost: Per pixel of each dab: two mixes (deposit, pick up); per dab: one refill of the patch.
- Tests: tst_brushengine::wetPaint*, tst_brushengine::everyImplementedPropertyChangesRendering

### Grade — `wetGrade` (planned)

Planned: pressure-dependent load (Procreate 'grade'). Not implemented.

- Status: **planned, not implemented**
- Type: number

### Blur — `wetBlur` (planned)

Planned: blur of the carried paint while mixing. Not implemented.

- Status: **planned, not implemented**
- Type: number

### Blur jitter — `wetBlurJitter` (planned)

Planned. Not implemented.

- Status: **planned, not implemented**
- Type: number

### Drying time — `dryingTime` (planned)

Planned: paint that stays wet across strokes and dries over time. Not implemented (no persistent wetness layer).

- Status: **planned, not implemented**
- Type: number

## Stabilization

### Smoothing — `smoothing`

Pull-string stabiliser: the brush follows the pointer on a string, removing wobble (in screen pixels, so it feels the same at every zoom).

- Status: implemented (Brush Studio and quick panel)
- Type: integer
- Range: 0 … 100
- Default: 0
- Engines: all
- Sensors: —
- Serialisation: `smoothing`: JSON integer
- Rendering: Brush point moves only when the pointer is farther than the string length.
- Cost: Walks the input in 0.5 px steps.
- Tests: tst_core::smoothing*, tst_brushengine::everyImplementedPropertyChangesRendering

### Pressure smoothing — `pressureSmoothing`

Evens out jumpy pressure readings (time constant up to 100 ms).

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: all
- Sensors: pressure
- Has an effect with: A pen that reports pressure
- Serialisation: `pressureSmoothing`: JSON number
- Rendering: Exponential smoothing of pressure by event time (per event without times).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering (simulated pressure)

### Jitter filter — `jitterFilter`

Ignores pointer moves shorter than this (hand tremor); the end point is always drawn.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 20 px
- Default: 0 px
- Engines: all
- Sensors: —
- Serialisation: `jitterFilter`: JSON number
- Rendering: Input samples closer than the distance to the last accepted one are held back.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Finish at the pen — `smoothingFinish`

With smoothing, lifting the pen draws the remaining line to where it was lifted. Off: the line ends where the string is.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: on
- Engines: all
- Sensors: —
- Has an effect with: Smoothing above 0
- Serialisation: `smoothingFinish`: true/false
- Rendering: Pen-up completes (or drops) the smoothing string's tail.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Speed-adaptive smoothing — `streamline` (planned)

Planned: more smoothing for slow strokes, less for fast ones. Not implemented.

- Status: **planned, not implemented**
- Type: number

### Motion prediction — `motionPrediction` (planned)

Planned: draws slightly ahead of the pointer to hide latency. Not implemented.

- Status: **planned, not implemented**
- Type: boolean

### Averaging stabiliser — `stabilizerWindow` (planned)

Planned: weighted moving average of the last N samples (Krita 'stabilizer'). Not implemented.

- Status: **planned, not implemented**
- Type: integer

### Straight line snap — `lineSnap` (planned)

Planned: hold at the end of a stroke to straighten it. Not implemented.

- Status: **planned, not implemented**
- Type: boolean

## Taper

### Start taper — `taperStart`

The stroke grows from a thin tip to full size over this length.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 2000 px
- Default: 0 px
- Engines: all
- Sensors: —
- Serialisation: `taperStart`: JSON number
- Rendering: Size (and optionally opacity) × start size + (1 − start size) × t^shape, t = distance / length.
- Cost: None.
- Tests: tst_brushengine::taper*, tst_brushengine::everyImplementedPropertyChangesRendering

### End taper — `taperEnd`

The stroke thins to a tip over its last this-many pixels. The last part of the line is drawn when the pen lifts (that is when its end is known).

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 2000 px
- Default: 0 px
- Engines: all
- Sensors: —
- Serialisation: `taperEnd`: JSON number
- Rendering: Input within the taper length of the newest point is held back; at pen up it is drawn with the taper from the now known end.
- Cost: The live line lags by the taper length.
- Tests: tst_brushengine::taper*, tst_brushengine::everyImplementedPropertyChangesRendering

### Start tip size — `taperStartSize`

Size at the very start of a start taper, as a share of the size.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 10 %
- Engines: all
- Sensors: —
- Has an effect with: Start taper above 0
- Serialisation: `taperStartSize`: JSON number
- Rendering: See start taper.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### End tip size — `taperEndSize`

Size at the very end of an end taper, as a share of the size.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 10 %
- Engines: all
- Sensors: —
- Has an effect with: End taper above 0
- Serialisation: `taperEndSize`: JSON number
- Rendering: See end taper.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Taper opacity — `taperOpacity`

How much the tapers also fade the opacity.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: all
- Sensors: —
- Has an effect with: A taper
- Serialisation: `taperOpacity`: JSON number
- Rendering: Ceiling × (1 − amount × (1 − taper factor)).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Taper shape — `taperShape`

Below 1 the taper swells quickly (blunt); above 1 it stays thin longer (sharp).

- Status: implemented (Brush Studio)
- Type: number
- Range: 0.25 … 4
- Default: 1
- Engines: all
- Sensors: —
- Has an effect with: A taper
- Serialisation: `taperShape`: JSON number
- Rendering: The exponent on t.
- Cost: Per dab: one pow().
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Taper only without pressure — `taperPenOnly` (planned)

Planned: separate taper for mouse/touch and pressure pens (Procreate). Not implemented.

- Status: **planned, not implemented**
- Type: boolean

### Tip sharpness — `taperTipSharpness` (planned)

Planned: roundness change towards the taper tip. Not implemented.

- Status: **planned, not implemented**
- Type: number

## Special Effects

### Spray particles — `sprayDensity`

Particles in each spray dab.

- Status: implemented (Brush Studio)
- Type: number
- Range: 1 … 500 per dab
- Default: 30 per dab
- Engines: spray
- Sensors: —
- Has an effect with: Spray dab type
- Serialisation: `sprayDensity`: JSON number
- Rendering: Particles at uniformly random positions over the dab's disc.
- Cost: Linear in the particle count.
- Tests: tst_brushengine::sprayAndHatching, tst_brushengine::everyImplementedPropertyChangesRendering

### Particle size — `sprayParticleSize`

Diameter of each spray particle.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0.5 … 64 px
- Default: 2 px
- Engines: spray
- Sensors: —
- Has an effect with: Spray dab type
- Serialisation: `sprayParticleSize`: JSON number
- Rendering: Anti-aliased round particle.
- Cost: Quadratic in the particle size.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Particle size jitter — `sprayParticleJitter`

Shrinks each particle by up to this share, at random.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: spray
- Sensors: —
- Has an effect with: Spray dab type
- Serialisation: `sprayParticleJitter`: JSON number
- Rendering: Particle diameter × (1 − jitter × random).
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Hatching angle — `hatchAngle`

Direction of the hatching lines.

- Status: implemented (Brush Studio)
- Type: number
- Range: -180 … 180 °
- Default: 45 °
- Engines: hatching
- Sensors: —
- Has an effect with: Hatching dab type
- Serialisation: `hatchAngle`: JSON number
- Rendering: Lines fixed in document space, so dabs join into continuous hatching.
- Cost: Per pixel: one distance.
- Tests: tst_brushengine::sprayAndHatching, tst_brushengine::everyImplementedPropertyChangesRendering

### Hatching spacing — `hatchSpacing`

Distance between hatching line centres.

- Status: implemented (Brush Studio)
- Type: number
- Range: 2 … 100 px
- Default: 6 px
- Engines: hatching
- Sensors: —
- Has an effect with: Hatching dab type
- Serialisation: `hatchSpacing`: JSON number
- Rendering: Line period.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

### Hatching thickness — `hatchThickness`

Width of each hatching line.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0.5 … 50 px
- Default: 1.5 px
- Engines: hatching
- Sensors: —
- Has an effect with: Hatching dab type
- Serialisation: `hatchThickness`: JSON number
- Rendering: Anti-aliased line coverage.
- Cost: None.
- Tests: tst_brushengine::everyImplementedPropertyChangesRendering

## Engine

### Engine — `blend`

What the brush does to the layer: paint colour, erase, smudge existing paint, or wet paint (carries and mixes paint).

- Status: implemented (Brush Studio)
- Type: option
- Range: Paint / Erase / Smudge / Wet paint
- Default: Paint
- Engines: all
- Sensors: —
- Serialisation: `blend`: one of "paint", "erase", "smudge", "wet"
- Rendering: Selects the renderer: coverage composite (paint, erase) or carried patch (smudge, wet paint).
- Cost: Smudge and wet paint touch pixels per dab; paint and erase recomposite each pixel once per input event.
- Tests: tst_brush, tst_brushengine (engine suites), tst_brushengine::everyImplementedPropertyChangesRendering

### Dab type — `stamp`

What one dab of a paint or erase brush is: the tip, a spray of particles, or hatching lines.

- Status: implemented (Brush Studio)
- Type: option
- Range: Tip / Spray / Hatching
- Default: Tip
- Engines: paint, erase, spray, hatching
- Sensors: —
- Has an effect with: Paint or erase engine
- Serialisation: `stamp`: one of "tip", "spray", "hatching"
- Rendering: Spray scatters round particles over the dab's disc; hatching draws parallel document-space lines inside the tip.
- Cost: Spray: per particle O(particle area). Hatching: as the tip plus one distance per pixel.
- Tests: tst_brushengine::sprayAndHatching


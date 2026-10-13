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
| Blending and Smudge | 9 | 3 | 12 |
| Wet Paint | 6 | 4 | 10 |
| Stabilization | 4 | 4 | 8 |
| Taper | 6 | 2 | 8 |
| Special Effects | 6 | 0 | 6 |
| Ink and Stroke | 4 | 0 | 4 |
| Dry Media | 9 | 0 | 9 |
| Mixing | 3 | 0 | 3 |
| Bristles | 8 | 0 | 8 |
| Watercolour | 7 | 0 | 7 |
| **Total** | **127** | **24** | **151** |

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
- Range: 0 … 400 %
- Default: 0 %
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
- Range: None / Canvas weave / Laid paper (pastel) / Paper tooth / Rough paper (cold press) / Smooth paper (bristol)
- Default: None
- Engines: all
- Sensors: —
- Serialisation: `texture`: one of "", "canvas", "laid", "paper", "rough", "smooth"
- Rendering: Per pixel the coverage is reduced in the texture's valleys (document space unless moving).
- Cost: Per pixel: one texture sample (nearest at scale 1 without rotation, bilinear otherwise).
- Tests: tst_brush::textureIsCoherentInDocumentSpace, subtractTextureFillsWithPressure, tst_brushengine::everyImplementedPropertyChangesRendering

### Texture scale — `textureScale`

Size of the grain.

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 10 … 800 %
- Default: 100 %
- Engines: paint, erase, smudge, wet paint, spray, hatching
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
- Engines: paint, erase, smudge, wet paint, spray, hatching
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
- Engines: paint, erase, smudge, wet paint, spray, hatching
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
- Engines: paint, erase, smudge, wet paint, spray, hatching
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
- Engines: paint, erase, smudge, wet paint, spray, hatching
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
- Engines: paint, erase, smudge, wet paint, spray, hatching
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
- Engines: paint, erase, smudge, wet paint, spray, hatching
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

Smear drags the paint pixel by pixel; dulling carries one averaged colour (softer, blends to flat colour); mix (milestone 11) carries one colour that blends what it passes with what it carries and the brush colour, for as long as the smudge length says.

- Status: implemented (Brush Studio)
- Type: option
- Range: Smear / Dulling / Mix (one carried colour)
- Default: Smear
- Engines: smudge
- Sensors: —
- Serialisation: `smudgeMode`: one of "smear", "dulling", "mix"
- Rendering: Dulling replaces the carried patch by its coverage-weighted average after each pickup. Mix keeps one premultiplied colour, mixed with the centre-weighted average under each dab.
- Cost: Dulling: one extra pass over the patch per dab. Mix: one sampling pass and one deposit pass per dab.
- Tests: tst_brushengine::smudgeModes, tst_brushmedia::mix*, tst_brushengine::everyImplementedPropertyChangesRendering

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

### Smudge radius — `smudgeRadius`

The part of the dab colour is picked up from (milestone 11, for smudge mode Mix): smaller values sample the middle only, so the dab's edges do not pull in neighbouring colours. Dulling and smear sample the whole dab as before.

- Status: implemented (Brush Studio)
- Type: number
- Range: 25 … 100 %
- Default: 100 %
- Engines: smudge
- Sensors: —
- Has an effect with: Smudge mode Mix
- Serialisation: `smudgeRadius`: JSON number
- Rendering: Mix: the tip evaluated at the distance divided by this share weights the sample.
- Cost: None.
- Tests: tst_brushmedia::mix*, tst_brushengine::everyImplementedPropertyChangesRendering

### Overlay and colour modes — `moreBlendModes` (planned)

Planned: overlay, soft light, hue, saturation, colour and luminosity blend modes. Not implemented.

- Status: **planned, not implemented**
- Type: option

### Pigment mixing — `pigmentMixing`

Mixes colours like paint instead of light (milestone 11): blue and yellow give green rather than grey. Per-channel Kubelka–Munk in linear light; pure screen primaries mix towards black (three channels cannot hold pigment spectra).

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: smudge, wet paint
- Sensors: —
- Serialisation: `pigmentMixing`: true/false
- Rendering: Every colour mix of the carried paint (pickup, colour rate, lay-down; wet paint refill and pickup) goes through K/S = (1 − R)² / 2R per channel, weighted by alpha.
- Cost: Per pixel of each dab: three table look-ups and three square roots per mix.
- Tests: tst_brushmedia::mix*, tst_brushmedia::pigment*, tst_brushengine::everyImplementedPropertyChangesRendering

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

## Ink and Stroke

### Curve interpolation — `curveInterpolation`

Draws smooth curves between the pen's input samples instead of straight segments, so fast curves stay round. The line trails the pen by one input sample.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: all
- Sensors: —
- Serialisation: `curveInterpolation`: true/false
- Rendering: Centripetal Catmull–Rom segment through the neighbouring samples, subdivided about every pixel; pressure, tilt and time interpolated linearly.
- Cost: Per input sample: about one point per pixel of path (the spacer was already walking it).
- Tests: tst_brushmedia::ink*

### Corner angle — `cornerAngle`

Turns sharper than this stay sharp corners instead of being rounded by the curve.

- Status: implemented (Brush Studio)
- Type: number
- Range: 10 … 180 °
- Default: 70 °
- Engines: all
- Sensors: —
- Has an effect with: Curve interpolation
- Serialisation: `cornerAngle`: JSON number
- Rendering: At a corner the curve's tangent follows the segment itself (a straight start or end).
- Cost: Per input sample: one angle.
- Tests: tst_brushmedia::ink*

### Live end taper — `taperLive`

Draws the line right up to the pen while drawing and tapers the end when the pen lifts, instead of holding the last taper-length of the line back until then.

- Status: implemented (Brush Studio)
- Type: boolean
- Range: on / off
- Default: off
- Engines: paint, erase, spray, hatching
- Sensors: —
- Has an effect with: End taper above 0
- Serialisation: `taperLive`: true/false
- Rendering: At pen-up the last taper-length is spaced again with the taper and the tiles it touched are rebuilt from the stroke's recorded dabs.
- Cost: Per dab: about 150 bytes recorded for the stroke; at pen-up, the tail's tiles are rebuilt once.
- Tests: tst_brushmedia::ink*

### Speed-adaptive smoothing — `smoothingSpeedAdapt`

Shortens the smoothing string when the pen moves fast, so quick strokes follow the hand closely while slow, careful ones stay smooth.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: all
- Sensors: —
- Has an effect with: Smoothing above 0, a device that reports event times
- Serialisation: `smoothingSpeedAdapt`: JSON number
- Rendering: String length × (1 − value × min(1, pointer speed / 1.5 px/ms)), speed smoothed over about 30 ms.
- Cost: Per input sample: one distance and one exponential.
- Tests: tst_brushmedia::ink*

## Dry Media

### Penetration — `dryDepth`

How deep into the paper's tooth the medium reaches at full pressure: low values touch only the peaks (a light, grainy mark), high values fill the valleys too (a dense mark).

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 0 … 100 %
- Default: 80 %
- Engines: 
- Sensors: —
- Serialisation: `dryDepth`: JSON number
- Rendering: Per pixel of each dab: tip × smoothstep((depth − (1 − paper height)) / softness); the paper is the texture in document space, so repeated strokes meet the same tooth.
- Cost: Per pixel of each dab: one paper sample.
- Tests: tst_brushmedia::dry*

### Pressure → penetration — `pressureDepth`

How pressure changes the penetration: depth × (minimum + (1 − minimum) × pressure^firmness). A pen without pressure, mouse and touch draw at full depth.

- Status: implemented (Brush Studio)
- Type: pressure response
- Range: minimum 0 … 100 %, firmness (gamma) 0.33 … 3
- Default: {"enabled":true,"gamma":1,"minimum":0.15}
- Engines: 
- Sensors: pressure
- Has an effect with: A pen that reports pressure
- Serialisation: `pressureDepth`: {enabled, minimum, gamma}
- Rendering: Scales the dab's penetration before the paper comparison.
- Cost: Per dab: one power.
- Tests: tst_brushmedia::dry*

### Softness — `drySoftness`

Width of the transition between touched and untouched tooth: low values give a crisp, hard-lead grain, high values a soft, smudgy one.

- Status: implemented (Brush Studio)
- Type: number
- Range: 1 … 50 %
- Default: 12 %
- Engines: 
- Sensors: —
- Serialisation: `drySoftness`: JSON number
- Rendering: Divides the depth difference before the smoothstep.
- Cost: None.
- Tests: tst_brushmedia::dry*

### Streaks — `dryStreaks`

Grain that runs along the stroke, as charcoal and pastel leave it; it turns with the stroke direction.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: 
- Sensors: —
- Serialisation: `dryStreaks`: JSON number
- Rendering: The paper height is mixed with a streak field sampled in the dab's frame (along the heading).
- Cost: Per pixel of each dab: one bilinear sample of the streak field.
- Tests: tst_brushmedia::dry*

### Streak length — `dryStreakLength`

How elongated the streaks are along the stroke (1 = round grain).

- Status: implemented (Brush Studio)
- Type: number
- Range: 1 … 16 ×
- Default: 6 ×
- Engines: 
- Sensors: —
- Has an effect with: Streaks above 0
- Serialisation: `dryStreakLength`: JSON number
- Rendering: Stretches the streak field along the heading.
- Cost: None.
- Tests: tst_brushmedia::dry*

### Streak size — `dryStreakScale`

Size of the streak grain.

- Status: implemented (Brush Studio)
- Type: number
- Range: 25 … 400 %
- Default: 100 %
- Engines: 
- Sensors: —
- Has an effect with: Streaks above 0
- Serialisation: `dryStreakScale`: JSON number
- Rendering: Divides the streak field coordinates.
- Cost: None.
- Tests: tst_brushmedia::dry*

### Edge breakup — `dryEdgeBreakup`

Makes the rim of the mark crumble, as charcoal and chalk do, instead of fading evenly.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: 
- Sensors: —
- Serialisation: `dryEdgeBreakup`: JSON number
- Rendering: Lowers the penetration towards the dab's rim by a document-space noise term.
- Cost: Per pixel of each dab: one extra paper sample near the rim.
- Tests: tst_brushmedia::dry*

### Grit — `dryGrit`

Pigment that skips: a share of pixels the stroke leaves bare, different for each stroke (seeded).

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 90 %
- Default: 0 %
- Engines: 
- Sensors: —
- Serialisation: `dryGrit`: JSON number
- Rendering: Pixels whose per-stroke hash is below the grit keep only a fifth of the deposit.
- Cost: Per pixel of each dab: one hash.
- Tests: tst_brushmedia::dry*

### Tilt → penetration — `dryTiltDepth`

Penetration at full tilt, as a share of the upright value: shading with the side of the lead touches only the peaks.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 100 %
- Engines: 
- Sensors: tilt
- Has an effect with: A pen that reports tilt
- Serialisation: `dryTiltDepth`: JSON number
- Rendering: Penetration × (1 + (value − 1) × tilt amount).
- Cost: None.
- Tests: tst_brushmedia::dry*

## Mixing

### Smudge length — `smudgeLength`

How long a picked-up colour persists: short lengths follow the canvas closely, long ones drag a colour far into the next one.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 50 %
- Engines: smudge
- Sensors: —
- Has an effect with: Smudge mode Mix
- Serialisation: `smudgeLength`: JSON number
- Rendering: Mix: the carried colour takes on the area under each dab by (1 − length), per pass.
- Cost: None.
- Tests: tst_brushmedia::mix*

### Depletion — `smudgeDepletion`

The carried paint runs out as the stroke goes on: this share is lost per 100 px of travel.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 99 %
- Default: 0 %
- Engines: smudge
- Sensors: —
- Has an effect with: Smudge mode Mix
- Serialisation: `smudgeDepletion`: JSON number
- Rendering: Mix: lay-down strength × (1 − depletion)^(distance / 100 px).
- Cost: Per dab: one power.
- Tests: tst_brushmedia::mix*

### Colour rate dynamics — `dynamics.colorRate`

Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through response curves; several sensors combine by the chosen rule. Off by default.

- Status: implemented (Brush Studio)
- Type: dynamics
- Range: minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1
- Default: {"combine":"multiply","enabled":false,"minimum":0,"sensors":[]}
- Engines: smudge
- Sensors: all sensors
- Has an effect with: Colour rate above 0
- Serialisation: `dynamics` → `colorRate`: {enabled, minimum, combine, sensors: [{sensor, curve, range}]}
- Rendering: Colour rate × value: for example pressure adds paint while a light touch only blends.
- Cost: Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).
- Tests: tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering

## Bristles

### Bristles — `bristleCount`

How many bristles the brush has. Few bristles leave separate streaks; many fill the stroke.

- Status: implemented (Brush Studio and quick panel)
- Type: integer
- Range: 1 … 512
- Default: 48
- Engines: 
- Sensors: —
- Serialisation: `bristleCount`: JSON integer
- Rendering: Bristles at seeded places in the footprint (the dab's size, roundness and angle).
- Cost: Per dab: one anti-aliased segment per bristle (cost grows with bristles × segment area).
- Tests: tst_brushmedia::bristle*

### Bristle thickness — `bristleThickness`

Width of each bristle as a share of the brush size (at least 0.8 px).

- Status: implemented (Brush Studio)
- Type: number
- Range: 0.5 … 50 %
- Default: 5 %
- Engines: 
- Sensors: —
- Serialisation: `bristleThickness`: JSON number
- Rendering: Capsule width of each bristle's segment.
- Cost: Grows with the width.
- Tests: tst_brushmedia::bristle*

### Spread with pressure — `bristleSpread`

How much pressure splays the bristles: light pressure gathers them into a narrow tip, full pressure spreads them over the whole footprint.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 40 %
- Engines: 
- Sensors: —
- Has an effect with: A pen that reports pressure
- Serialisation: `bristleSpread`: JSON number
- Rendering: Footprint × (1 − spread × (1 − pressure)).
- Cost: None.
- Tests: tst_brushmedia::bristle*

### Bristle lengths — `bristleLengthJitter`

How different the bristles' lengths are: with light pressure only the longest touch the canvas.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 50 %
- Engines: 
- Sensors: —
- Has an effect with: A pen that reports pressure
- Serialisation: `bristleLengthJitter`: JSON number
- Rendering: Each bristle touches once pressure passes its threshold (0 .. this value), rising over a 15 % range.
- Cost: None.
- Tests: tst_brushmedia::bristle*

### Paint load — `bristleLoad`

How much paint the bristles carry at the start of the stroke: low values give a dry brush that breaks up at once.

- Status: implemented (Brush Studio and quick panel)
- Type: number
- Range: 0 … 100 %
- Default: 100 %
- Engines: 
- Sensors: —
- Serialisation: `bristleLoad`: JSON number
- Rendering: Each bristle's reservoir starts at load × (0.75 .. 1).
- Cost: None.
- Tests: tst_brushmedia::bristle*

### Paint depletion — `bristleDepletion`

How fast bristles run out of paint as they paint (each at its own rate), so a long stroke dries out into separate streaks.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 20 %
- Engines: 
- Sensors: —
- Serialisation: `bristleDepletion`: JSON number
- Rendering: Reservoir × (1 − depletion × (0.5 .. 1.5))^(distance painted / 100 px); a drying bristle skips the paper's valleys first, then fades.
- Cost: Per bristle and dab: one power.
- Tests: tst_brushmedia::bristle*

### Colour pickup — `bristlePickup`

How fast each bristle takes on the colour of the paint it crosses (as it was before the stroke), so the stroke drags multicoloured streaks through existing paint.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 0 %
- Engines: 
- Sensors: —
- Serialisation: `bristlePickup`: JSON number
- Rendering: Per 10 px painted, the bristle colour moves this share towards the colour under it (× its alpha).
- Cost: Per bristle and dab: one pixel read and one mix.
- Tests: tst_brushmedia::bristle*

### Bristle wobble — `bristleWobble`

Sideways jitter of each bristle from dab to dab, as a share of the space between bristles.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 10 %
- Engines: 
- Sensors: —
- Serialisation: `bristleWobble`: JSON number
- Rendering: Adds a seeded offset across the footprint per bristle and dab.
- Cost: None.
- Tests: tst_brushmedia::bristle*

## Watercolour

### Water — `wcWater`

How wet the brush is: the paper under it is wetted up to this level. Wetter paint spreads further and stays wet longer; a wetter brush in a damp wash adds water to it.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 60 %
- Engines: 
- Sensors: —
- Serialisation: `wcWater`: JSON number
- Rendering: Water → water + (value × min(1, 3 × tip) − water) × min(1, 4 × pass), where lower; pass = flow normalised to the spacing.
- Cost: Per pixel of each dab: one update of the wet cell.
- Tests: tst_brushmedia::watercolour*

### Pigment — `wcPigment`

How much colour the brush carries (with opacity and its pressure response): low values give pale washes that can be glazed once dry.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 50 %
- Engines: 
- Sensors: —
- Serialisation: `wcPigment`: JSON number
- Rendering: The wash under the brush moves towards value × opacity of the brush colour by min(1, 4 × pass) × contact per dab; glaze opacity = 1 − e^(−2.5 × amount).
- Cost: None beyond the water update.
- Tests: tst_brushmedia::watercolour*

### Spread — `wcSpread`

How readily water and pigment move through the wet area: high values bloom into a wet wash, low values stay where they were put.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 50 %
- Engines: 
- Sensors: —
- Serialisation: `wcSpread`: JSON number
- Rendering: Water diffusion between wet cells 0.08 + 0.6 × spread per step; pigment 0.02 + 0.2 × spread.
- Cost: Per wet cell and step: four neighbour exchanges.
- Tests: tst_brushmedia::watercolour*

### Edge darkening — `wcEdgeDarkening`

Water flows to the edge of a wash and evaporates there, leaving its pigment: the darker rim of real watercolour.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 50 %
- Engines: 
- Sensors: —
- Serialisation: `wcEdgeDarkening`: JSON number
- Rendering: Pressure = water × (1 − 0.8 × value × edge) and evaporation × (1 + 4 × value × edge), edge = share of dry paper within two cells; the outward flow carries pigment.
- Cost: None beyond the step.
- Tests: tst_brushmedia::watercolour*

### Granulation — `wcGranulation`

Pigment settles in the valleys of the paper, so dried washes show the paper's grain.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 30 %
- Engines: 
- Sensors: —
- Serialisation: `wcGranulation`: JSON number
- Rendering: Settling rate × (1 + 3 × value × (1 − paper height)).
- Cost: Per wet cell and step: one paper sample.
- Tests: tst_brushmedia::watercolour*

### Drying time — `wcDryingTime`

How long a wash stays wet (simulation time; on the canvas it dries in real time after the stroke). Painting into a wash before it dries mixes wet-on-wet.

- Status: implemented (Brush Studio)
- Type: number
- Range: 0.5 … 60 s
- Default: 6 s
- Engines: 
- Sensors: —
- Serialisation: `wcDryingTime`: JSON number
- Rendering: Evaporation per 16 ms step = 0.6 / (time / 16 ms), more at the wet edge.
- Cost: The canvas runs a bounded number of steps per timer tick until the paint is dry.
- Tests: tst_brushmedia::watercolour*

### Lifting — `wcLifting`

Clear water over paint that has settled lifts it: the damp brush takes part of the colour up and the rest moves in the wash (while the canvas keeps the paint's wet state).

- Status: implemented (Brush Studio)
- Type: number
- Range: 0 … 100 %
- Default: 20 %
- Engines: 
- Sensors: —
- Serialisation: `wcLifting`: JSON number
- Rendering: Per dab: settled pigment × lifting × contact × pass is loosened into the wash, where the brush's paint exchange takes part of it up.
- Cost: Per pixel of each dab: one mix.
- Tests: tst_brushmedia::watercolour*

## Engine

### Engine — `blend`

What the brush does to the layer: paint colour, erase, smudge existing paint, or wet paint (carries and mixes paint).

- Status: implemented (Brush Studio)
- Type: option
- Range: Paint / Erase / Smudge / Wet paint / Watercolour
- Default: Paint
- Engines: all
- Sensors: —
- Serialisation: `blend`: one of "paint", "erase", "smudge", "wet", "watercolor"
- Rendering: Selects the renderer: coverage composite (paint, erase) or carried patch (smudge, wet paint).
- Cost: Smudge and wet paint touch pixels per dab; paint and erase recomposite each pixel once per input event.
- Tests: tst_brush, tst_brushengine (engine suites), tst_brushengine::everyImplementedPropertyChangesRendering

### Dab type — `stamp`

What one dab of a paint or erase brush is: the tip, a spray of particles, hatching lines, or dry media (graphite, charcoal, chalk, pastel) that catches in the paper's tooth.

- Status: implemented (Brush Studio)
- Type: option
- Range: Tip / Spray / Hatching / Dry media / Bristles
- Default: Tip
- Engines: paint, erase, spray, hatching
- Sensors: —
- Has an effect with: Paint or erase engine
- Serialisation: `stamp`: one of "tip", "spray", "hatching", "dry", "bristle"
- Rendering: Spray scatters round particles over the dab's disc; hatching draws parallel document-space lines inside the tip; dry media deposits the tip only where the pressure-dependent penetration reaches into the paper (the texture, in document space).
- Cost: Spray: per particle O(particle area). Hatching: as the tip plus one distance per pixel. Dry media: as the tip plus one or two texture samples per pixel.
- Tests: tst_brushengine::sprayAndHatching, tst_brushmedia (dry media)


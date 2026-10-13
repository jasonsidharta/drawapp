# Krita paint engines (milestone 11 research)

Read for algorithms and design only. Krita is GPL-2.0-or-later; no Krita code, presets, tips or
textures are copied into DrawApp (`research/m11-licenses.md`). Krita was not built or run on this
machine, so nothing here is a runtime observation: every statement is either **observed in source**
(file and line given) or marked **inference**.

## Revision

| Revision | How it was read |
|---|---|
| `25840ee8949d9c845f077bbc8188743d1364a9d6` (2026-10-11, `https://github.com/kde/krita.git` mirror of `invent.kde.org/graphics/krita`) | full local checkout, `C:\Users\Asus\OneDrive\Documents\cp lagi\krita`, read-only |

Milestone 9 (`research/milestone9-krita-brush-engine.md`) already covered the pixel engine, sensors
and curves, spacing, colour dynamics, texture options, colour smudge (briefly), `.kpp` presets and
dab caching. This document adds what milestone 11 needs: bristles, the newer colour smudge
strategies and paint thickness, height texturing, smoothing and stabilisation, hatching, spray and
sketch. Documentation consulted for user-facing names only:
https://docs.krita.org/en/reference_manual/brushes/brush_engines.html (2026-10-12).

## 1. Bristle engine ("hairy", the Sumi-e engine)

**Where:** `plugins/paintops/hairy/hairy_brush.cpp` (436 lines), `kis_hairy_paintop.cpp`,
`KisHairyBristleOptionData.h`, `KisHairyInkOptionData.h`.

**Observed:**
- *Bristles from the tip* (`HairyBrush::fromDabWithDensity`, line 58). The brush tip is rendered
  once at stroke start; every non-transparent tip pixel becomes a bristle with probability
  `density`. The bristle's offset is the pixel position and its "length" is the tip value there.
  Bristles therefore carry the tip's shape (round, flat, any image).
- *One path per bristle* (`paintLine`, line 96). Per input segment each bristle is transformed by
  rotate(−angle) · scale · translate(random ±randomFactor) · shear(pressure × shearFactor). With
  `connectedPath` the bristle continues from its own previous end point, so each bristle draws a
  continuous line rather than dabs. The line is sampled at 1 px steps
  (`m_trajectory.getLinearTrajectory(..., 1.0)`).
- *Contact by pressure* (`threshold = 1.0 - pi2.pressure()`, and `if (threshold && length <
  threshold) continue`). With threshold enabled, only bristles longer than (1 − pressure) touch,
  so light pressure paints with the longest few bristles.
- *Ink depletion* (`fetchInkDepletion`, line 239). A per-bristle counter indexes a curve tabulated
  to `inkAmount` entries (default 1024): the bristle fades along its own path, independently of the
  others. Depletion can lower opacity (`opacityDepletion`, line 277) or saturation
  (`saturationDepletion`, line 249), either as a product
  `pressure × length × inkAmount × (1 − depletion)` or as a weighted sum (`useWeights`).
- *Soak ink* (`colorifyBristles`, line 418). At the first contact each bristle takes the colour of
  the layer under it, which gives multicolour strokes that pull existing paint.
- *Deposit* (`paintParticle`, lines 318 and 357). A bristle point is a bilinear 2 × 2 splat, either
  composited (OVER) or "darken" (keep the higher opacity).
- *Mouse pressure* (`computeMousePressure`, line 401). Without a pen, a pressure is invented from
  movement speed (`(4 × old + 0.02 + (1 − distance / 20)) / 5`).
- *Spacing* (`kis_hairy_paintop.cpp:112`). Spacing is fixed at 0.5; the engine paints lines between
  input points, not dabs.

**For DrawApp:**
- **Adopt the concepts:** bristles sampled from the tip footprint; a continuous path per bristle
  (no gaps at speed); pressure-dependent contact via bristle length; per-bristle reservoir that
  depletes along its own path; pickup of canvas colour per bristle.
- **Differ deliberately:**
  - DrawApp never invents pressure from mouse speed (AGENTS.md: no invented data);
  - bristles are anti-aliased capsules with a width, not 1 px splats, so the stroke keeps its
    density at any zoom;
  - contact is soft (bristles fade in over a pressure range instead of switching on at a
    threshold), which avoids flicker as pressure crosses the threshold;
  - pickup samples the layer as it was before the stroke (the undo snapshot), so a stroke never
    picks up its own fresh paint and the result does not depend on how input is split.

## 2. Colour smudge: smearing, dulling, colour rate

**Where:** `plugins/paintops/colorsmudge/kis_colorsmudgeop.cpp` (`paintAt`, line 135),
`KisColorSmudgeStrategyBase.cpp`.

**Observed:**
- *The canvas is the carrier.* Smearing reads the area under the previous dab
  (`srcDabRect = dstDabRect.translated(lastPos − newCenter)`, `m_lastPaintPos = newCenterPos` at
  line 194) and composites it over the current position with `smudgeRate × opacity`
  (`smearRateOpacity`, line 167). There is no separate carried buffer in the new engine.
- *Dulling* samples one weighted average colour under the dab (`sampleDullingColor`) and lays it
  with `0.8 × smudgeRate × opacity` (`dullingRateOpacity`, line 162).
- *Colour rate* adds the paint colour with `colorRate² × opacity` (`colorRateOpacity`, line 154).
  The square makes low settings gentle.
- *Smear alpha* chooses COPY (alpha moves too) or OVER (`smearCompositeOp`).
- The first dab only records the position (`m_firstRun`); painting starts with the second.

**For DrawApp:** DrawApp's milestone 6 smudge carries a patch, which keeps colour along the
stroke even when the canvas under the previous dab has since been changed by the same stroke.
Milestone 11 keeps the patch and adds what Krita and MyPaint show is missing (section 6 of
`research/m11-paint-simulation.md`): separate pickup radius, smudge length (how long carried colour
persists), alpha-aware mixing and a colour-rate curve. The squared colour rate is adopted as a
**response shape**, not as code.

## 3. Paint thickness (impasto) and between-stroke state

**Where:** `KisColorSmudgeStrategyLightness.cpp` (`updateMask` line 90, `paintDab` line 137),
`KisColorSmudgeInterstrokeData.cpp`, `libs/image/KisInterstrokeData.h/.cpp`,
`libs/image/kis_paint_device_data.h:69`.

**Observed:**
- Lightness tips (`brushApplication == LIGHTNESSMAP`) are grey images centred on 127. Paint
  thickness scales the tip's deviation from 127, and the result is painted into a separate
  **heightmap** device. The visible layer is the colour device with its lightness modulated by
  the heightmap (`modulateLightnessByGrayBrush`). Thickness mode OVERLAY blends the new height with
  the existing one; OVERWRITE replaces it.
- The colour device and heightmap live in **interstroke data** attached to the layer. They persist
  between strokes, are wrapped in each stroke's undo transaction (`beginTransaction` /
  `endTransaction`), and are **not** cloned with the device ("WARNING: interstroke data is
  **not** copied while cloning, that is expected behavior!", `kis_paint_device_data.h:69`). They
  are dropped when the layer becomes incompatible (`isStillCompatible`: offset or colour space
  changed, `KisInterstrokeData.cpp:24`).
- No file-format code references interstroke data (only
  `libs/ui/tool/strokes/kis_painter_based_stroke_strategy.cpp` does). **Inference:** Krita does
  not save this state: after saving and reopening only the flattened appearance remains.

**For DrawApp:** this is the closest precedent for persistent wet paint. Two lessons:
1. state between strokes must be part of each stroke's undo record, or undo desynchronises it
   from the pixels;
2. Krita accepts losing the state on save. DrawApp's specification forbids doing that silently,
   so the wet-paint design (`research/m11-brush-architecture.md`, decision A7) must either save
   the state or dry it with a visible notice.

## 4. Height texturing (paper tooth)

**Where:** `plugins/paintops/libpaintop/KisTextureOptionData.h:27` (texturing modes MULTIPLY,
SUBTRACT, LIGHTNESS, GRADIENT, DARKEN, OVERLAY, …, HEIGHT, LINEAR_HEIGHT, HEIGHT_PHOTOSHOP,
LINEAR_HEIGHT_PHOTOSHOP); maths in `libs/ui/tool/strokes/KisMaskingBrushCompositeOp.h:560-710`.

**Observed (soft texturing, strength s):**
- HEIGHT: `result = clamp(dab / (1 − s) − texture × s)`. The dab's coverage is first scaled up,
  then the paper height is subtracted. A light dab reaches only the valleys; a heavy dab fills the
  tooth.
- LINEAR_HEIGHT: `result = clamp(max(dab' × (1 − texture·s), dab' − texture·s))` with
  `dab' = dab / (1 − s)`: the larger of a multiply and a height result, smoother at mid pressure.

**Verified use in popular bundles** (`research/m11-top30-brushes.md`): linear height (mode 13) and
height modes appear in the "INK brushes" bundle (#7) and Deevad's bundle (#9). Subtract (mode 1)
is the most common non-multiply mode.

**For DrawApp:** DrawApp's milestone 6 "Subtract" texture mode is a simpler relative (it removes
coverage in valleys relative to opacity). The milestone 11 dry-media engine uses a height
comparison of its own (paper height vs. pressure-dependent penetration depth, with a soft
transition), which gives the "light pencil skips the tooth, heavy pencil fills it" behaviour.

## 5. Smoothing and stabilisation

**Where:** `libs/ui/tool/kis_tool_freehand_helper.cpp`; `libs/ui/tool/kis_smoothing_options.h`
(NO_SMOOTHING, SIMPLE_SMOOTHING, WEIGHTED_SMOOTHING, STABILIZER, PIXEL_PERFECT).

**Observed:**
- *Weighted smoothing* (around line 526). The new point is a Gaussian-weighted average of the
  history, weighted by **arc length** back from the newest point. σ = (smoothness distance) / 3,
  where the distance interpolates between a minimum and a maximum with drawing speed (line 488),
  so fast strokes are smoothed less. A "tail aggressiveness" term lengthens the distance where
  pressure falls (the end of a stroke), letting the line catch up.
- *Stabilizer* (`stabilizerPollAndPaint`, line 862). A timer samples the cursor at a fixed
  interval, keeps a queue of the last N samples and paints their average. A "delay distance"
  dead zone (`delayDistance / zoom`) stops painting until the cursor leaves a circle around the
  last painted point. "Finish stabilized curve" draws the rest of the queue at pen-up.
- *Pixel perfect* removes L-shaped corner pixels from 1 px lines.

**For DrawApp:** DrawApp has a pull-string smoother (a dead zone that drags the line) plus
pressure smoothing and a jitter filter. Milestone 11 adds what the ink engine needs:
- speed-adaptive smoothing (less at speed), as in weighted smoothing;
- corner preservation, which Krita does not have as an option;
- curve interpolation between input events.

The timer-driven stabilizer is not adopted: it repaints on a timer, independent of input events,
which DrawApp's input model (one recomposite per input event) does not need. The pull string
already gives the dead-zone behaviour.

## 6. Hatching and crosshatching

**Where:** `plugins/paintops/hatching/hatching_brush.cpp`, `kis_hatching_paintop.cpp:119-162`,
`KisHatchingOptionsData.h:17` (NoCrosshatching, Perpendicular, MinusThenPlus, PlusThenMinus,
MoirePattern).

**Observed:**
- Lines are fixed in document space by an origin, so successive dabs join (`hatching_brush.cpp:62`
  computes the line period from the separation and angle).
- **Crosshatching by sensor.**
  - *Perpendicular:* a second pass at +90° when the crosshatching sensor exceeds 0.5.
  - *MinusThenPlus / PlusThenMinus:* passes at −45° then +45° (or the reverse) above 0.33 and 0.67.
  - *Moiré:* the angle itself follows the sensor.
- Separation can follow a sensor in discrete intervals (`separationAsFunctionOfParameter`, line 246).

**For DrawApp:** DrawApp's hatching (milestone 9) has one direction. Milestone 11 adds crosshatch
passes chosen by pressure thresholds, the same artist-facing idea with DrawApp's own sensor
mapping.

## 7. Spray and sketch

**Observed:**
- **Spray** (`plugins/paintops/spray/KisSprayRandomDistributions.h`): particle positions follow
  selectable distributions:
  - uniform;
  - uniform in polar distance;
  - normal (Gaussian), also in polar distance;
  - a cluster-based distribution.

  Particle shapes can be ellipses, rectangles, anti-aliased pixels or images
  (`KisSprayShapeOptionData`).
- **Sketch** (`plugins/paintops/sketch/kis_sketch_paintop.cpp:153-230`): every input point is
  remembered. A new point draws thin lines to earlier points closer than a threshold, with a
  probability ("density"), which produces web-like shading.

**For DrawApp:** the spray stamp gains a Gaussian distribution option (centre-weighted spray reads
as airbrush speckle). Sketch connections are recorded as a possible later engine. They are not in
this milestone's required list.

## 8. MyPaint integration

`plugins/paintops/mypaint` wraps libmypaint. MyPaint's own algorithms are studied from libmypaint
itself (`research/m11-paint-simulation.md`), whose ISC licence permits adaptation.

## 9. What Krita does not have

**Observed by absence in the source tree:**
- there is no watercolour or fluid paint engine among `plugins/paintops/*`;
- wet-looking presets use colour smudge, texture and blending modes. The most-liked watercolour
  set (#3 in `research/m11-top30-brushes.md`) relies on the "Geometric Mean" blending mode, per
  its author.

**Inference:** Krita does not simulate water. A DrawApp watercolour simulation would therefore go
beyond Krita, not reproduce it.

# Procreate Brush Studio (milestone 11 research)

Only **publicly documented behaviour** from the Procreate Handbook is used. There is no access to
Procreate's code or assets, Procreate was not run (it is iPad-only), and nothing here is a
runtime observation. No Procreate names, icons, brushes or brush files are used in DrawApp.

## Sources

| Page | Read |
|---|---|
| https://help.procreate.com/procreate/handbook/brushes/brush-studio-settings | 2026-10-12 (descriptions quoted below as documented) |
| https://help.procreate.com/procreate/handbook/brushes/brush-studio | 2026-10-12 (milestone 9; structure and editing loop, `research/milestone9-procreate-brush-studio.md`) |

Milestone 9 recorded the attribute list and the editing loop. This document records the
**behavioural descriptions** milestone 11 needs and what DrawApp does with each.

## Documented behaviour (quoted) and DrawApp's interpretation

### Rendering

| Setting | Documented | DrawApp |
|---|---|---|
| Light Glaze | "Procreate's standard and lightest rendering mode." | build-up accumulation (milestone 6) |
| Uniform Glaze | "similar to the rendering used in Adobe® Photoshop®." | uniform accumulation (milestone 6) |
| Intense Glaze | "renders colors … with a slightly heavier touch" | not a separate mode; build-up with a higher flow is the nearest equivalent |
| Heavy Glaze | "maintains the paint's opacity when mixing" | not adopted |
| Uniform / Intense Blending | "Intense Blending is great for wet brushes that squash and mix colors together." | the mixing engines (smudge mix, wet paint) |
| Wet Edges | "Soften and blur the edges of your brushstrokes to mimic pigment bleeding into paper." | planned property (static); the watercolour engine produces edges by simulation instead |
| Burnt Edges | "a 'color burn' effect around the edges of the stroke when you layer brushstrokes." | not adopted |
| Luminance Blending | "blend the lightness values instead." | not adopted |

**Inference:** "glaze" modes describe how a stroke accumulates against its own opacity, and
"blending" modes describe interaction with existing paint. That is the same split as DrawApp's
coverage engines (paint with Uniform or BuildUp) versus carried-paint engines (smudge, wet).

### Wet Mix

| Setting | Documented | DrawApp |
|---|---|---|
| Dilution | "how much water mixes in with the paint on your brush" | wet paint `wetDilution` (milestone 9); in watercolour, the water carried by each dab |
| Charge | "how much paint is applied onto your brush when you begin to make a stroke" | `wetLoad`; reservoir of the mixing engines |
| Attack | "the amount of paint that sticks to the canvas" | `wetAttack` |
| Pull | "the strength of how your brush pulls paint around the canvas" | `wetPull`, smudge strength |
| Grade | "the chunkiness and contrast of your brush texture" | not adopted |
| Blur / Blur Jitter | "the amount of blur your brush applies to the paint on your canvas" | not adopted as a separate property; watercolour diffusion plays this role |
| Wetness Jitter | "Randomize how much water mixes in with the paint" | `wetnessJitter` (milestone 9) |

**Lesson:** Procreate's Wet Mix is a mixing model (charge, attack, pull), not a fluid simulation,
going by its documentation. The milestone 11 specification asks DrawApp's watercolour for more:
diffusion, drying and wet edges, which needs state in the canvas
(`research/m11-paint-simulation.md`).

### Stabilization

| Setting | Documented | DrawApp |
|---|---|---|
| StreamLine Amount | "smoothing out wobbles or shakes in your line" | the pull-string smoother (`smoothing`) |
| StreamLine Pressure | "turn Pressure up for a longer, smoother application" | pressure smoothing (`pressureSmoothing`) |
| Stabilization Amount | "the more it averages out the wobbles in a stroke" | milestone 11: speed-adaptive averaging (the ink engine) |
| Motion Filtering Amount | "deletes the extremities of a stroke's wobbles entirely" | the jitter filter (`jitterFilter`) is the nearest: it drops moves under a distance |
| Motion Filtering Expression | "puts some expressive feeling back into strokes" | not adopted |

Three documented mechanisms map to three distinct algorithms:
- a dragged string (lag);
- averaging (smoothing);
- outlier removal (filtering).

DrawApp has the first and third, and adds the second in milestone 11.

### Taper

| Setting | Documented | DrawApp |
|---|---|---|
| Pressure Taper | "Extend the taper at the beginning and end of a brush stroke drawn with Apple Pencil." | start and end taper by length (milestone 9) |
| Size / Opacity | "how severe your taper will transition"; "transparency your taper fade to" | `taperStartSize`, `taperEndSize`, `taperOpacity` |
| Pressure | "pressure feedback … for a more responsive natural taper" | the taper multiplies with pressure → size |
| Tip | "on low settings, … a brush with a very fine tip" | `taperShape` (exponent) is the nearest |
| Tip Animation | "see the effect Procreate applies to the taper" | **inference:** the end taper is applied after the fact (the line visibly thins at pen-up). This supports milestone 11's retroactive end taper (`research/m11-brush-architecture.md`, A4), which removes the milestone 9 lag of the live line |
| Classic Taper | "returns the taper behavior … to previous versions" | presets keep their behaviour (DrawApp's compatibility rule) |

### Grain

| Setting | Documented | DrawApp |
|---|---|---|
| Moving / Texturized | "move with the stroke for a more streaky and blurred effect" / "stay as a static texture 'behind' your brush stroke" | `textureMoving` (each dab) / document-space grain (milestone 9) |
| Movement | "Set the slider low for more drag." | not adopted |
| Scale, Rotation | grain size; "smear the grain image based on directional stroke changes" | `textureScale`, `textureRotation` |
| Depth / Depth Minimum / Depth Jitter | strength of the texture; minimum contrast; random | `textureStrength` with dynamics |
| Offset Jitter | "Offset where your texture places down each time you make a new brushstroke." | `textureRandomOffset` |
| Brightness/Contrast | "makes the grain lighter or darker" | `textureBrightness`, `textureContrast` |

**Lesson for dry media:** "Texturized" grain fixed behind the stroke is what makes repeated
pencil strokes build up in the same paper tooth. Milestone 11's dry media keeps the paper in
document space for that reason.

## Apple Pencil behaviour

The milestone 9 table (pressure and tilt graphs for size, opacity, flow, bleed and gradation)
still applies. DrawApp maps these to curves per sensor. No pen hardware was available, so every
pressure and tilt behaviour in milestone 11 is checked with **simulated** input only.

## What is not adopted

- Proprietary brush files (`.brush`, `.brushset`), their names and their assets.
- 3D materials.
- Burnt edges, luminance blending, grade and blur as separate controls.

# Procreate Brush Studio (milestone 9 research)

Only **publicly documented behaviour** from the Procreate Handbook is used here. There is no
access to Procreate's code, and nothing here comes from running Procreate. No Procreate names,
icons, brushes or assets are used in DrawApp.

## Sources (read 2026-10-12)

- https://help.procreate.com/procreate/handbook/brushes/brush-studio
- https://help.procreate.com/procreate/handbook/brushes/brush-studio-settings

## Interface organisation (documented)

The Brush Studio has three areas:
- **Attributes**: a list on the left;
- **Settings**: the selected attribute's controls in the centre;
- **Drawing Pad**: a preview on the right ("Draw shapes and scribbles into it to see differences
  in how your brush responds").

The Drawing Pad has its own settings: clear the pad, reset all brush settings, preview size, and
a preview colour. Changes are kept with **Done** or discarded with **Cancel**. **About this
Brush** offers **Create New Reset Point** and **Reset brush** (back to the last reset point or
factory state).

Lessons for DrawApp:
- **Progressive disclosure:** the Brush Studio is separate from the quick brush panel. Everyday
  painting uses the existing panel (milestone 6); the studio is for building brushes.
- **Preview:** a dedicated drawing pad that uses the brush being edited, not the saved one.
- **Done/Cancel:** edits apply to a copy; Cancel leaves the preset untouched.
- **Reset:** per setting and per preset. Reset points are not adopted: DrawApp resets to the
  saved version or, for built-ins, the factory definition.

## Attribute categories (documented)

The handbook page lists 15 headings (the overview says 14 attributes; Preview and About are
partly brush metadata):
1. Stroke Path
2. Stabilization
3. Taper
4. Shape
5. Grain
6. Rendering
7. Wet Mix
8. Color Dynamics
9. Dynamics
10. Apple Pencil
11. Properties
12. Materials
13. Preview
14. About this Brush

Blending is also documented as a group.

Settings that matter for DrawApp's design:

| Category | Documented settings (exact names) | Meaning for DrawApp |
|---|---|---|
| Stroke Path | Spacing, Spacing Jitter, Jitter Lateral, Jitter Linear, Fall Off | Spacing and its jitter, scatter perpendicular and along the stroke, fade over distance (a Distance/Progress sensor on opacity) |
| Stabilization | StreamLine Amount, StreamLine Pressure, Stabilization Amount, Motion Filtering Amount, Motion Filtering Expression | DrawApp has a pull-string smoother. Adding a moving-average stabiliser is a separate setting |
| Taper | Pressure Taper (size, opacity, pressure, tip), Touch Taper (separate for finger), Classic Taper | Start and end taper by length, with size and opacity at the ends |
| Shape | Shape Source, Scatter, Count, Count Jitter, Randomized, Flip X / Flip Y, Pressure Roundness, Tilt Roundness, Roundness Vertical/Horizontal Jitter, Shape Filtering | Tip image, rotation jitter, stamps per point, flips, roundness dynamics |
| Grain | Grain Source, Moving vs Texturized, Movement, Scale, Zoom, Rotation, Depth, Depth Minimum, Depth Jitter, Offset Jitter, Blend Mode, Brightness/Contrast, Grain Filtering | Texture image; document-fixed vs stroke-following grain; scale, rotation, depth (strength) with pressure, brightness/contrast |
| Rendering | Light/Uniformed/Intense/Heavy Glaze, Uniform/Intense Blending | Accumulation modes. DrawApp has Uniform and Build-up |
| Blending | Flow, Wet Edges, Burnt Edges, Blend Mode, Luminance Blending, Alpha Threshold | Flow; alpha threshold is a cheap, testable effect |
| Wet Mix | Dilution, Charge, Attack, Pull, Grade, Blur, Blur Jitter, Wetness Jitter | Directly guides DrawApp's wet-paint prototype parameters |
| Color Dynamics | Hue/Saturation/Lightness/Darkness/Secondary Color per Stamp, per Stroke, by Pressure, by Tilt, by Barrel roll | Hue/saturation/lightness as dynamic parameters with per-dab random, per-stroke random, pressure and tilt sensors |
| Dynamics | Size/Opacity/Spacing by Speed, Size/Opacity Jitter | Speed sensor mappings and random sensors |
| Apple Pencil | Pressure Graph, Size/Opacity/Flow/Bleed (Pressure), Tilt Graph, Opacity/Gradation/Bleed/Size (Tilt) | A response curve per mapping. DrawApp's curve editor plays the role of these graphs |
| Properties | Use Stamp Preview, Orient to Screen, Preview Size, Smudge Pull, Maximum/Minimum Size and Opacity | Size and opacity limits for the quick controls |
| Materials | Metallic, Roughness | Not applicable (3D painting); not adopted |

## What DrawApp takes from this

- **Group settings by artist intent**, not by engine internals: the 12 groups of
  `docs/brush-settings-specification.md`.
- **Keep sensors in one framework**, so "by pressure", "by tilt", "by speed" and "jitter" are one
  mechanism shown in each group, not separate code paths.
- **Keep preview, Done/Cancel and reset as the core editing loop.**
- **Not adopted:** reset points, 3D materials, Apple Pencil hover and barrel roll (no hardware
  support; DrawApp reads only what Qt reports).

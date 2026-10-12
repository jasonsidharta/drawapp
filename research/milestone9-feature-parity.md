# Milestone 9 feature parity

What DrawApp has compared with Krita (source study, `research/milestone9-krita-brush-engine.md`),
PhotoCraft (source study, `research/milestone9-photocraft-architecture.md`) and Procreate's
documented Brush Studio (`research/milestone9-procreate-brush-studio.md`).

None of these applications was run on this machine:
- Krita is not installed.
- PhotoCraft needs a Rust toolchain, which is not installed.
- Procreate is iPad-only.

The "they have" column therefore comes from source code or documentation, not from use. It is
not a quality comparison: "Implemented" says that DrawApp has the capability and a test proves it
changes behaviour, not that it is as good as the other application's.

## Status categories

| Status | Meaning |
|---|---|
| **Already implemented** | in DrawApp before milestone 9 |
| **Newly implemented** | added in milestone 9, with a test that shows a real effect |
| **Partial** | works, with a stated limitation |
| **Not implemented** | absent; may be listed as planned |
| **Not tested** | the code exists but has not been verified where it matters (for example on a physical pen) |
| **Incompatible / unsupported** | deliberately not supported, or not possible in DrawApp's design |

A UI control is not counted unless it changes brush or canvas behaviour. The registry's "planned"
properties have no controls at all.

## Layers, selections and transforms

| Capability | Krita | PhotoCraft | DrawApp status | Evidence |
|---|---|---|---|---|
| Select several layers | yes | yes | **Newly implemented** (Ctrl/Shift+click, long-press) | `tst_canvas` multi-layer tests, `tst_ui` workflows 1, 2, 10 |
| Transform several layers together, aligned, one undo step | yes (transform tool on selected layers) | yes | **Newly implemented** | `multiLayerTransformIsOneStepAndSurvivesSaving`, workflow 1 alignment check |
| Locked or hidden layers in a multi-layer transform | skipped silently (`isEditable`) | — | **Newly implemented, different on purpose**: refused with a message naming the layer | `multiLayerTransformRefusesLockedOrHiddenLayers` |
| Transaction: prepare everything, then commit; no partial results; out-of-memory handled | yes | clone-then-commit | **Newly implemented** | `multiLayerCancelAndFailedApplyChangeNothing` |
| Pixel-identical cancel | yes | yes | **Already implemented**, extended to several layers | workflow 10 (byte comparison) |
| Copy/cut/paste across several layers | partly (copy merged or per layer) | — | **Newly implemented** (one pasted layer per source layer, one undo step) | `multiLayerCopyCutPaste` |
| Masks, clipping layers, layer groups | yes | groups | **Incompatible / unsupported**: DrawApp has none, so they cannot affect transforms | ARCHITECTURE.md |
| Rectangle, ellipse, freehand lasso, modes, invert, reselect | yes | yes | **Already implemented** (milestone 8) | `tst_selection` |
| Polygon lasso | yes | — | **Newly implemented** (click corners; Enter, double-click or the first point closes; Backspace; Esc) | `polygonLassoByClicks` |
| Magic wand / contiguous colour select | yes | yes | **Newly implemented** (tolerance, contiguous or global, active layer or all layers, anti-aliased) | `magicWandContiguousAndGlobal`, `magicWandAndModifyCommands`, workflow 3 |
| Feather, grow, shrink | yes | — | **Newly implemented** (Euclidean distance, box-blur feather) | `growAndShrinkByDistance`, `featherSoftensWithoutLosingCoverage`, workflow 5 |
| Clear selection contents | yes | yes | **Already implemented**, now on every selected layer | `tst_canvas` |
| Transform the selection outline only | yes | — | **Newly implemented** | `transformSelectionOutlineOnly` |
| Floating selection (a movable pasted selection that is not yet a layer) | yes | — | **Partial**: paste always creates a layer, which can then be transformed. There is no floating state | — |
| Empty selection means "nothing", never "everything" | yes | **no** (collapses to none) | **Already implemented**; kept | `emptySelectionNeverPaintsEverywhere` |
| Every brush through a feathered selection exactly once | yes | — | **Already implemented**; verified for the new engines | `featheredSelectionIsAppliedOnceByBrushes`, `smudgeInsideAFeatheredSelectionDoesNotBleed`, `wetPaintLoadsDepositsAndDrags` |

## Brush engines

| Engine | Krita | DrawApp status | Notes |
|---|---|---|---|
| Pixel brush (round and image tips, hardness, spacing, rotation, texture, pressure) | pixel engine | **Already implemented**; extended | imported bitmap tips, flips, density, minimum size |
| Colour smudge | colour smudge engine | **Already implemented**; extended | dulling mode, colour rate, colour-only |
| Textured brush | texture options in most engines | **Already implemented**; extended | rotation, contrast, brightness, invert, each dab, moving grain, random offset, pressure → strength |
| Wet paint | none built in (smudge engines, MyPaint) | **Newly implemented as a bounded prototype** | load, load length, attack, pull, dilution, wetness jitter. Not a fluid or pigment simulation |
| Spray | spray engine | **Newly implemented (subset)** | particle count, size and size jitter. No particle shapes, images or physics |
| Hatching | hatching engine | **Newly implemented (subset)** | angle, spacing, thickness. One direction only; no crosshatching passes |
| Bristle | bristle engine (simulated hairs) | **Not implemented** | the generated bristle *tip* only |
| Sketch (connect nearby strokes) | sketch engine | **Not implemented** | |
| Clone | clone engine | **Not implemented** | |
| Particle, deform, curve, filter, quick, shape engines | yes | **Not implemented** | |

## Dynamics and sensors

| Capability | Krita | Procreate (docs) | DrawApp status |
|---|---|---|---|
| Pressure, tilt X/Y, tilt amount and direction, stroke direction, speed, distance, fade, time, random | yes (dynamic sensors) | pressure, tilt, speed (partly) | **Newly implemented**, plus acceleration and random per stroke |
| Barrel rotation, tangential pressure | yes | — | **Not implemented** (planned property; no test hardware) |
| Curves per sensor | yes (cubic spline) | yes | **Newly implemented** (monotone cubic, 16 points, presets) |
| Several sensors per parameter, combination rules | yes (multiply, add, max, min, difference) | — | **Newly implemented** (the same five rules) |
| Controlled parameters | many per engine | many | **Newly implemented**: size, opacity, flow, spacing, rotation, roundness, scatter, texture strength, hue, saturation, lightness, smudge strength (12) |
| Missing hardware data never invented | — | — | **Newly implemented** (`hasPressure`, `hasTilt`; a sensor without data is left out) |
| Physical pen pressure and tilt | yes | Apple Pencil | **Not tested**: no pen hardware. Only simulated pressure and tilt were used |

## Brush Studio and presets

| Capability | Procreate (docs) | Krita | DrawApp status |
|---|---|---|---|
| Categories, live preview, drawing pad, Done/Cancel | yes | brush editor (no Done/Cancel copy) | **Newly implemented** |
| Search settings | — | — | **Newly implemented** |
| Numeric entry with units | partly | yes | **Newly implemented** |
| Curve editor | yes | yes | **Newly implemented** (touch: drag, tap to add, long-press to remove) |
| Reset one setting / whole preset | reset points | reload preset | **Newly implemented** (to the preset as saved) |
| Duplicate, rename | yes | yes | **Newly implemented** (built-ins keep their names) |
| Import/export own format | `.brush`, `.brushset` | `.kpp`, bundles | **Newly implemented**: `.drawbrush`, versioned |
| Import Krita `.kpp` | — | — | **Incompatible / unsupported** (investigated; not attempted) |
| Import Procreate `.brush` | — | — | **Incompatible / unsupported** (format undocumented) |
| Unknown future keys preserved | — | partly | **Newly implemented** |
| Version 1 presets migrate | — | — | **Newly implemented** (`libraryVersion1Migrates`) |

## The 120-property design target

The registry (`docs/brush-settings-specification.md`) lists all 120 properties:

- **94 implemented.** Each changes rendering in
  `tst_brushengine::everyImplementedPropertyChangesRendering`.
- **26 planned.** They have no control and no effect.

The two engine selectors (blend, stamp) are not counted.

| Group | Implemented / target |
|---|---|
| Brush Tip | 10 / 12 |
| Stroke Path | 8 / 10 |
| Pressure and Tilt | 10 / 12 |
| Opacity and Flow | 5 / 8 |
| Texture and Grain | 12 / 12 |
| Shape Dynamics | 11 / 12 |
| Color Dynamics | 9 / 10 |
| Blending and Smudge | 7 / 12 |
| Wet Paint | 6 / 10 |
| Stabilization | 4 / 8 |
| Taper | 6 / 8 |
| Special Effects | 6 / 6 |

Some implemented properties have an effect only under conditions the specification states:
- flips need an asymmetric tip;
- tilt settings need a pen that reports tilt;
- flow needs build-up.

They are counted because the effect is real and tested under those conditions. Their effect for
real pens remains **not tested** on hardware.

## Not comparable here

The following were not compared, and no claim is made about any of them:
- performance against Krita or PhotoCraft (the manual procedure is in
  `docs/milestone9-performance.md`);
- brush feel with a physical pen;
- Procreate's rendering.

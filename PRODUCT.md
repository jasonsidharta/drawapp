# Product

Working title: **DrawApp** (placeholder name; app id `org.drawapp.prototype`).

## Purpose

A lightweight, artist-focused drawing application with a simpler interface than Krita, Clip
Studio Paint or MediBang: responsive pen input, efficient memory use and reliable saving. It is
an independent application, not a Krita fork. Those products are references for behaviour only.

AI-assisted development is how we build it. Generative AI *inside* the product is a separate,
future feature with no work scheduled.

## Platforms

| Platform | Distribution | Status (details in BUILD.md) |
|---|---|---|
| Windows 10/11 x64 | Direct download first; store later | Prototype built, launched and tested |
| Android 9+ (API 28+) | Google Play | Prototype APKs built (debug APK signed with a development key), not launched |
| iOS / iPadOS | App Store | Blocked (no macOS/Xcode available) |
| Linux x86_64 | Direct download / Flatpak (to decide) | Blocked (build dependencies not installed) |

## First-release goals (planned, not done)

- Common blend modes, groups and clipping for layers (Normal-blend raster layers are done).
- Brush presets and more brush shapes (opacity, a pressure-to-size curve and smoothing are done).
- Selection and transform basics.
- JPEG export (project saving with crash recovery and PNG export are done).
- Canvas rotation and mirroring.
- Touch gestures on tablets (pinch zoom/pan, two-finger tap undo) with palm rejection.
- Interface that works on phone, tablet and desktop sizes.

## Done so far

- Milestone 0: one 2048 × 1536 raster canvas; round brush with size and color; eraser; mouse, pen
  (pressure) and touch input; zoom and pan; clear.
- Milestone 1: undo/redo (stroke, eraser stroke, clear) under a 128 MiB budget; New/Open/Save/Save As
  with a versioned .drawapp format, atomic saving and unsaved-changes prompts; PNG export.
- Milestone 2: crash recovery (10 s autosave to a separate recovery file, restore/discard on start).
- Milestone 3: raster layers (Normal blending, opacity, visibility, lock) with a layer panel, undo for
  every layer operation, a layered project format (older files still open) and layered recovery.
- Milestone 4: saving, opening and exporting in the background; Android tested on an emulator.
- Milestone 5: safer saving to Android storage providers (overwrite warning, Save a Copy, a local
  copy kept until the file is written and checked); brush opacity that stays even within a
  stroke, a pressure-to-size response with a live preview, and optional smoothing.
- Milestone 6: an artist brush system with eight original presets (pencil, ink, solid round,
  airbrush, textured paint, smudge and two erasers). Includes a preset panel with rendered
  thumbnails, favourites and a scratchpad, custom presets, and pressure to size, opacity and flow
  (tilt where the pen reports it).
- Milestone 8: selections (rectangle, ellipse, lasso; add, subtract, intersect; soft edges) that
  every brush respects, and move, scale, rotate and flip of the selection or the layer, with
  handles, numeric entry, Apply and Cancel; copy, cut and paste to a new layer.
- Milestone 10: large canvases. New canvas up to 16384 × 16384 and 64 layers at any size; memory
  is used only where something is painted, within a limit (half of the device's memory on
  desktop, a quarter on Android) shown in a Memory usage dialog. Zooming out stays fast on large
  canvases; large transforms are applied in the background with progress and Stop; the undo
  history grows with the memory limit and asks before an action that could not be undone.
  Projects are saved in a new sparse format (older projects still open).

See ROADMAP.md for acceptance status.

Milestone 10.5 is in progress: transactional transform Apply, memory ownership,
history preservation and display-resolution previews. Validation is tracked in
`docs/m105-engineering.md`; these additions are not yet release-validated.

## Deferred (not planned for the first release unless decided otherwise)

- Animation; liquify; masks and clipping groups (clipping may move into the first release).
- Editable vector strokes. The document model may later hold both raster layers and editable
  strokes; nothing is built for this yet.
- Krita (.kra) and PSD import/export.
- Generative AI features.
- Cloud accounts, collaboration, subscriptions, asset marketplace.

## Open product decisions

- Final name and app id.
- Qt licensing route for iOS (LGPLv3 with static linking vs. commercial license); see ARCHITECTURE.md.
- Default canvas sizes per platform. The maximum is 16384 × 16384 everywhere since milestone 10,
  with memory bounded by the painted area; whether phones need a smaller default still needs
  measurements on devices (only the emulator was used).

Milestone 10.5 correctness checkpoint: all 10 Windows suites passed in the Codex
build directory (65.91 s). Transaction preparation and dense-buffer ownership
fixes are implemented; preview performance and Android validation remain pending.
See `docs/m105-engineering.md`.

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
- Pressure-sensitive brushes with a pressure curve, smoothing and a small set of brush presets.
- Selection and transform basics.
- JPEG export (atomic project saving, crash recovery and PNG export are done).
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

See ROADMAP.md for acceptance status.

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
- Default canvas sizes and maximum canvas size per platform (needs memory measurements on devices).

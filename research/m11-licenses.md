# Milestone 11 licence audit

Every external source considered for milestone 11, what was taken from it, and what notice that
requires. DrawApp's implementation in milestone 11 is **independently authored**: no external
code, data tables, brush presets, tips, textures or patterns were copied into the repository.
This file is updated if that changes.

| Project | Repository / URL | Revision or date | Files read | Licence | Code copied or adapted? | Assets used? | Notice required in DrawApp |
|---|---|---|---|---|---|---|---|
| Krita | `https://github.com/kde/krita.git` (mirror of `invent.kde.org/graphics/krita`) | `25840ee8949d9c845f077bbc8188743d1364a9d6` | `plugins/paintops/hairy/*`, `plugins/paintops/colorsmudge/*`, `plugins/paintops/hatching/*`, `plugins/paintops/spray/KisSprayRandomDistributions.h`, `plugins/paintops/sketch/kis_sketch_paintop.cpp`, `plugins/paintops/libpaintop/KisTextureOptionData.h`, `libs/ui/tool/strokes/KisMaskingBrushCompositeOp.h`, `libs/ui/tool/kis_tool_freehand_helper.cpp`, `libs/image/KisInterstrokeData*` | GPL-2.0-or-later (SPDX headers) | **No.** Concepts only, re-expressed independently | No | None (nothing copied) |
| Krita documentation | `https://docs.krita.org/en/reference_manual/brushes/brush_engines.html` | read 2026-10-12 | engine list | not checked (no content reused) | No | No | None |
| libmypaint | `https://github.com/mypaint/libmypaint` | `d5a88fbe6649d5ec776bc42ec8c1f4bb29d7fd7f` | `mypaint-brush.c`, `helpers.c`, `brushmodes.c`, `brushsettings.json`, `COPYING` | ISC | **No.** The smudge-length and erase-towards-transparency concepts are re-expressed; the spectral tables are **not** used (DrawApp uses per-channel Kubelka–Munk) | No | None now. **If** the 10-band spectral tables are ever adopted, the ISC copyright and permission notice must ship with DrawApp |
| PhotoCraft | `https://github.com/storytold/photocraft` | `20cb95bc272feb9691537bccb46e84e583f5a04a` | `crates/paint/src/{mixer,render,procedural,brush}.rs`, `LICENSE-*`, `NOTICE` | MIT OR Apache-2.0 (brand assets excluded) | **No.** Concepts only | No | None (nothing copied). If code were adapted: the MIT notice |
| Procreate Handbook | `https://help.procreate.com/procreate/handbook/brushes/brush-studio-settings` | read 2026-10-12 | Brush Studio settings | proprietary documentation | No (behavioural reference only, short quotations for research) | No Procreate brushes, names or assets | None |
| Krita Artists resources (top 30) | `https://krita-artists.org/c/resources/brushes-and-bundles/32/l/top` | accessed 2026-10-12 23:59 UTC | topic first posts; 11 bundle files downloaded to a temporary folder to read preset settings | per resource (see `research/m11-top30-brushes.md`): CC-0, CC-BY-SA, "free to use", paid, or unstated | No | **No** (inspected only; nothing bundled, imported or redistributed) | None |
| Mixbox | `https://scrtwpns.com/mixbox/` | searched 2026-10-12 | — | CC BY-NC | **Rejected** (non-commercial) | No | — |
| spectral.js | `https://github.com/rvanwijnen/spectral.js` | searched 2026-10-12 | — | MIT | No (not used) | No | — |
| Curtis et al. 1997; Van Laerhoven & Van Reeth 2005; Chu & Tai 2005 | publication pages (see `research/m11-paint-simulation.md`) | read 2026-10-12 | abstracts and project pages | publications | No code exists in DrawApp from them; the watercolour model is DrawApp's own simplification | No | None (cited in documentation) |
| Kubelka–Munk theory, Catmull–Rom splines | textbook mathematics | — | — | not copyrightable as methods | Independently implemented from the formulas | — | None |

## Assets

- DrawApp's tips, textures and paper height fields are **generated procedurally** from seeded
  hashes in DrawApp's own code (`BrushTips`, and the milestone 11 paper and grain generators).
  No image files from any third party are included.
- New built-in presets are authored for DrawApp, with names that do not imitate any product's
  presets.

## Process rule for the rest of the milestone

Before any external code or data enters the repository:
1. record it in this table (file, revision, licence);
2. confirm the licence allows it (GPL code: **not** without explicit approval of the licensing
   consequences);
3. add the required notice to the distribution.

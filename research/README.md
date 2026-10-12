# Research and reference checkouts

Other projects we read for reference. Every checkout lives **outside this repository** in
`C:\Users\Asus\OneDrive\Documents\cp lagi\` and is read-only for us: nothing is built from it,
and nothing is written to it. Notes in this folder keep revision, paths, verified facts and
inferences separate (see AGENTS.md).

## Rules for using a reference

- DrawApp is an independent application in C++/Qt. Do not copy or translate code from any
  reference into this repository, whatever its license. Describe behaviour in our own terms and
  implement it ourselves.
- Record which revision a note was written against, and say where something was read in source
  versus inferred.
- Permissive licenses (MIT / Apache-2.0) would allow reuse with attribution. If that is ever
  wanted, it is the owner's decision. It should be a separate, recorded step with the license and
  NOTICE kept, never done silently.

## Reference checkouts

| Project | What it is | Language / UI | License | Revision (date) | Notes |
|---|---|---|---|---|---|
| Krita | Painting app (KDE) | C++ / Qt Widgets | GPL-2.0-or-later / GPL-3 (some files LGPL) | `25840ee8949d` (2026-10-11) | `krita-input-and-canvas.md`, `krita-android-saving-stylus-brush.md`. The working tree is missing `LICENSES/` (21 files); not caused by us |
| PhotoCraft | Clean-room image editor in the style of Photoshop, by the ArtCraft team ("early alpha") | Rust / egui, wgpu | MIT or Apache-2.0; ArtCraft name and logos excluded (`docs/brand/LICENSE-brand.txt`) | `20cb95bc272f` (2026-10-12) | Cloned 2026-10-12 from https://github.com/storytold/photocraft. The brush engine was studied for `brush-system.md`; everything else is unstudied (map below) |
| EffectCraft | Motion graphics and VFX app by the same team | Rust | MIT or Apache-2.0 | `67434e90388b` (2026-10-11) | Cloned in milestone 0; not studied |
| libmypaint | MyPaint's brush engine library | C | ISC | `d5a88fbe6649` (2026-04-14, shallow clone) | Smudge and dab mask only: `brush-system.md` |

Krita's README sets a moratorium on AI-assisted development *of Krita*. That concerns
contributions to Krita, not reading it.

## PhotoCraft: where to look (not studied yet)

PhotoCraft is a large Rust workspace: 25 library crates under `crates/` (its README says 24) and
3 apps under `apps/`, about 1,180 `.rs` files. These parts touch
DrawApp's open questions. Paths are relative to the checkout; nothing below has been read in
detail, so treat it as a map, not as findings.

| Our topic | PhotoCraft location | Why it may help |
|---|---|---|
| Pen input | `crates/tablet` ("Pen tablet pressure, tilt, rotation and eraser end on macOS (AppKit) and Linux X11 (XInput2)") | Platform pen handling. **No Windows or Android code** there; on those platforms it presumably relies on its windowing layer (winit), which is not verified |
| Brush engine | `crates/paint` (`brush.rs`, `dynamics.rs`, `render.rs`, `presets.rs`, `brush_tests.rs`) | The README lists Build-up and Smoothing including "Pulled String" (like our pull string), driven by pressure, tilt and rotation, with deterministic, replayable strokes |
| Tiled raster storage | `crates/raster`, `crates/engine` | Copy-on-write 256 × 256 sparse tiles (README); relevant if we move from full-canvas layers to tiles |
| Two compositors tested against each other | `crates/compose`, `crates/gpu` | A CPU reference compositor checked against the wgpu compositor; a test idea for our future GPU canvas |
| Native file format and saving | `crates/format`, `crates/io` | `.pcraft` format and how they write files; compare with our SafeSave |
| Command registry, agent control | `crates/automation`, `crates/engine`; `docs/control-protocol.md` | Every action is a command; the UI is screenshotted offscreen through a control channel, similar to our offscreen UI tests |
| Project process | `AGENTS.md`, `ROADMAP.md`, `docs/target-app-parity.md` | How they document parity and status honestly |

Not applicable to us: no Android or iOS target (desktop, FreeBSD and web/WebAssembly only), and
no Qt.

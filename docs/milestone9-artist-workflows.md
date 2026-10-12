# Milestone 9 artist workflows

The twelve tasks from milestone 9 section 11, as run in DrawApp.

**What kind of evidence this is.** The tasks are automated interaction:
`tst_ui::artistWorkflowsMilestone9` drives the real `Main.qml` with synthetic mouse and
keyboard events, offscreen, and checks pixels, layers, history and files after each step. Where
a pen is needed (tasks 4 and 8), pressure is **simulated** with `StrokeSample` values. No artist
performed these tasks by hand, and no physical pen was used. Hands-on artist validation is still
open (ROADMAP, milestone 7).

Screenshots are written to `build\windows-mingw-release\tests\tst_ui_m9_*.png` on every run.

## Setup

`drawCharacter()` paints a small character through the canvas with the built-in presets:
- **Colour** layer: face, body, hand and hair;
- **Ink** layer on top: head outline, eyes, mouth, arm, hand and fingers, and some pencil
  shading.

The brush library is pointed at a temporary file, so presets persist within the test.

## Tasks and results

| # | Task | How it was done (real UI unless noted) | Checked | Result |
|---|---|---|---|---|
| 1 | Enlarge the whole head across the ink and colour layers | Ctrl+click the Colour row (both layers selected); Select tool → Lasso around the head; Transform; Alt-drag the bottom-right handle (scale from the centre); Apply | 2 history steps (selection, transform); both layers changed; **alignment**: measured through the pivot, the colour and ink right edges scaled by the same factor (within 3 %), more than 1.1× | Pass |
| 2 | Rotate the arm and hand across both layers | Rectangle around arm and hand; Transform; type 20 in the angle field; Enter applies | both layers changed | Pass |
| 3 | Select the clothing and recolour it | Click the Colour row (one layer); Wand; tap the body; Solid Round 260 px, green, scribble over and beyond the body | wand selection covers the body, not the face; body pixels now green; **no pixel outside the selection changed** (checked every 5 px) | Pass |
| 4 | Shade with a textured pressure-sensitive brush | Sketch Pencil 14 px; six hatching strokes through `drawStroke` with simulated pressure 0.15 → 1.0 | the heavy end changed the pixels more than the light end; the heavy end shows several distinct levels (paper grain) | Pass (simulated pressure) |
| 5 | Smudge two colours without bleeding outside a feathered selection | Rectangle over the face–body border; radius 12; Feather; Blend/Smudge; eight drags across the border | feathered edge is partial (0 < coverage < 255); **no pixel with coverage 0 changed**; > 200 sampled pixels inside changed | Pass |
| 6 | Create a custom pencil and use it for line art | Brush panel → "Brush Studio…"; size 3, hardness 60 %, texture strength 60 % (Studio model calls); Duplicate as "Line pencil"; Done (button); draw on Ink | new preset is current and saved; the Ink layer changed | Pass |
| 7 | Create a textured ink preset and save/reload it | Studio on Clean Ink: paper texture, tooth mode, 50 %; Duplicate "Textured ink"; Export `.drawbrush`; Done; Import the file; reload the brush library from disk | export → import gives identical settings; after the reload both custom presets exist with their saved settings | Pass |
| 8 | Modify a pressure curve and verify its visual effect | New layer "Curve test"; stroke with simulated pressure 0 → 1 (linear); Studio: basic pressure response off, size dynamics on, pressure curve **inverted**, minimum 10 %; Done; same stroke | linear: thin at the light (left) end, thick at the right; inverted: the reverse (column heights) | Pass (simulated pressure) |
| 9 | Copy an eye, flip it, paste onto a new layer | Ink row; Ellipse around the left eye; Copy; Paste (new layer); Transform; Flip H; drag right; Apply | one more layer; horizontal scale negative before Apply; the layer has content | Pass |
| 10 | Cancel a large multi-layer transform | Click Colour, Ctrl+click Ink and Curve test (3 layers, whole); Transform; angle 30 and a handle drag; Cancel | preview differed from the document; after Cancel **every layer is byte-identical** and history is unchanged | Pass |
| 11 | Undo and redo a complex sequence | Whole three-layer transform (−8°); two strokes with two presets; rectangle selection + transform (drag inside the box); then Undo and Redo button presses | every step changed the artwork; stepping back reproduces each earlier composite exactly, and stepping forward reproduces each later one | Pass |
| 12 | Save, close, reopen; inspect layers and presets | Save As; New; Open | same layer count, names and **every layer's pixels**; reloaded brush library: both custom presets, the pencil's size and the built-in's saved dynamics as saved | Pass |

## Problems found while building these tasks

| Found | Fixed |
|---|---|
| The Studio fields used `DoubleValidator`, whose locale fix-up turned "40.0" into "400" when the field lost focus (where "." groups digits). The milestone 8 transform fields had the same latent bug | Yes: locale-independent validators. `brushStudio` checks that leaving the field keeps the value |
| Lazy loading of the Studio created a `canvas: canvas` binding loop inside the loader component | Yes |
| Test-side mistakes, not app bugs: an alignment check that assumed the original drawing was aligned at one row; a drag that grabbed the transform pivot at the box centre (correct app behaviour) | Test corrected |

The milestone 8 tasks (`artistTasksWithSelectionsAndTransforms`) still pass unchanged.

## Not covered

- **By-hand drawing quality and feel**: needs an artist and a pen.
- **Pen pressure and tilt from hardware**: simulated only.
- **Android**: the workflows were not run there. Only the Studio's touch handling was checked on
  the emulator (`docs/milestone9-testing.md`).
- **The same tasks in Krita**: not run (no Krita installation). The manual procedure is in
  `docs/milestone9-performance.md`, and the milestone 8 checklist is in
  `research/selections-transforms.md`.

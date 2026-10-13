# Milestone 11 known limitations

Everything here is known and was measured or observed. Nothing in this list is hidden behind a
working-looking control: settings that do nothing for a brush are hidden in the panel, and the
Studio dims them and says they do not apply.

## Evidence

- **No physical device and no real pen.** Pressure and tilt in every test are simulated. The
  Android arm64 build was built but never run. The x86_64 build ran on an emulator only, which
  says nothing about performance or pen behaviour.
- **No artist evaluation.** No illustrator has used these brushes, and "feels like" claims
  are not made.
- **No side-by-side comparison** with Krita, Procreate or Clip Studio Paint. This is not
  feature parity with any of them.

## Watercolour

- **It is an approximation.** It is a bounded cellular model (`WetLayer.h`), not a fluid
  simulation. There is no velocity field, no paper fibre flow, no back-runs from capillary
  action, and no colour separation of pigments.
- **Edge darkening is real but modest.** Rim-to-centre darkness is 1.13 with edge darkening 0.9
  against 1.03 without, and the rim is 1–2 px wide.
- **Lifting has limits.** It works on paint from the current wet session. Once a session ends
  (dried, or fixed by another edit), the paint is ordinary pixels and cannot be lifted.
- **No movement while the pen holds still.** Within a stroke, the simulation advances only with
  input events.
- **Wet paint is not stored in files.** Saving while wet saves the paint as it looks and says
  so. Reopening shows it as saved, dry.
- **Other actions end the session.** Another tool, a selection, a layer change, undo or redo
  fixes the paint exactly as it looks.
- **Large washes on dense documents cost frame time.** A 1600 × 240 px wash on a 10-layer
  2048 × 1536 document measured stroke events at p95 22 ms and drying ticks of 22–37 ms every
  50 ms. The paint dries about 1.3–1.5× slower than its setting, because of the time budgets.
  The document's composite refresh is the largest part of a tick.
- **Previews dry with capped work.** The scratchpad, thumbnails and brush sheet dry with bounded
  work (0.4–1.9 s for a 1600 px stroke on this machine). Very large preview strokes settle early
  and can look less developed than on the canvas.
- **Memory pressure can drop undo steps.** The wet layer's memory (up to about 21 MiB for a
  large wash, at most 600 tiles) counts against the memory budget like any other buffer. Under
  memory pressure it can contribute to old undo steps being dropped (milestone 10 behaviour).

## Bristles

- **2D contact model**, not a physical bristle simulation: no bristle bending, no paint
  thickness and no impasto.
- **No live end taper**: bristle state depends on every earlier dab, so the hold-back taper is
  kept.

## Dry media, ink, mixing, procedural

- **Fixed paper:** dry media use a paper fixed in document space. There is no separate paper per
  layer, and no blending stump for graphite.
- **Curve latency:** the Catmull–Rom curve adds one input sample of latency when it is on.
- **Live taper:** it works for coverage engines only, not smudge, wet paint, bristles or
  watercolour.
- **Pigment mixing** is per-channel Kubelka–Munk in linear light, an approximation. Three
  channels cannot hold pigment spectra, and pure screen primaries mix towards black.
- **Path-following strands need a direction**, so a single tap with Hair or Fur draws nothing.
- **Zigzag and scallop patterns** join only when the spacing equals their length.
- **No shape cache:** procedural elements are rasterised each dab.
- **Large procedural brushes:** Foliage at 200 px reaches p95 5–8 ms.

## Presets and settings

- **Downgrade loss:** an older build (milestones 9 and 10) that saves the brush library drops
  the milestone 11 engine presets it skipped. A single new-engine `.drawbrush` is refused by
  older builds.
- **Categories:** they are derived from what a preset does and cannot be edited. Search matches
  name and category only.
- **Panel controls:** the panel shows quick controls for the common settings. Engine-specific
  settings are in the Brush Studio.

## Performance

- **Possible small regression for old presets.** The milestone 10 presets render the same
  pixels. Per-event time on dense documents may be up to about 10–15 % higher (geomean
  1.01–1.15 in two A/B passes, with ±40 % noise per row). The cause of a small fixed overhead
  for tiny dabs is not isolated.
- **Slow test suite:** `tst_brushengine` takes about 3 minutes, because the registry audit
  renders 1035 property-engine pairs.

## Found outside milestone 11 (reported, not changed)

- **Transform apply can drop undo steps with an almost empty budget.** Applying a transform after
  a session with layers and a selection made the memory evictor drop the oldest undo steps
  although the budget was nearly empty ("the memory limit", 12 MiB used of 7870 MiB). The
  milestone 10 baseline does the same. It lies in the transform and memory-budget code, which
  belongs to milestone 10.5 (`docs/m11-artist-workflows.md`).
- **tst_ui depends on free memory.** Its 16384² workflows fail when another large process (here
  the Android emulator) holds most of the free memory. The budget-checked `toImage` is refused.

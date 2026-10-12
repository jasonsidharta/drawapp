# Selections and transforms research (milestone 8)

References for our own C++/Qt implementation. Nothing here is copied: no code, no icons, no
cursors. Facts below were read in the source; inferences are marked as such.

## Sources and versions inspected (2026-10-12)

| Source | Version | Paths read |
|---|---|---|
| Krita (https://invent.kde.org/graphics/krita), GPL | `25840ee8949d9c845f077bbc8188743d1364a9d6` (2026-10-11) | `libs/image/KisSelectionTags.h` (SelectionAction), `libs/image/kis_pixel_selection.h/.cpp` (`applySelection`, `addSelection` 184–213, `subtractSelection` 215–247, `intersectSelection` 249–270, `invert` 343–363), `libs/ui/actions/kis_selection_action_factories.cpp` (Select All 147–183, Deselect 185–204, Reselect 206–216, `trimDevice` 70–110, Cut/Copy 300–453), `libs/ui/tool/kis_selection_tool_helper.cpp` (fill rule, 230–245), `plugins/tools/selectiontools/kis_tool_select_rectangular.cc` and `kis_tool_select_outline.cc` (anti-aliasing option, 77–106), `plugins/tools/tool_transform2/strokes/inplace_transform_stroke_strategy.cpp` (selection transform 184–203, `createCacheAndClearNode` 873–945, restoring selections 1018–1024), `kis_free_transform_strategy.cpp` (handle functions 28–47, aspect and pivot modifiers 454–730), `tool_transform_args.cc` (default filter) |
| PhotoCraft (https://github.com/storytold/photocraft), MIT or Apache-2.0 | `20cb95bc272feb9691537bccb46e84e583f5a04a` (2026-10-12) | `crates/algo/src/selection.rs` (`SelectionMode` 18–37, `combine` 138–166, `polygon` 770–813, tests), `crates/engine/src/selection_cmds.rs` (`point_in_selection` 32–34, `set_selection` 63–69, `lasso`/`polygon_selection` 577–610), `crates/ui-egui/src/transform_tool.rs` (function list; interpolation defaults at 242, 340, 451, 501, 583), `crates/ui-egui/tests/drag_preview_canvas.rs` (header, 1–40) |
| Photoshop / Clip Studio Paint documentation | not re-read in this session | Interaction conventions below are **recalled**, not verified against the current manuals |

Neither Krita nor PhotoCraft could be run here: Krita is not installed on this machine, and
PhotoCraft needs a Rust toolchain that is not installed. Everything in this note comes from
reading code, not from hands-on use.

## Selection model

**Krita (verified in code).**
- A selection is a `KisPixelSelection`: an 8-bit coverage paint device (`MIN_SELECTED`..`MAX_SELECTED`)
  with a default pixel, so "everything selected" or "inverted" can extend beyond the image.
- Combination (`kis_pixel_selection.cpp`):
  - Add = **saturating sum** `min(255, a + b)`, not max. Two anti-aliased shapes that share an
    edge (each 50 % on the boundary pixels) sum to 100 %, so no seam appears where they meet.
  - Subtract = saturating difference `max(0, a − b)`.
  - Intersect = `min(a, b)`, including the default pixels.
  - Symmetric difference exists as well.
- Invert flips every stored pixel and the default pixel (`255 − a`).
- Select All, Deselect and Reselect are undoable commands in the image's history
  (`KisDeselectActiveSelectionCommand`, `KisReselectActiveSelectionCommand`). Deselect keeps the
  selection so Reselect can bring it back.
- The rectangular and outline tools have an anti-alias option (`setAntiAliasPolygonFill`), turned
  off when feathering. Shapes are combined as `QPainterPath`s with `Qt::WindingFill`.

**PhotoCraft (verified in code).**
- `combine` quantises the new mask to the 8-bit grid it stores, then uses Add = **max**,
  Subtract = `max(0, p − q)` and Intersect = `min`.
- When the result has no tiles, `combine` returns `None`, which is the same value as "no
  selection" (`set_selection` reports `selected: false` to the caller). `point_in_selection`
  treats `None` as "everything selected" (`is_none_or`). *Inference:* subtracting the whole
  selection turns into "no selection", and the next edit applies everywhere unless the UI
  intervenes. This is the case our spec says must never happen silently.
- The lasso fills a polygon with an even-odd scanline at 4 sub-rows per pixel and exact
  horizontal coverage (`polygon`).
- Each selection change is one history step (`s.edit(label, …)`).
- `drag_preview_canvas.rs` exists because the marquee preview was once a 1 px white line that
  vanished on a white document. The fix uses contrasting black/white "ants", tested on white and
  black.

**Lessons for DrawApp.**
1. **Keep "no selection" and "empty selection" distinct.** Painting with an explicit empty
   selection must change nothing and say why. The PhotoCraft path above shows how easily an empty
   result collapses into "everything".
2. **Add as a saturating sum (Krita), not max (PhotoCraft).** Adjacent anti-aliased shapes then
   join without a half-transparent seam. Subtract is a saturating difference; Intersect is min.
3. **Selection changes are undo steps** in both references. Reselect needs the last selection
   kept after Deselect.
4. **Draw the outline in two contrasting colours** so it reads on any artwork; test it on white
   and black.
5. **Fill rule.** Krita uses winding fill for combined paths, PhotoCraft even-odd for the lasso.
   For a freehand lasso that crosses itself we choose nonzero winding, so everything the artist
   circled is inside.

## Painting through a selection

- **Krita:** the painter takes the selection (`KisPainter::setSelection`) and applies it when the
  dab is composited. Cut uses `clearSelection`, which leaves `pixels × (1 − selection)`.
- **PhotoCraft:** the stroke composite multiplies `k` by the selection once (brush research,
  `render.rs`).
- **Lesson:** apply coverage once, at compositing, against the pre-stroke pixels. Multiplying
  per dab compounds, so soft edges darken or fill in, the "repeated edge multiplication" our spec
  warns about.
- **Smudge** reads the layer, so it needs the same treatment:
  - let it work unclipped on a stroke-local copy;
  - write `before + (work − before) × selection` once.
  - It can therefore sample colour from outside the selection but writes only inside. This
    combination is our design; the references were not checked for it.

## Transforms

**Krita in-place transform (verified in code).**
- **Begin** (`createCacheAndClearNode`):
  - the source is cached as `layer × selection` (`bitBlt` with the selection), or the whole
    layer without one;
  - the layer is cleared with `clearSelection` (or fully), as a stroke transaction.
- **Previews** are rendered from the cached source each time (no re-sampling of earlier
  previews), into the layer, inside the stroke.
- **Cancel** undoes the stroke's transactions.
- **The selection** is hidden during the transform and transformed once at the end
  (`transformDevice` on the pixel selection), so it follows the pixels. Selections and overlay
  masks are made visible again afterwards.
- **Handles** (`kis_free_transform_strategy.cpp`):
  - eight scale handles, rotate outside the box, a movable rotation centre;
  - Shift or "keep aspect ratio" constrains scaling;
  - Alt toggles transforming around the rotation centre.
- **Resampling filter:** Bicubic by default (`tool_transform_args.cc`).
- **Canvas edge:** Krita paint devices are unbounded, so pixels moved beyond the canvas are kept
  in the layer (*inference from the device model*).

**PhotoCraft (function list and defaults only).**
- Transform sessions for layers, copies, placed images, paths and selections (`begin*`), with
  `commit` and `cancel`.
- Interpolation defaults to bicubic for pixels and bilinear for selection masks.
- The preview is a texture of the source drawn by the GPU (`preview_image`, *inference from the
  names*).

**Lessons for DrawApp.**
1. **Cache the source once** (`layer × selection`) and the remainder (`layer × (1 − selection)`).
   Render every preview from that cache.
2. **Our layers are canvas-sized**, so pixels moved off the canvas are lost on Apply. Say so; do
   not imply otherwise. While the preview is open, nothing is lost, because the source stays
   whole.
3. **The preview must not touch the document.** Krita previews inside the layer and relies on
   stroke undo. Our spec asks for a non-destructive preview, so we render into a preview
   composite and write to the layer only on Apply.
4. **The selection follows the transformed pixels** after Apply (Krita), so shading the moved
   part continues to work.
5. **Resample premultiplied colour** and clamp colour to alpha, so transparent pixels cannot
   leak colour into edges. Use bicubic for the final result (both references default to it), and
   prefilter large reductions.

## Copy and paste

- **Krita, with a selection:** copy multiplies the layer by the selection and crops to the
  selection's exact bounds (`trimDevice`). Cut clears with `× (1 − selection)`.
- **Krita, without a selection:** copy and cut take whole layers (`copyLayersToClipboard`,
  `cutLayersToClipboard`).
- **Lesson:** `copy = pixels × s` and `cut leaves pixels × (1 − s)`, so pasting a soft-edged copy
  back over a cut restores the soft edge's total. Without a selection, copy the whole active layer.

## Interaction conventions (recalled documentation, not verified)

- **Photoshop:**
  - Shift adds, Alt/Option subtracts, Shift+Alt intersects when starting a marquee or lasso.
  - Ctrl+D deselects, Ctrl+Shift+D reselects, Ctrl+Shift+I inverts, Ctrl+T free transform.
  - Enter commits, Esc cancels.
  - Since 2019, corner handles scale proportionally by default, and Shift toggles that.
- **Clip Studio Paint:** selection launcher bar under the selection (Deselect, Invert, Transform
  and similar).

## Comparing the artist experience (milestone 8, stage 5)

### What was tested (our app)

`tst_ui::artistTasksWithSelectionsAndTransforms` runs the seven tasks on a drawn character
(colour layer under an ink layer, 2048 × 1536). It drives the real controls with synthetic mouse
input on the offscreen platform. Interactions counted as the test performs them:

| Task | Our steps | Count | Result |
|---|---|---|---|
| 1. Lasso the head, enlarge proportionally | Select, Lasso, lasso drag, Transform, Alt + corner drag, Apply | 6 | Works on the active layer only: the ink head grows, but the colour layer under it does not (one transform per layer). Without Alt the opposite corner stays put and the head drifts |
| 2. Rotate a hand and move it | Rectangle, drag, Transform, type 35 in the angle field (or drag the knob), drag inside, Enter | 6 | Works |
| 3. Add/subtract regions, shade inside | Ellipse, drag, Add, drag, Subtract, drag (or Shift-drag / Alt-drag), choose Soft Airbrush, Brush tool, strokes | 9 + strokes (6 with modifiers) | Works; nothing changes outside the region |
| 4. Copy an eye to another layer and flip it | Ellipse, drag, Copy, Paste, Transform, Flip H, drag, Apply | 8 | Works after a fix: dragging the middle of the small box used to move the pivot instead of the eye |
| 5. Cancel a transform | Transform, drag, Cancel | 3 | The document is identical (compared pixel for pixel) |
| 6. Undo/redo an applied transform | Undo, Redo | 2 | Identical before and after |
| 7. Save and reopen | Save As, Open | 2 dialogs | Composite and layer count identical; the selection is not saved (by design) |

Edge quality was checked by tests, not by eye on hardware:
- transparent edges keep their colour within a few levels after scaling and rotating;
- whole-pixel moves and flips are exact copies;
- soft selection edges give at most their own coverage.

Preview responsiveness was measured as CPU time only (BENCHMARKS.md milestone 8).

### Krita: documented or inferred, not tested here

From the code (see above) and Krita's documented conventions:
- the transform tool works on the current layer, with options for more;
- the rotation centre is a handle;
- Shift constrains, Ctrl+T starts;
- the default filter is bicubic;
- canvas-unbounded layers keep pixels moved off the canvas.

None of this was observed in the running application for this report.

### Not run, and why

- Krita is not installed on this machine. Installing and driving it needs the desktop, which
  needs an agreed idle period.
- PhotoCraft needs a Rust toolchain that is not installed.
- No comparison claims are made from CPU timings or screenshots.

### Manual comparison checklist (same machine, same artwork)

Preparation:
- Export the workflow's character (or any two-layer drawing, colour under ink) as PNG layers.
- Open it at 2048 × 1536 in DrawApp and in Krita, at the same zoom (fit to window) and the same
  window size.
- Use a mouse first, then a pen if available.

For each task 1–7 above, in each application, record:
1. **Interactions:** clicks, drags, key presses and dialogs from start to done. Note any menu you
   had to look for (discoverability).
2. **Selection visibility:** can you see the outline on the light face and the dark ink at 25 %,
   100 % and 400 % zoom?
3. **Handles:**
   - are the handles easy to grab with the mouse and with a finger or pen?
   - does dragging the middle of a small selection move it?
   - how do you keep proportions and scale from the centre?
4. **Preview responsiveness:** does the preview follow the pointer smoothly for a small selection
   and for the whole layer? Note any visible lag or jumps (a phone camera at 240 fps can measure
   it).
5. **Edge quality** after task 1 (enlarge) and task 2 (rotate), zoomed to 400 %:
   - ink edges: sharp or soft, any halo;
   - pencil texture: is the grain kept or smeared;
   - soft transparency (the cheek shading): any dark or light fringe.
6. **Intent:**
   - did the whole head (ink and colour) grow, or only one layer?
   - did the flipped eye land where intended?
   - after Cancel, is everything exactly as before?
7. **Undo/redo and save:** one undo step per transform? Reopen: identical? (Krita also keeps
   selections in .kra files; DrawApp does not save selections.)

Record DrawApp's version (git hash) and Krita's version.

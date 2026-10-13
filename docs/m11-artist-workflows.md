# Milestone 11 artist workflows

What was exercised as an artist would use the new brushes, how, and what that does and does not
show. **No artist has used milestone 11 hands-on, and no physical pen was used.** Every stroke
below is synthetic: scripted samples with simulated pressure, or emulator touch input. These
workflows show that the features work together. They do not show that the brushes feel good
under a real pen.

## 1. Scripted illustration on the canvas (Windows, automated)

`tst_canvas::artistWorkflowWithMilestone11Brushes` runs through the real `CanvasItem`: tools,
layers, selection, transform, history and files. It runs on every ctest. The steps:

1. A graphite (HB) sketch: two curved strokes.
2. A new layer with a Watercolour Wash in blue, then a second wash in orange across it while the
   first is still wet (wet-on-wet), left to dry.
3. A new layer with G-Pen ink over the sketch.
4. A rectangular selection, then Splatter across and past it. Nothing changes outside the
   selection (checked every 5 px).
5. A new layer with Grass along the bottom, Foliage and Round Bristle.
6. The ink layer moved 40 px with a transform (Apply runs in the background).
7. Every step undone and redone. After each one, the picture must equal the one recorded when
   that step was first reached: a hash of the composite, by history state. It matched at 12 points.
8. Save, check the file's composite, reopen, compare: identical, 4 layers.

The result is `build/.../tests/tst_canvas_m11_workflow.png`, the canvas at 100 %. It shows:
- two glazes, the orange crossing the blue;
- the sketch with the ink line 40 px to its right;
- splatter inside the old selection;
- leaves at the bottom right.

**Found while writing it.** On this machine, applying the transform made the memory evictor
drop the oldest undo steps: 5 in this run, with the notice "Memory is running low …". The
evictor ran although the budget held 12 MiB of 7870 MiB; the reason recorded was "the memory
limit". The milestone 10 baseline does the same with the same sequence and milestone 10 brushes
(10 of 14 steps dropped), so it predates milestone 11. It lies in the transform and memory-budget
code that milestone 11 does not change, and is reported for milestone 10.5. The test logs it
instead of failing.

## 2. Watercolour session on the canvas (Windows, automated)

`tst_canvas::watercolour*`:
- the paint keeps changing after pen-up and then stops;
- the wet stroke is one undo step;
- it dries on the real timer: Watercolour Round in 2.5 s;
- a second stroke into wet paint differs from the same stroke after drying, at the crossing
  only: 5786 px there, 0 elsewhere;
- another tool, a selection, undo or a new layer fixes the paint exactly as it looks;
- saving while wet says so, keeps drying, and the file holds the picture as it was when saved.

## 3. Brushes by touch on the Android emulator

Pixel Tablet API 36, x86_64, headless, driven by adb. Details are in BUILD.md:
- the brush panel with categories, and search by name and category;
- the Watercolour Wash scratchpad;
- a watercolour stroke on the canvas that kept changing while it dried, then undone whole;
- Round Bristle, Grass, Hair, Foliage and Splatter strokes.

It found one problem: the panel's Hardness slider showed for brushes that ignore hardness. That
is fixed.

## 4. The brush sheet

`brush_sheet` renders all 38 presets on stripes, transparency and in close-up. It was rendered
and looked at after each engine stage (AGENTS.md), and it is how two bugs were found:
- the first watercolour rim was jagged;
- strands were clipped to their roots.

## Not done

- Hands-on use by an illustrator, with a real pen, on a tablet or a pen display.
- A side-by-side comparison with Krita or Procreate on the same task (neither is installed).
- Long sessions: hours of painting, hundreds of layers of watercolour, memory over time.

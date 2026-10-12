# Milestone 10: artist workflows (automated)

**What this is.** Fifteen tasks an artist would do with large canvases and many layers, run through
the real interface (`Main.qml` and `CanvasItem`) by `tst_ui::artistWorkflowsMilestone10`, with
**synthetic input**: QtTest mouse events, the Enter key, and generated stroke samples with
simulated pressure. The window is rendered offscreen. It checks that the features work together
and that nothing breaks at these sizes; it is **not** hands-on validation by an artist, and it
says nothing about pen feel, latency or comfort. No physical pen, tablet or phone was used.

Run: `ctest -R tst_ui` (or `tst_ui artistWorkflowsMilestone10`). Screenshots:
`build/windows-mingw-release/tests/tst_ui_m10w_*.png` (looked at; described below).
Result on 2026-10-12, Windows 11, MinGW release build: **all fifteen tasks passed**.

| # | Task | How it is driven | What is checked |
|---|---|---|---|
| 1 | New 16384 × 16384 canvas | File menu dialog: preset button, OK | canvas size; no tile allocated |
| 2 | Six long sketch strokes across the sheet | generated samples with pressure | fewer than a tenth of the sheet's tiles allocated; layer memory under 256 MiB |
| 3 | See the whole sheet | Fit (5 % zoom, view pyramid) | the strokes are visible in the window capture |
| 4 | Work at 100 % in the middle of the sheet | zoom to actual size, mouse stroke | stroke painted |
| 5 | Twenty layers, five painted in colour | Add button ×19, strokes | 20 layers (milestone 9 could not hold one layer of this size: 1 GiB against its 512 MiB limit) |
| 6 | Erase a stroke completely | eraser strokes | the layer keeps less than a quarter of its tiles |
| 7 | Undo ten steps, redo them | Undo button, redo | history count and pixels as before |
| 8 | Check memory | File > Memory usage… | dialog shown, closed with its button |
| 9 | Save the 20-layer 16384² project and open it again | Save As, Open | file under 32 MiB; 20 layers; pixels equal |
| 10 | Open a project saved by milestone 9, paint, save | Open, mouse stroke, Save | file is format 3 afterwards |
| 11 | Transform six 4096² layers together | select all layers, Transform, rotate and scale, Apply button | Apply runs in the background with progress shown (held at a test gate so the state can be captured) |
| 12 | Try to paint while it applies; stop; apply again | mouse stroke, Stop button, Enter | refusal notice; zoom works; Stop leaves the drawing unchanged; Apply finishes as one undo step; undo restores |
| 13 | Magic wand on the large canvas | Select tool, wand, click | a selection exists |
| 14 | Copy all layers and paste them | Select all, copy, paste | layer count doubles |
| 15 | Export PNG | export | 4096 × 4096 PNG written |

## What the screenshots show

- `m10w_03_huge_fit`: the 16384² sheet at 5 %, six thin crossing strokes, smooth (pyramid level 4).
- `m10w_05_twenty_layers`: the layer panel reads "Layers (20/64)", five coloured strokes.
- `m10w_11_applying`: the transform bar shows "Applying… 0 %", a progress bar and "Stop" instead of
  Apply; the preview box is still drawn.
- `m10w_12_applied`: the six bands rotated and narrowed, no seams between tiles.
- `m10w_08_memory`: the Memory usage dialog.

## Problems found while writing these tasks

- The app could only create 2048 × 1536 canvases (New had no size). Added **File > New canvas…**
  with presets up to 16384 × 16384.
- A mouse click on Apply right after starting the transform from code missed the button: the
  transform bar becomes visible with the transform and its buttons are placed at the next layout
  pass, and the test computed the click position before that pass (diagnosed by printing the
  button's position before and after: 244,646 vs 808,818). A test timing issue, not an app
  defect (a person cannot click before the bar is drawn); the task now waits for the layout and
  clicks Apply, and applies the second time with Enter.
- A milestone 9 file whose active layer is hidden refuses painting until another layer is chosen
  (expected behaviour, made explicit in task 10).
- The "cannot be undone" notice said "0 MiB" for small budgets; sizes under 1 MiB now read in KiB.

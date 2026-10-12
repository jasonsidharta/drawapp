# Milestone 9 known limitations

What is missing, partial or unverified after milestone 9, with severity:
- **High**: blocks a reasonable professional workflow or risks the user's work.
- **Medium**: noticeable gap with a workaround.
- **Low**: polish or rare case.

No known data-loss or painting-correctness bug is open. The two found this milestone (a
multi-threaded preview race and a 1 GiB read reservation) are fixed and tested.

## Validation gaps

| Limitation | Severity | Notes |
|---|---|---|
| No physical pen tested: pressure, tilt, palm rejection and brush feel are unverified | High | Every pressure and tilt result in this milestone uses simulated input. Instructions for a device test are in BUILD.md (milestone 7) |
| Workflows automated, not performed by an artist | Medium | `docs/milestone9-artist-workflows.md` |
| No comparison with Krita or PhotoCraft was run | Medium | Neither can run here. Manual procedure in `docs/milestone9-performance.md`. No claim of being lighter or faster |
| Android performance and frame stability not measured | Medium | Emulator only, used for behaviour |
| `.drawbrush` import/export through Android pickers not tried on the emulator | Low | The same code path as other provider files; unit-tested on desktop |
| No sanitizers (ASan/UBSan/TSan) run | Medium | MinGW on Windows lacks them (`docs/milestone9-testing.md`) |
| Golden images recorded on Windows only | Low | Tolerance allows small floating-point differences; never run on Android or Clang |

## Painting engine

| Limitation | Severity | Notes |
|---|---|---|
| Wet paint is a prototype | Medium | Deposit, pick up and run out of load only. No diffusion, drying, absorption, persistent wetness or pigment (subtractive) mixing |
| Spray and hatching are subsets | Low | No particle shapes or physics; hatching has one direction (no crosshatch passes) |
| No bristle simulation, clone, sketch-connect, filter or deform engines | Medium | Listed in `research/milestone9-feature-parity.md` |
| 26 of the 120 designed properties are planned only | Medium | e.g. wet edges, more blend modes (overlay, colour…), sample all layers, streamline, motion prediction, barrel rotation, spikes. `docs/brush-settings-specification.md` |
| End taper delays the live line by the taper length | Low | Inherent to the hold-back design: the tail is drawn at pen-up |
| A tap with a start taper is a small dot | Low | By design (the start taper applies at distance 0) |
| Flips have no effect on round tips | Low | Documented in the specification ("Has an effect with") |
| Colour dynamics cost 16 KiB extra per touched tile | Low | Only when colour varies per dab |
| Brush settings are not saved in projects | Low | As in milestone 6; presets live in the brush library |

## Layers, selections and transforms

| Limitation | Severity | Notes |
|---|---|---|
| Document limit 512 MiB of layer pixels; 4096 × 4096 allows at most 8 layers (the required 20-layer workload is refused) | High for large-format work | Full-canvas layers by design (decision D1). Lifting the limit needs sparse or tiled layers |
| A multi-layer transform on large canvases can exceed the 128 MiB undo budget; it is applied but cannot be undone | Medium | The app now **says so** (notice) instead of failing silently |
| Transform Apply on many large layers runs on the UI thread (2.3–3.2 s for 7 × 4096²) | Medium | A progress indication or a worker thread is future work |
| Preview of a multi-layer transform is about 4–7 fps at 4096² and about 30–40 fps at 2048 × 1536 (9 layers) | Medium | Merged preview; layers with a selection, or non-adjacent layers, use the slower per-layer preview |
| The merged preview can differ slightly from the applied result at overlapping anti-aliased edges | Low | Apply is exact. Tested |
| No masks, clipping layers or layer groups | Medium | Not part of DrawApp yet. Transforms therefore have nothing to respect there |
| No floating selection state; paste always makes a new layer | Low | Transform the pasted layer instead |
| Magic wand "All layers" samples the visible composite at the time of the click | Low | As documented |
| Selections are not saved in files | Low | As in milestone 8 |

## Brush Studio and presets

| Limitation | Severity | Notes |
|---|---|---|
| No Krita `.kpp` or Procreate `.brush` import | Medium | Investigated; not attempted (`docs/brush-preset-format.md`) |
| Reset returns to the preset as saved; no named reset points | Low | |
| Built-in brushes keep their names (rename needs Duplicate) | Low | |
| Studio preview uses one fixed stroke with simulated pressure | Low | Labelled; the try-out pad uses real input |
| Brush file operations run on the UI thread | Low | Files are at most 2 MiB |
| Studio layout below 760 px width is a compact single column | Low | Checked at 420 px (desktop) and on the tablet emulator in portrait; not on a phone |

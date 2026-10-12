# Milestone 9 testing

How milestone 9 was tested, what passed, and what was not run. Commands are in BUILD.md. The
`ctest` suites run offscreen, so they never take over the desktop.

## Results (Windows 11 25H2, MinGW 15.1 Release build, Qt 6.12.0)

| Suite | Test cases passed (QtTest totals, incl. init/cleanup and data rows) | Failed | Skipped | Milestone 9 additions |
|---|---:|---:|---:|---|
| tst_core | 34 | 0 | 0 | — |
| tst_brush | 37 | 0 | 0 | library format 2 accepted, format 3 refused |
| tst_brushengine (new) | 28 | 0 | 0 | the whole suite (below) |
| tst_selection | 34 | 0 | 0 | multi-layer transform core, wand, grow/shrink, feather, outline transform, preview race regression |
| tst_document | 63 | 0 | 0 | bounded file reading |
| tst_recovery | 28 | 0 | 0 | — |
| tst_canvas | 100 | 0 | 0 | multi-layer transform/clipboard, polygon lasso, wand and modify commands, feathered smudge, outline transform, undo-budget notice, merged-preview accuracy |
| tst_ui | 13 | 0 | 0 | `brushStudio`, `artistWorkflowsMilestone9` |
| **Total** | **337** | **0** | **0** | baseline before milestone 9: 287 in 7 suites, all passing |

Every suite passed in the final run (`ctest`, 8/8). Nothing is skipped or blacklisted.

## `tst_brushengine` (new suite, headless)

| Area | Tests |
|---|---|
| Sensor curves | `sensorCurves`: linear exactness, inverted, threshold, exponential, clamped, monotone presets, no overshoot on a spike, sanitising (sorting, NaN, 16-point limit), JSON and malformed JSON |
| Combination rules | `dynamicsCombination`: multiply, add, max, min, difference, minimum; sensors without data left out; random repeatable per seed, dab and parameter |
| Dynamics JSON | `dynamicsJson` |
| Pressure → size, opacity, flow | `dynamicsPressureToSize` (incl. no pressure data and inverted curve), `dynamicsPressureToOpacityAndFlow` |
| Tilt → rotation | `dynamicsTiltToRotation` (incl. no tilt data) |
| Speed → spacing | `dynamicsSpeedToSpacing` (incl. no event times) |
| Numerical stability | `dynamicsAreStableAtExtremes`: zero and NaN pressure, duplicate and backwards timestamps, 380 px in 1 ms, a 10⁹ ms jump |
| Seeds | `randomSeedReproducibility`: same seed gives the same pixels; another seed differs; splitting the input differently changes at most the spacing phase |
| Colour dynamics | `colorDynamicsVaryPerDab`: many hues, coverage unchanged, pressure → lightness |
| Taper | `taperStartAndEnd`: widths, end held back until pen-up, live result equals the one-shot render |
| Spray, hatching | `sprayAndHatching` |
| Blend modes | `paintModes`: normal, multiply, screen, behind and preserve alpha, over opaque and transparent pixels |
| Smudge | `smudgeModes`: smear vs dulling, colour rate |
| Wet paint | `wetPaintLoadsDepositsAndDrags`: load, running out, dilution, pull, selection applied once |
| Imported tips | `importedTipIsUsedAndSaved` |
| Presets | `presetFormat2RoundTripAndUnknownKeys`, `presetFileGolden`, `libraryVersion1Migrates` |
| Fuzz | `malformedPresetFuzz` (400 randomly corrupted presets), `randomSettingsFuzz` (60 random settings incl. out-of-range values, rendered) |
| Registry | `registryIsComplete` (120 properties, group sizes, unique ids, ranges), `everyImplementedPropertyChangesRendering` (all 94 + 2 engine selectors), `specificationIsCurrent` |
| Golden images | `goldenImages`: 15 reference strokes (8 built-in presets + spray, hatching, hue jitter, wet ink, taper with dynamics), tolerance 1 % of pixels and at most 3 levels |
| Regression | `builtInPresetsStillRender` |

## The brush correctness list (milestone 9 section 10)

| Required | Where |
|---|---|
| Pressure → size, opacity, flow | `dynamicsPressureToSize`, `dynamicsPressureToOpacityAndFlow`, `tst_brush::pressure*` |
| Tilt → shape rotation | `dynamicsTiltToRotation`, `tst_brush::tipFollowsDirectionOrTilt` |
| Speed → spacing | `dynamicsSpeedToSpacing` |
| Colour dynamic response | `colorDynamicsVaryPerDab` |
| Random seed reproducibility | `randomSeedReproducibility`, `tst_brush::deterministicReplay` |
| Interpolation continuity | `tst_brush::noGapsOnFastStrokes`, `splittingInputGivesSamePixels` |
| Alpha compositing, transparent pixels | `paintModes`, `tst_brush::noHalosOnTransparency` |
| Hard and soft edges | golden images, `everyImplementedPropertyChangesRendering` (hardness) |
| Selection and feathered boundaries | `featheredSelectionIsAppliedOnceByBrushes`, `smudgeInsideAFeatheredSelectionDoesNotBleed`, wet paint selection, workflow 5 |
| Eraser | golden images (hard and soft eraser), `tst_brush::eraseNearTransparencyAndEdges` |
| Smudge pickup vs deposition | `smudgeModes`, `tst_brush::smudgeCarriesColour` |
| Undo/redo of complex strokes | `runStroke` in bench_m9 (undo/redo per engine), workflow 11 |
| Preset save/load | `presetFormat2RoundTripAndUnknownKeys`, `presetFileGolden`, workflows 7 and 12 |

## Transform list (milestone 9 section 10)

| Required | Where |
|---|---|
| Multiple layers | `multiLayer*` (tst_selection, tst_canvas), workflows 1, 2, 10, 11 |
| Hidden and locked layers | `multiLayerTransformRefusesLockedOrHiddenLayers` |
| Masks and clipping | **Not applicable**: DrawApp has no masks or clipping layers |
| Scaling around pivots, rotation, mirroring, integer moves, off-canvas clipping | milestone 8 transform tests (still passing), workflows 1, 2, 9 |
| Cancel | `multiLayerCancelAndFailedApplyChangeNothing`, workflow 10 (byte comparison of every layer) |
| Undo/redo | `groupUndoIsOneStep`, workflow 11 |
| Transaction failure rollback | `multiLayerCancelAndFailedApplyChangeNothing` (simulated out-of-memory for one layer) |
| Save/reopen | `multiLayerTransformIsOneStepAndSurvivesSaving`, workflow 12 |

## Stress and fuzz (section 10)

| Required | Where / status |
|---|---|
| Very small and very large brushes | `randomSettingsFuzz` (1 … 80 px), bench_m9 (128 and 200 px brushes), `tst_brush::smallTipsAreAreaSampled` |
| Tiny and full-canvas selections | milestone 8 tests; `growAndShrinkByDistance` (shrinking to empty) |
| Thousands of rapid pointer events | bench_m9 long strokes (10 000 events per engine); `dynamicsAreStableAtExtremes` |
| Malformed presets, missing textures | `malformedPresetFuzz`, `presetFileGolden` (unknown texture) |
| Repeated undo/redo | workflow 11; bench_m9 undo/redo loops |
| Repeated preset switching | `tst_brush::everyPresetDrawsWithAndWithoutPressure`, workflows 3–8 |
| Save under memory pressure | **not run** (no memory-pressure harness). Allocation failure during Apply is simulated (`failRenderRegionForTesting`) |
| Device resize and interruption | `brushStudio` (narrow window); milestone 4–6 interruption tests still pass. Android lifecycle was not re-run this milestone |

## Tools that were not run

| Tool | Status |
|---|---|
| AddressSanitizer / UndefinedBehaviorSanitizer | **Not run.** MinGW GCC on Windows does not ship a working ASan runtime, and no Linux or Clang build environment is set up. The multi-threaded `QImage::scanLine` race found this milestone was found by a benchmark crash, not by a sanitizer |
| ThreadSanitizer | **Not run** (same reason). The transform row threads now receive plain pointers; `previewIntoSharedCopiesNeverTouchesTheLayers` checks the result, not the absence of races |
| Valgrind / Dr. Memory | **Not run** |
| Coverage-guided fuzzing (libFuzzer, AFL) | **Not run**. Random-mutation fuzz tests run inside the suites instead (`malformedPresetFuzz`, `randomSettingsFuzz`) |

## Golden images

`tests/golden/stroke-*.png` are the reference renders. A deliberate rendering change is
re-recorded with `DRAWAPP_WRITE_GOLDEN=1 tst_brushengine`, and the new images must be looked at
before committing them. The tolerance allows for floating-point differences between compilers
and CPUs: at most 1 % of pixels may differ, by at most 3 levels. The goldens were produced
on Windows (MinGW). They have not been checked on an Android or Clang build.

## Android (emulator, not a physical device)

The x86_64 debug APK was installed on the Pixel Tablet AVD (Android 16, API 36, `-no-window`) and
exercised by touch with `adb shell input`:
- the brush panel opens and shows "Brush Studio…";
- the Studio opens; categories switch;
- "Follow sensors" shows the curve editor; dragging a curve point by touch changes the curve and
  the preview;
- the try-out pad draws at full size (touch has no pressure);
- Cancel leaves the preset untouched.

The brush library written by the milestone 8 build was read by the new build (format 1 → 2).

Found and fixed on the emulator:
- "↺" and "✕" were missing from Android's font;
- the Studio's top row sat under the status bar.

Not checked on Android: pen pressure and tilt (the emulator has none), performance, and the
`.drawbrush` import/export pickers. Physical stylus validation is still open (ROADMAP,
milestone 7).

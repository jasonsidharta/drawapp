# Krita brush engine and editing internals (milestone 9 research)

Read for concepts only. No Krita code, presets, brush tips or textures are copied into DrawApp
(Krita is GPL; DrawApp is an independent implementation).

## Sources and revisions

| Revision | How it was read |
|---|---|
| `25840ee8949d9c845f077bbc8188743d1364a9d6` (2026-10-11), recorded in milestone 8 | Full local checkout (`C:\Users\Asus\OneDrive\Documents\cp lagi\krita`, read-only) |
| `e579e3a429646e270ac17c5fbcff709149c4bbc5` (2026-10-12 08:55 +0200), current `HEAD` on 2026-10-12 (`git ls-remote https://invent.kde.org/graphics/krita.git`) | Partial clone without a checkout (Windows path-length limits); blobs compared by hash |

Every source file cited below has the **same blob hash at both revisions**, verified with
`git rev-parse <rev>:<path>` on each side, so each finding applies to current Krita as well.

Official documentation consulted:
- https://docs.krita.org/en/reference_manual/brushes.html
- https://docs.krita.org/en/reference_manual/brushes/brush_settings.html

These were used only for user-facing names; every technical claim below comes from source.

Not done: Krita was not built or run on this machine, so nothing here is a runtime observation.

## Findings

Each finding gives where it is (repository paths at the revisions above), what the code does,
why (where evidence exists), the performance implications, and what DrawApp does with it.

### 1. Engine abstraction and registration

**Where:**
- `libs/image/brushengine/kis_paintop.h` (`class KisPaintOp`: public `paintAt`, `paintLine`,
  `paintBezierCurve`; protected pure virtuals `paintAt(const KisPaintInformation&)` returning
  `KisSpacingInformation`, `updateSpacingImpl`, optional `updateTimingImpl`,
  `doAsynchronousUpdate`);
- `libs/image/brushengine/kis_paintop_registry.cc` (`KisPaintOpRegistry::instance()` loads plugins
  of type `Krita/Paintop`; `paintOp(id, settings, painter, node, image)`);
- `plugins/paintops/defaultpaintops/defaultpaintops_plugin.cc` (registers
  `KisSimplePaintOpFactory<KisBrushOp, KisBrushOpSettings, KisBrushOpSettingsWidget>("paintbrush", …)`
  and the clone op `"duplicate"`).

**What:** each engine is a plugin with three parts: the op, its settings and its widget. The
engine paints one dab per `paintAt` call and returns the spacing to the next one. The shared
line and curve code walks the path and asks the op for each dab.

**Why:** spacing and timing belong to the engine (they depend on its tip), while path walking is
shared. Engines can therefore differ completely (pixel, smudge, particle) and still share input
handling.

**Performance:** a virtual call per dab, which is negligible next to rasterising a dab.

**DrawApp:**
- **Adapt.** DrawApp keeps one `StrokeEngine` that owns path walking, sensors and compositing,
  and dispatches dab rendering per engine type. Engines are compiled in, not plugins: no plugin
  system for hypothetical needs (AGENTS.md).

### 2. Pixel brush pipeline

**Where:** `plugins/paintops/defaultpaintops/brush/kis_brushop.cpp`, `KisBrushOp::paintAt`.

**What:**
- Size, rotation, ratio, scatter, opacity/flow, softness and lightness options are each applied
  to the `KisPaintInformation` (`m_sizeOption.apply(info)` and so on).
- A `KisDabShape(scale, ratio, rotation)` is built.
- A `DabRequestInfo` is queued on `m_dabExecutor->addDab(request, dabOpacity, dabFlow)`.
- Spacing comes from `effectiveSpacing(scale, rotation, &m_airbrushData, &m_spacingOption, info)`.
- Dabs are rendered and merged later, in `doAsynchronousUpdate`, as parallel jobs
  (`KisDabRenderingJob.h`).

**Why:** dab mask generation is the expensive part. A queue lets dabs render on worker threads
while input continues.

**Performance:** parallel dab rendering scales with cores but needs ordering care (the queue
keeps duplicates sequential, per the comment in `doAsynchronousUpdate`).

**DrawApp:**
- **Adopt the per-option evaluation** (each parameter = base × dynamics).
- **Reject the asynchronous dab queue for now**: DrawApp's per-event costs are measured below 1 ms
  for default sizes (BENCHMARKS.md milestone 6); revisit for large textured brushes.

### 3. Sensors, curves and combination

**Where:**
- `plugins/paintops/libpaintop/KisDynamicSensorIds.h`: sensor ids pressure, pressurein,
  xtilt, ytilt, ascension (tilt direction), declination (tilt elevation), speed, drawingangle,
  rotation, distance, time, fade, fuzzy (per dab), fuzzystroke, perspective, tangentialpressure;
- `sensors/KisDynamicSensor.cpp`: `KisDynamicSensor::parameter()` maps the raw value through the
  curve's `floatTransfer(256)` table with linear interpolation;
- `KisCurveOption.cpp`: `computeValueComponents`, `ValueComponents::sizeLikeValue`,
  `rotationLikeValue`;
- `KisCurveOptionDataCommon.h`: `curveMode`, `strengthValue`, `useSameCurve`.

**What:**
- Each option has a list of sensors, each with its own curve (or one shared curve).
- Several sensors combine by `curveMode`: 0 multiply (default), 1 add, 2 max, 3 min,
  4 difference.
- Additive sensors (rotation-like) and absolute-rotation sensors are handled separately.
- The result is multiplied by the option's strength and bounded by
  `strengthMinValue..strengthMaxValue`.

**Why:** one framework serves every engine parameter. The combination rule is explicit because
pressure × tilt and max(pressure, speed) mean different things to artists.

**Performance:** the curve is sampled into a 256-entry table (`floatTransfer(256)`), so a sensor
costs one lookup per dab.

**DrawApp:**
- **Adopt:** sensors, a curve per mapping, the five combination rules and a 256-entry curve
  lookup table.
- **Adapt:** DrawApp's dynamic value is `minimum + (1 − minimum) × combined`, the same
  "lightest value" control the milestone 5 UI already uses.

### 4. Speed, distance, time, random

**Where:**
- `libs/image/brushengine/KisStrokeSpeedMeasurer.h` (`currentSpeed()`);
- `libs/image/brushengine/kis_paint_information.h` (`drawingAngle`, `drawingSpeed`,
  `drawingDistance`, `currentTime`, `totalStrokeLength`, `randomSource`,
  `perStrokeRandomSource`);
- `KisPerStrokeRandomSource.h`.

**What:** speed is measured from timestamped points along the stroke. Random numbers come from a
per-dab source and a per-stroke source (Fuzzy Dab and Fuzzy Stroke).

**Performance:** cheap per dab.

**DrawApp:**
- **Adopt** per-dab and per-stroke random values from the stroke seed (DrawApp already hashes
  the seed with the dab index), and speed from sample timestamps with smoothing. Duplicate or
  out-of-order timestamps keep the previous speed.

### 5. Spacing and interpolation

**Where:** `libs/image/kis_distance_information.cpp` (`spacingUpdateInterval`,
`timingUpdateInterval`, isotropic and anisotropic next-point positions).

**What:**
- Spacing can be updated during a segment (an update interval), not only at each dab.
- Anisotropic spacing handles elliptical (ratio-scaled) dabs.
- Airbrush timing is separate from distance spacing.

**DrawApp:**
- **Already equivalent** for distance and time dabs (milestone 6).
- **Adapt:** DrawApp gains spacing dynamics (speed → spacing) by evaluating spacing at each dab.

### 6. Colour dynamics

**Where:** `plugins/paintops/libpaintop/KisHSVOption.cpp`
(`KisHSVOption::createHueOption/…Saturation…/…Value…`, `apply(KoColorTransformation*, info)`).

**What:** hue, saturation and value options are curve options, applied as a colour
transformation to the dab colour.

**DrawApp:**
- **Adopt** hue/saturation/lightness as ordinary dynamic parameters.
- DrawApp's coverage model needs a per-pixel colour when the colour varies within a stroke (see
  `milestone9-architecture-decisions.md`).

### 7. Texture

**Where:** `plugins/paintops/libpaintop/kis_texture_option.cpp` (`KisTextureOption::apply`,
`applyLightness`, `applyGradient`; texturing modes MULTIPLY, SUBTRACT, LIGHTNESS, GRADIENT,
HEIGHT, LINEAR_HEIGHT and Photoshop-style height variants).

**What:** the texture is applied to each dab as a mask with a composite op, with strength,
scale and offset options.

**DrawApp:**
- **Already have** Multiply and Subtract in document space (milestone 6).
- **Adopt** brightness, contrast, invert and rotation of the texture, plus pressure → texture
  strength.
- **Defer** the height and gradient modes.

### 8. Colour smudge

**Where:** `plugins/paintops/colorsmudge/kis_colorsmudgeop.cpp` and the
`KisColorSmudgeStrategy*` files: smudge rate and colour rate options, smearing vs dulling
(`KisSmudgeLengthOptionData::SMEARING_MODE`/`DULLING_MODE`), `smearAlpha`, and a paint-thickness
option (`KisPaintThicknessOption*`).

**What:**
- Smearing copies from the previous dab position; dulling uses an average colour.
- Colour rate adds the brush colour.
- Paint thickness is a height channel for lightness strategies.

**DrawApp:**
- **Adopt:** smear vs dulling modes, colour rate (brush colour mixed into the carried paint)
  and smudge length.
- **Defer** paint thickness.

### 9. Preset serialisation (`.kpp`)

**Where:** `libs/image/brushengine/kis_paintop_preset.cpp`: loading reads
`reader.text("preset")`; saving uses `QImageWriter writer(dev, "PNG")` and
`writer.setText("preset", doc.toString())`.

**What:** a `.kpp` is a PNG thumbnail whose `preset` text chunk holds the XML settings. Resources
(tips, textures) are referenced, or embedded in bundles.

**DrawApp:**
- **Reject as the native format.** DrawApp uses versioned JSON (`docs/brush-preset-format.md`).
- **No `.kpp` importer** is implemented. Krita settings are engine-specific XML with resource
  references; an importer is future work and is not claimed.

### 10. Multi-layer transforms

**Where:** `plugins/tools/tool_transform2/kis_transform_utils.cpp`,
`KisTransformUtils::fetchNodesList`, called from both transform stroke strategies.

**What:**
- Krita transforms the selected root nodes and their descendants.
- A node is included only if `node->isEditable(node == root)`, so locked nodes, and a hidden
  root, are skipped silently.
- File layers, colorize masks and selection nodes are excluded, and shape layers only in free
  transform.

**DrawApp:**
- **Adapt:** DrawApp transforms all selected layers with one shared geometry.
- **Reject the silent skip.** A locked or hidden layer in the selection stops the transform with
  a message naming it, because silently leaving one layer behind breaks the alignment the artist
  asked for.

### 11. Tiles, undo and transactions

**Where:**
- `libs/image/tiles3/kis_tile_data_interface.h` (`__TILE_DATA_WIDTH 64`, `__TILE_DATA_HEIGHT 64`);
- `kis_tile_data.h` (swap support);
- `kis_memento_manager.h` (`commit()`: changed tiles are recorded per commit);
- `libs/image/kis_transaction.h` (`KisTransaction::commit`, `endAndTake`).

**What:**
- Paint devices are sparse 64 × 64 tile maps with copy-on-write tile data and swapping to disk.
- A transaction records the tiles changed since it began (mementos), and undo restores them.

**DrawApp:**
- **Already similar for undo** (`TileSnapshot` stores the changed 64 × 64 tiles).
- **Layer storage stays full-canvas images this milestone**; see the architecture decisions for
  why, and the measured limit this imposes (4096 × 4096 × 20 layers is beyond DrawApp's 512 MiB
  document limit).

### 12. Dirty regions and projection updates

**Where:** `libs/image/kis_simple_update_queue.cpp` (merges dirty rects before they are processed),
`kis_update_scheduler.h`, `kis_updater_context.h`, `kis_async_merger.cpp`.

**What:** dirty rectangles are queued, merged and recomposited on worker threads.

**DrawApp:**
- **Already** refreshes only the changed rectangle of the composite, on the UI thread.
- **Keep:** measured composite costs (BENCHMARKS.md) do not justify a scheduler yet.

### 13. Dab caching

**Where:** `plugins/paintops/libpaintop/kis_dab_cache_base.cpp`: `SavedDabParameters::compare`
with precision levels, including subpixel tolerances.

**What:** a dab is reused when scale, rotation and subpixel offset match within the selected
precision level.

**Why:** consecutive dabs of a steady stroke are often identical.

**DrawApp:**
- **Measure first.** DrawApp evaluates the round tip analytically per pixel (no mask generation),
  so the cache's gain is smaller. Considered for sampled tips and spray particles if profiling
  shows it.

## Not studied in depth

The bristle (`hairy`), sketch, spray and hatching engines were only listed. DrawApp's spray and
hatching designs are its own and not derived from those engines' code.

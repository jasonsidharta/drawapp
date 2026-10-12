# Brush preset format

DrawApp stores brush settings in two places, both UTF-8 JSON and both read by the same code
(`BrushSettings::fromJson`, driven by the property registry):

1. **The brush library** (`brushes.json` in the app data folder, or the file named by
   `DRAWAPP_BRUSH_FILE`): custom presets, saved changes to built-ins, favourites and the selected
   presets. Format `"drawapp-brushes"`, defined in `src/core/BrushPresetStore.h`.
2. **A shared preset file** (`.drawbrush`): one preset to export, import or send to someone.
   Format `"drawapp-brush"`, defined in `src/core/BrushPresetFile.h`.

Projects (`.drawapp`) do not contain brush settings; their format did not change in milestone 9.

## `.drawbrush` (format version 2)

```json
{
    "format": "drawapp-brush",
    "version": 2,
    "metadata": { "app": "DrawApp", "settingsVersion": 2 },
    "settings": {
        "name": "Golden wet ink",
        "blend": "wet",
        "stamp": "tip",
        "size": 18,
        "spacing": 0.12,
        "pressureSize": { "enabled": true, "minimum": 0.2, "gamma": 1.4 },
        "tipImage": "iVBORw0KGgo…",
        "dynamics": {
            "opacity": {
                "enabled": true,
                "minimum": 0.25,
                "combine": "max",
                "sensors": [
                    { "sensor": "pressure", "curve": [[0, 0], [0.125, 0.024], …, [1, 1]] },
                    { "sensor": "speed", "curve": [[0, 1], [1, 0]], "range": 3 }
                ]
            }
        },
        "…": "every other implemented property, by its registry id"
    }
}
```

A complete committed example is `tests/golden/golden-wet-ink.drawbrush`.

### What the settings hold

- **Engine.** `blend` (`paint`, `erase`, `smudge`, `wet`) and `stamp` (`tip`, `spray`,
  `hatching`).
- **Resources.**
  - `tip` is the id of a generated tip, `""` for the round tip.
  - `tipImage` is an imported tip embedded as a base64 PNG (8-bit grayscale, square, at most
    256 × 256). It is written only when one is set.
  - `texture` is the id of a generated texture, `""` for none.
- **Every implemented property**, under its registry id with its JSON type: numbers, integers,
  booleans, option names as strings, and pressure responses as objects. The full list, with
  ranges and defaults, is `docs/brush-settings-specification.md`.
- **Sensor curves and randomisation.**
  - `dynamics` maps a target id (`size`, `opacity`, `flow`, `spacing`, `rotation`,
    `roundness`, `scatter`, `textureStrength`, `hue`, `saturation`, `lightness`,
    `smudgeStrength`) to `{enabled, minimum, combine, sensors}`.
  - Each sensor is `{sensor, curve, range}`. The curve is a list of 2–16 `[x, y]` points in
    0..1. The range is present for speed, acceleration, distance, fade and time.
  - Only dynamics that differ from "off" are written.
  - Jitter settings and random sensors are seeded per stroke, so a preset holds no random state.
- **Metadata.** `name` (at most 64 characters) inside the settings, and `metadata.app` with the
  settings version at the top level. Metadata is informational and is not checked on reading.

### Reading rules

| Situation | Result |
|---|---|
| Not JSON, not an object, wrong `format`, no `settings`, a non-integer or missing `version` | refused with a message; nothing is imported |
| `version` greater than 2 | refused: "saved by a newer version of DrawApp" |
| File over 2 MiB | refused before parsing (read with a bounded buffer) |
| A known key with the wrong JSON type (e.g. `"size": "big"`), an unknown option name, an unknown sensor or combination, a malformed curve | refused; the message names the setting |
| A number outside its range, or NaN | clamped into the range (NaN becomes the default) |
| A key this version does not know (top level or under `dynamics`) | **kept** and written back unchanged on export or save |
| An unknown `tip` or `texture` id (a newer version, a removed resource) | kept; drawn as the round tip or without texture; a warning is shown |
| A `tipImage` that is not a readable PNG | ignored with a warning; the tip shape is used |
| A missing key | the property's default |
| An empty name | "Imported brush" |

Embedding imported tips means a preset never refers to a file outside itself, so a missing
external asset cannot happen. The only references are the ids of generated tips and textures,
which are handled as described above.

### Versions and migrations

| Settings version | Milestone | Difference |
|---|---|---|
| 1 | 6 | the milestone 6 properties (size … smoothing, the pressure responses, tilt factors, texture, airbrush, smudge) |
| 2 | 9 | adds the engines, the property groups and `dynamics` |

Version 2 only adds keys and changes the meaning of none, so the migration is direct. A version 1
preset reads as version 2 with every new property at its default (off or neutral), and renders
exactly as it did in milestone 6. Milestone 6's basic pressure responses are kept as they are
rather than converted to curves (decision D3 in `research/milestone9-architecture-decisions.md`).

The brush library file went from version 1 to 2 with the same rule:
- Version 1 files are read, then written back as version 2.
- A milestone 6 build refuses version 2 files instead of loading them and dropping the new
  properties on its next save. It also never overwrites a file it refused.

Tested by `tst_brushengine::libraryVersion1Migrates`, using a hand-written milestone 6 library:
`tests/golden/library-v1.json`.

### Stability

Serialisation is stable: keys are sorted (`QJsonDocument`), the same settings always give the
same JSON, and `tst_brushengine::presetFileGolden` compares today's encoding with the committed
golden file. The embedded PNG is compared by pixels, because zlib versions may encode the same
image to different bytes.

## Brush library file (format version 2)

```json
{"format": "drawapp-brushes", "version": 2,
 "custom":    [{"id": "custom.3", "settings": {…the same settings object…}}],
 "overrides": {"builtin.clean-ink": {…}},
 "favorites": ["builtin.clean-ink", "custom.3"],
 "selected":  {"paint": "builtin.solid-round", "eraser": "builtin.hard-eraser"}}
```

- A single invalid preset or reference is skipped with a warning; the rest loads.
- A damaged file, or one from a newer version, is refused as a whole and never overwritten.
- At most 500 custom presets, and the file is at most 4 MiB.
- Writes are atomic (`ProjectFile::writeFileSafely`).

## Import and export in the app

The Brush Studio has *Export…* and *Import…*:
- *Export* writes the settings being edited, including unsaved edits, to a `.drawbrush` file.
- *Import* adds the file as a new custom preset and opens it in the Studio.

Local files are written atomically. Android documents (`content://`) are read and written
through the document provider. Brush files are small, so this happens on the UI thread.

## Other formats (investigated, not supported)

- **Krita `.kpp`** (research: `research/milestone9-krita-brush-engine.md`). A PNG preview image
  with the preset XML in a `preset` text chunk. The XML names a paint-op plugin and its
  settings, with sensor curves as point lists. Mapping it to DrawApp would need:
  - per-engine translation of Krita's paint ops (pixel, colour smudge, …);
  - Krita's tip and texture resources, which are separate files referenced by name or MD5 and
    usually not inside the `.kpp`, and which we would have no right to ship;
  - answers for the many settings we have no equivalent for.

  **No `.kpp` importer exists, and DrawApp makes no claim of Krita preset compatibility.**
- **Procreate `.brush` / `.brushset`**. A zip archive with an Apple binary property list and
  shape/grain images. The format is not publicly documented, so no importer was attempted, and
  DrawApp makes no claim of compatibility.
- **ABR (Photoshop brushes)**. Not investigated in this milestone.

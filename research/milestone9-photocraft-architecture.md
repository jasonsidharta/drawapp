# PhotoCraft architecture (milestone 9 research)

Static source reading only: PhotoCraft was **not built or run**, because no Rust toolchain is
installed on this machine (`cargo` not found). Nothing here is a runtime observation.

## Repository and revisions

The repository is https://github.com/storytold/photocraft (MIT or Apache-2.0), the same one
studied in milestones 6 and 8 (its README names the ArtCraft team).

| Revision | Use |
|---|---|
| `20cb95bc272feb9691537bccb46e84e583f5a04a` (2026-10-12 02:26 −0400) | The milestone 6/8 checkout (`C:\Users\Asus\OneDrive\Documents\cp lagi\photocraft`, read-only) |
| `8b216216717c7105df105bf1664e592daeb38cd6` (2026-10-12 04:43 −0400) | Current `HEAD` (`git ls-remote`), shallow clone in the session scratchpad. All paths below are at this revision |

## Findings

### Document model

**Where:** `crates/doc/src/lib.rs`: `pub struct Document` (line ≈823) with `layers: Vec<Layer>`
and `selection: Option<Surface>`; `pub struct Layer` (≈589).

**What:** a document is a value: layers, a selection, channels and more, all made of
`Surface`s.

### Tiles and copy-on-write

**Where:** `crates/raster/src/lib.rs` ("Sparse, copy-on-write tiled surfaces"). `Surface` holds
`tiles: BTreeMap<TileCoord, Arc<Tile>>`; writes go through `Arc::make_mut` (line ≈177).
`crates/geom/src/lib.rs` sets `TILE_SIZE = 256`.

**What:** tiles are shared between document versions until one side writes. Absent tiles are
transparent (sparse).

**Lesson:** sparse copy-on-write tiles make whole-document snapshots cheap. DrawApp has
copy-on-write only at whole-layer granularity (`QImage`), which is why recovery snapshots cost
one layer copy on the next edit (BENCHMARKS.md milestone 4).

### Undo and redo, atomic edits

**Where:** `crates/engine/src/lib.rs`, `Session::edit` (line ≈609):
`let before = st.doc.clone(); let mut doc = (*before).clone(); let r = f(&mut doc, &mut active)?; st.doc = Arc::new(doc); …`.

**What:**
- Every edit runs on a clone of the document. Because of copy-on-write tiles, the clone costs
  the tree, not the pixels.
- The new document is installed only when the closure succeeds. On error (`?`), the clone is
  dropped and the session is unchanged.
- History stores document versions. Its byte budget comes from preferences
  (`history_budget_bytes`).

**Lesson for multi-layer transforms:** prepare every affected layer's result first, and change
the document only when everything succeeded. DrawApp follows this in its transform Apply (see
the architecture decisions), using its existing tile snapshots, not document clones.

### Selection, including the empty-selection behaviour

**Where:**
- `crates/algo/src/selection.rs`: `combine` (≈line 138) ends with
  `(s.tile_count() > 0).then_some(s)`; `combine_region` (≈374) has
  `(SelectionMode::Subtract | SelectionMode::Intersect, None) => None`, and a test asserts
  `combine_region(None, None, SelectionMode::Subtract).is_none()`;
- `crates/engine/src/selection_cmds.rs`: `point_in_selection` (line 32) is
  `doc.selection.as_ref().is_none_or(|selection| selection.sample_channel(x, y, 0) > 0.0)`.

**What:** a selection that becomes empty is stored as `None`, and `None` means "everything is
selected" for selection-aware operations.

**The behaviour DrawApp must avoid** (inference from the code above, not observed at runtime):
- subtracting everything, or intersecting with nothing, leaves "no selection";
- the next selection-aware operation then applies to the whole layer.

The command reports `selected: false`, so the UI could warn, but the document no longer tells an
empty selection from no selection. Bucket fill and other paint commands clone `doc.selection`
and pass it on (`paint_cmds.rs` lines 66, 122), so they inherit the same rule.

**DrawApp** keeps three states (none, active, empty; `src/core/Selection.h`), and every
selection-aware operation refuses on empty. This is unchanged in milestone 9 and covered by
`noSelectionAndEmptySelectionAreDifferent` and `emptySelectionNeverPaintsEverywhere`.

### Brush processing and dynamics

**Where:** `crates/paint/src/dynamics.rs` and `crates/paint/src/brush.rs` (7,210 lines across
`crates/paint/src`).

**What:**
- Photoshop-style dynamics: each parameter has a `Control` (Off, Fade, PenPressure, PenTilt,
  StylusWheel, Rotation, InitialDirection, Direction), a `minimum` and a `jitter`.
- `scalar()` (≈line 380): `(min + (1 − min) × control) × (1 − jitter × random)`, never below
  the minimum.
- Randomness comes from `crate::rng`, keyed by the stroke seed and the step or dab index, in
  separate streams per purpose (`stream::HUE`, `stream::SAT`, `stream::BRIGHT`, …).
- `ColorDynamics` (brush.rs ≈341): per-tip or per-stroke hue, saturation and brightness jitter,
  foreground/background mix and purity.

**Lessons:**
- Separate random streams per parameter, so changing one jitter does not reshuffle the others.
  DrawApp adopts this: the stream id is part of the hash.
- One control per parameter is simpler than Krita's sensor lists but cannot combine sensors.
  DrawApp follows Krita here: several sensors per parameter with an explicit combination rule.

### Canvas rendering and the GPU

**Where:** `crates/compose/src/lib.rs` (the CPU reference compositor: blend modes, masks,
clipping groups, adjustment layers) and `crates/gpu/src/lib.rs` (a wgpu compositor that matches
the CPU reference within about 1/255).

**What:**
- Layer surfaces live in GPU pages. A tile is re-uploaded only when its `Arc` changed, and pages
  over a byte budget are evicted least recently used first.

**Lessons:**
- A GPU compositor brings residency, upload and eviction policy, plus a CPU reference to test
  it against.
- DrawApp's measured composite costs (5 ms at 5 layers, 12 ms at 10, full canvas) do not justify
  that complexity this milestone. The spec says "do not introduce GPU complexity without
  demonstrating a benefit".

### Transforms

**Where:** `crates/ui-egui/src/transform_tool.rs`: `begin`, `begin_selection` (refuses an empty
selection: `if b.is_empty() { return Err("no selection") }`), `commit` (≈930; cancels if the
document changed underneath), `cancel` (≈1067).

**What:**
- The preview is a texture of the source.
- Commit runs one engine command (one history step).
- Interpolation defaults are bicubic for pixels and bilinear for selections.

**Lesson:** commit checks that the document is still the one the preview was made from. DrawApp
gets the same guarantee by cancelling on document replacement and applying before any other
edit (milestone 8).

### Layer selection

**Where:** `crates/engine/src/lib.rs`: `selected_layers: Vec<LayerId>` (≈188),
`fn selected_layers()` (≈258). A comment notes that selecting layers is not a history step.

**Lesson:** a multi-layer selection is document view state, not history. DrawApp does the same.

### Serialisation

**Where:**
- `crates/format/src/lib.rs`: the `.pcraft` bundle (`manifest.json` versioned tree,
  content-addressed `tiles/<blake3>.zst` 256 × 256 tiles, ZIP or directory, incremental saves);
- `crates/format/src/migrate.rs` (step-wise JSON migrations, version n → n + 1).

**Lessons:**
- Step-wise migrations of raw JSON keep old files loading.
- DrawApp adopts that pattern for brush presets (version 1 → 2), see
  `docs/brush-preset-format.md`.
- Content-addressed tile storage is a good fit for incremental saves; that is future work for
  DrawApp's project format.

### Memory

- Copy-on-write tiles plus a byte-budgeted history (`history_budget_bytes`).
- GPU pages evicted by budget.
- Not measured here.

## What DrawApp adopts, adapts or rejects

| Area | Decision |
|---|---|
| Empty selection collapsing to "everything" | **Reject**: keep none/active/empty |
| Prepare, then commit, for multi-layer edits | **Adopt** in the transform Apply |
| Random streams per parameter | **Adopt** |
| One control per parameter | **Reject** in favour of Krita-style multi-sensor mappings |
| Step-wise JSON migrations | **Adopt** for presets |
| GPU compositor | **Reject for now**: no measured need |
| Sparse copy-on-write tiles | **Future**: the biggest structural gap (memory for large documents) |

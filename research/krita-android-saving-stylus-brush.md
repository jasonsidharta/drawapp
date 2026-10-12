# Krita reference: Android saving, stylus and touch, smoothing, stroke opacity

Bounded research after milestone 5 (2026-10-12). It covers only what our milestones 5–6 depend
on: how Krita writes files on Android, how it separates pen, touch and palm, its smoothing modes,
and how it keeps opacity even within a stroke. It is not a study of Krita as a whole. Earlier
notes: `krita-input-and-canvas.md`.

## Reference checkout

| | |
|---|---|
| Revision | `25840ee8949d9c845f077bbc8188743d1364a9d6` (master, committed 2026-10-11 02:19 UTC); same as the first note |
| Local checkout | `C:\Users\Asus\OneDrive\Documents\cp lagi\krita`, outside this repository, read only for us. On 2026-10-12, `git status` showed the 21 files of `LICENSES/` as deleted in the working tree. This research did not change them, and nothing was written to the checkout |
| License | Mostly GPL-2.0-or-later / GPL-3; `packaging/android/.../DocumentSaverService.java` is LGPL-2.0-or-later. Nothing from it is copied into this project |
| Policy | Krita's README sets a moratorium on AI-assisted development *of Krita*. Reading it to inform an independent application is a different activity; the owner should be aware of it |

## Provenance

Our milestone 5 code (`SafeSave`, `AndroidDocumentProvider`, `StrokeCompositor`,
`StrokeSmoother`, `PressureResponse`) was written **before** this research was done; this note was
written afterwards to compare. No Krita code, identifiers, structure or comments were copied or
translated, and none should be. Where this note recommends a change, it describes the behaviour,
not Krita's implementation.

## Source paths examined

| Path | What |
|---|---|
| `libs/ui/KisImportExportManager.cpp` (about lines 783–950) | `doExportImpl`: which platforms use `QSaveFile`; the Android write path; verification after writing |
| `libs/ui/KisDocument.cpp` ("ANDROID NOTES", about lines 742–816; also 1109, 1910, 2019) | Content URIs, writability checks, "file exists" on Android, backup location |
| `packaging/android/apk/src/org/krita/android/DocumentSaverService.java` | Saving while the app goes to the background |
| `libs/ui/kis_config.h`, `kis_config.cc` (`touchPainting`, `disableTouchOnCanvas`) | Finger painting on or off |
| `libs/ui/input/kis_touch_shortcut.cpp`, `kis_input_manager_p.cpp`, `kis_shortcut_matcher.cpp` (Android branches) | Touch painting priority, Android workarounds |
| `libs/ui/input/kis_input_manager.cpp` (`compressMoveEventCommon`) | Move event compression |
| `libs/ui/tool/kis_smoothing_options.h`, `kis_tool_freehand_helper.cpp` (`paint`, `effectiveSmoothnessDistance`, stabilizer functions about lines 795–943) | Smoothing modes |
| `libs/image/kis_indirect_painting_support.h`, `libs/ui/tool/strokes/kis_painter_based_stroke_strategy.cpp`, `libs/ui/tool/kis_resources_snapshot.cpp` | Per-stroke opacity ("indirect painting") |
| `3rdparty/` | Contains only `README.md` at this revision; Krita's Qt patches are not in this repository |

## Verified (read in source at the revision above)

### Saving on Android

1. **Krita does not use `QSaveFile` on Android, Windows or macOS.** The comment above
   `USE_QSAVEFILE` explains that on Android external storage there are no atomic renames, so
   `QSaveFile` would fall back to a direct write that truncates the file first. That leaves a long
   window in which Android can kill the app and leave an incomplete file.
2. **Android write path:** the document is written to a `QTemporaryFile` in the temp folder
   first. That file is then copied into the target in chunks, through `QFile` opened with
   `WriteOnly | Truncate`. Afterwards the bytes written are compared with the temporary file's
   size, and a mismatch is an error. Krita's own comment says this "is still not atomic and can
   end up with a partial file", only with a shorter window, and that after truncation "there's not
   much we can do".
3. **Truncation must be asked for explicitly on Android.** Krita's comment says opening a
   content URI for writing does not truncate by itself, which can leave old bytes at the end of
   the file.
4. **After writing, the export filter verifies the written file** (`filter->verify(location)`,
   "some minimal verification"), on every platform.
5. **Writability checks are skipped on Android.** Per "ANDROID NOTES", `isWritable` returns false
   on some providers, including the default provider on some devices. Krita assumes files are
   writable and relies on the write failing instead.
6. **"Exists" means "non-empty" on Android.** Requesting a file from the system always creates an
   empty one, so Krita treats a document as existing only if its size is greater than 0.
7. **Save As and Export can never replace an existing file on Android.** They use the "create
   document" request, and if the user picks an existing name the system adds a number. Only File >
   Save overwrites.
8. **Some providers silently lose data.** The notes cite reports of archive (ZIP/RAR) providers
   that accept data without error and discard it.
9. **Backups and autosaves for content URIs go to the app's autosave folder**, because a URI may
   have no directory next to it.
10. **Saving survives backgrounding through a foreground service.** `DocumentSaverService`
    promotes itself to a foreground service (`FOREGROUND_SERVICE_TYPE_SHORT_SERVICE` on API 34+;
    it continues without it if Android refuses). It runs the native save on its own thread,
    repeats when another save was requested meanwhile, and waits for the save before letting the
    process be killed. Its "cancel" action kills the process, with a TODO: "Think about atomicity,
    which Qt doesn't support for Android".

### Pen, touch and palm

11. **Finger painting has three settings: Auto (default), On, Off.** In Auto, touch painting is
    disabled as soon as any tablet (pen) event has been received in the session
    (`KoPointerEvent::tabletInputReceived()`). Touch gestures such as pan and zoom keep working;
    only painting with a finger stops.
12. **Android-specific input workarounds:** one for Xiaomi tablets (multi-finger cancel), and
    one that keeps canvas actions enabled when the S Pen hovers off the canvas (on other platforms
    actions are suppressed while the cursor is outside).
13. **No Android pen code lives in this repository.** None of the Java sources handle
    `TOOL_TYPE_STYLUS` or pressure; pen events reach Krita as Qt tablet events. How its patched
    Qt delivers them is not visible here (see item 21).
14. **Move events are compressed only for tools that don't need high-resolution input.** Brushes
    receive every move event.

### Smoothing

15. **Modes:** none, simple, weighted, stabilizer, pixel-perfect.
16. **Weighted smoothing** averages recent positions with Gaussian weights over the *distance*
    travelled. The source comment says it deliberately uses distance instead of velocity because
    "time measurements are too unstable in real-world environments". Options cover the distance
    range (by speed), optional pressure smoothing, scaling with zoom, and "tail aggressiveness",
    which shortens the history where pressure drops at the end of a stroke.
17. **The stabilizer is time-based.** A poll timer (interval = the "sample size" setting)
    averages a queue of samples. An optional *delay distance* (a dead zone of radius R, divided by
    the zoom, drawn as a circle on the cursor outline) holds the brush until the pointer leaves the
    circle. With *finish stabilized curve* on, the stroke end flushes the pending samples; with it
    off, the tail is not completed.
18. **Smoothing distances are expressed on screen and divided by zoom** (`effectiveZoom`), except
    where an option scales them with the canvas.

### Opacity within a stroke

19. **"Indirect painting" ("wash").** When a preset is not incremental (`paintIncremental() ==
    false`), the stroke paints into a temporary target with the "alpha darken" composite: within
    a stroke, opacity rises to the dab's opacity instead of adding up. The layer shows that target
    with the stroke's composite op and opacity, and it is merged into the layer when the stroke
    ends. Incremental presets ("build-up") paint dabs directly, so overlaps accumulate.
20. **Brush settings are snapshotted per stroke** (`KisResourcesSnapshot`): opacity, composite
    op, colour and preset are read from the snapshot, not live.

## Unresolved

21. How Krita's Qt delivers Android stylus pressure, tilt and hover. The patches are kept outside
    this repository and were not read.
22. Whether Krita reads back and compares the whole file on Android, or only checks the size (the
    copy loop) and the filter's "minimal verification", which was not read in detail.
23. How the stabilizer's timer interacts with high-rate pens (240 Hz and more); not measured.

## Comparison with our milestone 5 implementation

| Topic | Krita (verified above) | Us | Assessment |
|---|---|---|---|
| Android overwrite | Temp file, then truncate and copy into the target; size check; not atomic (their own words) | Probe; ask before overwriting; local copy in the recovery file, read back; free-space check; `posix_fallocate` reservation **before any byte changes**; `"rw"` + `ftruncate` (no truncate-first); `fsync`; full read-back compare | Same honest conclusion (not atomic). Ours stops more failures before the document is touched and verifies more afterwards. Krita's foreground service also protects saves when the app is backgrounded; **we have no equivalent** |
| Truncation | Must request `Truncate` explicitly | `"rw"` without truncation, then `ftruncate` to the new length after writing | Independently the same fact; our order keeps the old contents until the write starts |
| "File exists" | Non-empty means existing | Empty document = just created, no overwrite prompt | Same rule |
| Save As can't replace | System appends a number | Same Android behaviour; our prompt handles the case where the picker returns an existing document anyway | Matches |
| Lossy providers | Known to exist; not detected by a size check of what was written | Read-back compare after writing | Ours would report a provider that drops data, provided reading it back goes through the same provider. **Untested on real providers** |
| Finger vs pen | Auto: no finger painting once a pen was used in the session | Touch ignored only while the pen is down and for 0.4 s after | **Gap:** ours is weaker against palms. Consider an Auto / On / Off setting with the same rule (behaviour, not code) |
| Smoothing | Weighted (distance-weighted average), stabilizer (timer + optional dead zone), tail controls | One pull-string mode; distance only; tail always completed | Both chose distance over time for the same reason. Our pull string resembles the stabilizer's dead zone, without its timer averaging. Their tail options show some users want the tail *not* completed; we have no option for that |
| Stroke opacity | Wash: temporary target with alpha darken, merged with opacity at stroke end | Max-coverage mask recomposited from the undo tiles | Same visible rule (no build-up within a stroke). Krita also offers build-up as a per-preset choice; we have only wash |
| Per-stroke settings | Resource snapshot | Captured in `beginStroke` | Same approach |
| Move compression | None for brushes | We filter tablet events at the window (every event). Qt Quick compresses **touch** moves until the next frame (found in milestone 5) | Pen paths are unaffected; touch strokes could lose fine detail on slow frames. Measure on hardware |

## Candidate follow-ups (behaviour descriptions for our own design)

1. **Keep saving when the app goes to the background on Android** (for example a short
   foreground service), so a save started just before the user switches apps completes.
2. **Finger painting: Auto / On / Off**, with Auto disabling finger painting once a pen has been
   used in the session; touch gestures keep working.
3. **Tail option for smoothing**: complete the tail at pen-up (current behaviour) or stop where
   the brush is.
4. **Build-up option** for brushes, next to the current uniform ("wash") opacity.
5. On a physical device, **test archive/cloud providers** with the read-back check, since Krita
   reports providers that silently drop data.

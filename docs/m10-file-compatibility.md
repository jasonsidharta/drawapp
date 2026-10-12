# Milestone 10: file format and compatibility

Byte layout: `src/core/ProjectFile.h`. Tests: `tests/tst_fileformat.cpp`, `tests/tst_document.cpp`,
`tests/tst_recovery.cpp`, `tests/tst_canvas.cpp`.

## Formats

| Format | Written by | Read by milestone 10 | Layer storage |
|---|---|---|---|
| 1 | milestone 1 | yes, as one layer | one dense zlib block, RGBA premultiplied |
| 2 | milestones 3–9 | yes | one dense zlib block per layer, RGBA premultiplied |
| 3 | milestone 10 | yes (written) | only painted 64 × 64 tiles, each a zlib block, BGRA premultiplied |

Format 3 keeps the 68-byte header, the SHA-256 of the payload and the JSON metadata of format 2,
and adds `tileSize` (root) and `tiles` (per layer). A layer block is a tile count followed by
`(column, row, length, zlib data)` entries in increasing row-major order. Edge tiles store only
their pixels inside the canvas. A tile that is not stored is transparent.

Recovery files keep container format 2; the embedded project is format 3 when written by
milestone 10. Recovery files from milestones 2–9 (embedding formats 1 or 2) are still read.

Milestone 9 and earlier cannot open format 3 files: they report "saved by a newer version of
DrawApp (format 3)" and change nothing. There is no "save as format 2" option.

## What was tested

**Files written by earlier builds** (`tests/golden/compat/`), compared with what the milestone 9
code itself decoded from them (`m9-fixtures.json`: per-layer SHA-256 of the premultiplied pixels,
every property, and the composite):

| File | Written by | Result in milestone 10 |
|---|---|---|
| `m1-manual.drawapp` | milestone 1 build (manual test, 2048 × 1536, format 1) | identical layer and composite |
| `m9-android-layered.drawapp` | milestone 9 Android build on the emulator (format 2) | identical |
| `m9-layered.drawapp` | milestone 9 code (commit 7469a69), 301 × 197, three layers: locked, hidden at 37 % opacity, Unicode names, non-contiguous ids, partial alpha, paint reaching the right and bottom edges | identical |
| `m9-sparse.drawapp` | milestone 9 code, 2048 × 1536, two strokes (one in the bottom-right corner) | identical |
| `m9-layered.drawrecovery` | milestone 9 code, recovery format 2 | identical document and metadata |

The milestone 9 fixtures were made by building milestone 9's `drawcore` from commit 7469a69 in a
separate worktree and running a small generator against it (the generator is not part of the
repository; the files and the expected values are). Each file is also saved again as format 3
and reopened: still identical.

**Size of the same documents** (bytes, from the test log):

| File | As written | As format 3 |
|---|---|---|
| `m1-manual.drawapp` | 19 124 | 9 137 |
| `m9-android-layered.drawapp` | 30 831 | 8 008 |
| `m9-sparse.drawapp` | 14 114 | 4 894 |
| `m9-layered.drawapp` (fully painted gradient background, 301 × 197) | 9 667 | 14 155 |

Format 3 is smaller when much of the canvas is empty, and can be **larger** for fully painted
canvases: each tile is compressed on its own, so zlib loses the context across tiles. Larger
documents are measured in `docs/m10-performance.md`.

**Malformed input** (`tst_fileformat::malformedFilesNeverCrashOrLeak`): 3 000 deterministic
mutations each of a format 3 project, a format 2 project and a recovery file (bit flips, random
byte runs, truncation, insertion, header edits; half with the checksum recomputed so the damage
reaches the structure checks). Every input either decoded to a valid document (sizes agree, ids
unique, active layer present) or was refused with a message and no layers; no tile stayed
allocated afterwards; `verify()` never accepted an input that decoding refused. Results: format 3
2 960 refused / 40 accepted, format 2 2 966 / 34, recovery 2 998 / 2. Accepted inputs are
mutations that remain well-formed (for example changed pixel bytes or names with a recomputed
checksum). Hand-written damaged format 3 files (`tst_document::rejectsDamagedTiledFiles`, 19
cases) check each validation rule by name. This is not coverage-guided fuzzing (no libFuzzer on
MinGW).

**Memory while opening**: the file size is reserved against `MemoryBudget` before it is read;
format 3 tiles are allocated against the budget while decoding, and a project that does not fit is
refused with a message, leaving nothing allocated (`tst_document::openingRespectsTheMemoryBudget`).
Declared sizes are checked before any allocation (`hugeDeclaredSizesAreRefusedBeforeAllocating`).

**Saving never destroys the previous file before the new one is complete**: local saves go
through `QSaveFile` (atomic replace); a failed write leaves the old file byte for byte
(`savingKeepsPreviousFileUntilTheNewOneIsComplete`, `tst_document::failedSaveKeepsPreviousFile`);
provider documents keep the milestone 5 behaviour (`tst_canvas` provider tests).

## Not tested

- Big-endian machines (the format is defined little-endian with BGRA pixel bytes; the code swaps
  on big-endian builds, but none was available).
- Files written by milestones 2–8 other than through the format 2 encoder kept in tests
  (`ProjectFile::encodeVersion2`); the milestone 9 fixtures cover the format 2 reader.
- Physical Android devices (the Android fixture comes from the emulator).

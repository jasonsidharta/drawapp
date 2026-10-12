# Concurrency design for milestone 10

Rules for which thread may touch what. Written before the implementation and kept as the
reference the code and tests check against (`docs/m10-test-results.md`).

## Threads that exist

| Thread | Owns / does |
|---|---|
| UI thread | the live `LayeredDocument`, `UndoHistory`, `TransformSession`, view caches; **the only writer of live pixels** |
| `ProjectIo` worker (milestone 4) | encodes and saves a `DocumentData` snapshot; decodes opened files |
| `RecoveryManager` worker (milestone 2) | encodes and writes recovery snapshots |
| Transform row pool (milestone 8) | renders rows of one output image in parallel; the caller waits |
| Transform Apply worker (new) | prepares a transform's results from immutable inputs |

## Invariants

1. **Single writer.** Only the UI thread mutates a live layer, the composite or a preview image.
   Workers mutate only images they created themselves.
2. **Immutable once shared.**
   - A tile with more than one owner is never written. A writer that needs it copies it first.
   - The uniqueness check uses an acquire load of the tile's atomic owner count. Owners release
     with release-ordered decrements.
   - When the count is 1, no other thread holds the tile, and none can obtain it: new references
     are made only by copying a `TiledImage` that already owns one.
3. **Tables are implicitly shared.** A `TiledImage` copy shares the tile table (atomic reference
   count). The first write on either side detaches the table first, which re-references every
   tile. Only after that is a tile's owner count checked.
4. **Snapshots for workers.** Workers receive `TiledImage`/`DocumentData` copies made on the UI
   thread. Holding a copy keeps every tile in it alive and unchanged, so a worker can read tile
   memory without locks.
5. **No `QImage::scanLine()` on worker threads** for an image that may be shared. Workers get
   plain pointers into buffers that the owning thread detached (milestone 9 fix, kept).
6. **Results are committed on the UI thread, by generation.**
   - Each background transform has a generation id and records each source layer's *version*
     (a per-layer counter bumped by every pixel mutation).
   - A finished job is delivered by a queued call. It commits only if its generation is current
     and every layer version still matches. Otherwise it is discarded and the document is
     untouched.
7. **Commit is all or nothing.**
   - The worker produces complete new images; allocation failure on the worker means "no
     result".
   - Commit only swaps references and records one undo command. It cannot fail half-way.
8. **Cancellation is cooperative.**
   - An atomic flag is checked per row band and per layer.
   - A cancelled job delivers nothing.
   - Cancel on the UI thread bumps the generation, so an in-flight result is rejected even if it
     finishes.
9. **Shutdown.** When `CanvasItem` is destroyed or a document is replaced, any running job is
   cancelled and waited for. Its inputs are owned by the job, so waiting is safe even after the
   document is gone.
10. **No edits during Apply.**
    - While a transform is being applied, painting, layer actions, undo and redo, file operations
      and starting another transform are refused with a message.
    - Viewing (pan, zoom) and Cancel stay available, so the UI never freezes for the
      computation.

## What is deliberately not done

- No lock-free structures beyond atomic reference counts, and no concurrent writers to one image
  (Krita's lock-free hash and multithreaded strokes). DrawApp's strokes are cheap enough on one
  thread (`docs/m10-performance.md`), and correctness comes first.
- No background compositing for painting updates (measured sub-millisecond per event).

## Verification

- Deterministic tests: snapshot isolation, writes after snapshots, concurrent readers while the
  UI thread writes, stale-generation rejection, cancellation at every stage, destruction during a
  job.
- Stress tests with many iterations.
- Sanitizers: availability is checked in `docs/m10-test-results.md`. MinGW has no TSan or ASan;
  results are reported as run or not run, never assumed.

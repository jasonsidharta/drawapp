# File-open memory: the milestone 9 claim re-measured (milestone 10, 2026-10-12)

Machine: Intel Core Ultra 7 155H, 15.4 GB RAM, Windows 11 25H2, MinGW 15.1 release builds.

## What was claimed

`BENCHMARKS.md` (milestone 9): "Peak private memory while opening a 0.1 MiB project: 1055 MiB
before, 29 MiB after (bounded read)". `docs/milestone9-performance.md` attributes the 1055 MiB to
`QIODevice::read(MaxFileBytes + 1)` reserving the 512 MiB limit, fixed by
`ProjectFile::readBounded` in commit 326374c.

## Method (equivalent conditions)

One small program (`openprobe`: `ProjectFile::load(path)` in a fresh `QCoreApplication` process,
then Windows `GetProcessMemoryInfo`: `PeakPagefileUsage` = peak private, `PeakWorkingSetSize`),
compiled three times, each against a different `drawcore`:

- **before**: commit eb47f39 (the parent of 326374c, last commit without `readBounded`);
- **milestone 9**: commit 7469a69 (final milestone 9);
- **milestone 10**: the current tree.

The milestone 9 libraries were built from separate git worktrees outside the repository. Each
build opened the same file five times, each run in a new process. The files were written by the
milestone 9 code (format 2), so every build reads the same bytes.

## Results (peak private memory of the whole process; five runs each, the spread was ≤ 0.1 MiB)

| File | before (eb47f39) | milestone 9 (7469a69) | milestone 10 |
|---|---|---|---|
| 512 × 512, 3 layers, 69 KB (`m9-workload-512x3.drawapp`, the milestone 9 "512² × 3" workload content) | 1033.6 MiB | 7.7 MiB | 7.6 MiB |
| 2048 × 1536, 2 layers, mostly empty, 30 KB (`tests/golden/compat/m9-android-layered.drawapp`) | 1076.5 MiB | 50.6 MiB | 26.5 MiB |

Peak working set: 47.6 / 15.5 / 14.4 MiB (512²) and 90.4–91.4 / 58.4–59.4 / 34.4–35.4 MiB
(2048 × 1536).

## Conclusion

- The **1055 MiB** figure is confirmed in kind: without the bounded read, opening any small
  project committed about 1 GiB (1033.6 and 1076.5 MiB here).
- The **29 MiB** figure was not a measurement of opening alone: it is the peak of the whole
  `bench_m9` 512² × 3 process, which also created, composited and saved the document. Opening
  alone peaks at **7.7 MiB** for that workload with the milestone 9 code. For a 2048 × 1536 project
  it is **50.6 MiB** with milestone 9 (dense decoding of each layer) and **26.5 MiB** with
  milestone 10 (unpainted tiles are not allocated; format 2 layers are still decoded densely one
  at a time).
- So "1055 → 29 MiB" overstated the comparison's precision but not its direction; the accurate
  statement is the table above.

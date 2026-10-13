# Compares bench_m11 output of the milestone 10 baseline and milestone 11 (docs/m11-performance.md).
# Usage: python tools/m11_ab_compare.py <directory with <section>-<m10|m11>-r<round>.md files>
# Writes ab_comparison.md into that directory: per brush and size, the median of the per-round
# medians for each build, and their ratio.
import os, re, statistics, sys
AB = sys.argv[1] if len(sys.argv) > 1 else 'ab'
SP = AB
sections = ['doc2048', 'doc4096', 'sparse8192', 'sparse16384']
cols = ['dabs', 'first', 'median', 'p95', 'max', 'tiles_med', 'tiles_max', 'buffers', 'undo_mib', 'undo_ms']


def parse(path):
    rows, extra = {}, {}
    m11 = False
    text = open(path, encoding='utf-8', errors='replace').read()
    for line in text.splitlines():
        if line.startswith('Milestone 11 engines'):
            m11 = True
        m = re.match(r'^\| ([a-z0-9-]+) \| (\d+) \| (.*)\|$', line)
        if m:
            vals = [float(v.strip()) for v in m.group(3).split('|')]
            rows[(m.group(1), int(m.group(2)), m11)] = dict(zip(cols, vals))
        m = re.match(r'^Long stroke \(2000 events, ([A-Za-z ]+) (\d+) px\): total ([\d.]+) ms', line)
        if m:
            extra['long ' + m.group(1) + ' ' + m.group(2)] = float(m.group(3))
        m = re.match(r'^Save median (\d+) ms, open median (\d+) ms, file ([\d.]+) MiB\. Peak private (\d+) MiB', line)
        if m:
            extra['save ms'], extra['open ms'], extra['file MiB'], extra['peak private MiB'] = map(float, m.groups())
        m = re.match(r'^Document private memory (\d+) MiB', line)
        if m:
            extra['document MiB'] = float(m.group(1))
        m = re.match(r'^Watercolour on a canvas wet layer, ([a-z-]+) (\d+) px: stroke event median ([\d.]+) / p95 ([\d.]+) / max ([\d.]+) ms; (\d+) drying ticks \(2 steps each\): median ([\d.]+) / p95 ([\d.]+) / max ([\d.]+) ms, total (\d+) ms; wet layer (\d+) tiles, ([\d.]+) MiB', line)
        if m:
            g = m.groups()
            extra['wet ' + g[0]] = dict(event_median=float(g[2]), event_p95=float(g[3]), event_max=float(g[4]), ticks=int(g[5]),
                                        tick_median=float(g[6]), tick_p95=float(g[7]), tick_max=float(g[8]), total=float(g[9]),
                                        tiles=int(g[10]), mib=float(g[11]))
    return rows, extra


def med(xs):
    return statistics.median(xs) if xs else float('nan')


out = []
for sec in sections:
    runs = {b: [parse(os.path.join(AB, f'{sec}-{b}-r{r}.md')) for r in (1, 2, 3) if os.path.exists(os.path.join(AB, f'{sec}-{b}-r{r}.md'))]
            for b in ('m10', 'm11')}
    if not runs['m10'] or not runs['m11']:
        continue
    out.append(f'\n### {sec} (median of {len(runs["m10"])}/{len(runs["m11"])} rounds)\n')
    out.append('| Brush | size | M10 median ms | M11 median ms | ratio | M10 p95 | M11 p95 | M10 max | M11 max | first px M10/M11 ms | tiles/event M10/M11 | undo MiB M10/M11 |')
    out.append('|---|---|---|---|---|---|---|---|---|---|---|---|')
    keys = [k for k in runs['m10'][0][0] if not k[2]]
    worst = []
    for k in keys:
        a = {c: med([r[0][k][c] for r in runs['m10'] if k in r[0]]) for c in cols}
        b = {c: med([r[0][k][c] for r in runs['m11'] if k in r[0]]) for c in cols}
        ratio = b['median'] / a['median'] if a['median'] > 0 else float('nan')
        worst.append((ratio, k))
        out.append(f"| {k[0]} | {k[1]} | {a['median']:.4f} | {b['median']:.4f} | {ratio:.2f} | {a['p95']:.4f} | {b['p95']:.4f} | "
                   f"{a['max']:.3f} | {b['max']:.3f} | {a['first']:.3f} / {b['first']:.3f} | {a['tiles_med']:.0f} / {b['tiles_med']:.0f} | "
                   f"{a['undo_mib']:.2f} / {b['undo_mib']:.2f} |")
    ex = {}
    for key in ['long Solid Round 60', 'long Textured Paint 40', 'save ms', 'open ms', 'file MiB', 'peak private MiB', 'document MiB']:
        av = med([r[1][key] for r in runs['m10'] if key in r[1]])
        bv = med([r[1][key] for r in runs['m11'] if key in r[1]])
        out.append(f'\n{key}: M10 {av:.1f}, M11 {bv:.1f}' + (f' (ratio {bv / av:.2f})' if av else ''))
    worst.sort(reverse=True)
    out.append('\nLargest median ratios (M11/M10): ' + ', '.join(f'{k[0]} {k[1]} px {r:.2f}' for r, k in worst[:5]))
    # Milestone 11 engines
    out.append('\nMilestone 11 engines (M11 only):\n')
    out.append('| Brush | size | median ms | p95 ms | max ms | first px ms | tiles/event | buffers MiB | undo MiB |')
    out.append('|---|---|---|---|---|---|---|---|---|')
    for k in [k for k in runs['m11'][0][0] if k[2]]:
        b = {c: med([r[0][k][c] for r in runs['m11'] if k in r[0]]) for c in cols}
        out.append(f"| {k[0]} | {k[1]} | {b['median']:.4f} | {b['p95']:.4f} | {b['max']:.3f} | {b['first']:.3f} | {b['tiles_med']:.0f} | "
                   f"{b['buffers']:.2f} | {b['undo_mib']:.2f} |")
    for w in ['wet watercolor-wash', 'wet watercolor-round']:
        vals = [r[1][w] for r in runs['m11'] if w in r[1]]
        if vals:
            d = {c: med([v[c] for v in vals]) for c in vals[0]}
            out.append(f"\n{w}: stroke event median {d['event_median']:.3f} / p95 {d['event_p95']:.3f} / max {d['event_max']:.3f} ms; "
                       f"{d['ticks']:.0f} drying ticks: median {d['tick_median']:.3f} / p95 {d['tick_p95']:.3f} / max {d['tick_max']:.3f} ms, "
                       f"total {d['total']:.0f} ms; {d['tiles']:.0f} wet tiles, {d['mib']:.1f} MiB")
open(os.path.join(AB, 'ab_comparison.md'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print('\n'.join(out))

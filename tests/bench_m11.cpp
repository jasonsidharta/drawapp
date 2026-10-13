// Milestone 11 benchmarks (research/m11-brush-architecture.md §5). The same source builds against
// the milestone 10 baseline (A/B: the milestone 10 presets on the same workloads) and against
// milestone 11, where DRAWAPP_M11 adds the new engines. Prints Markdown tables.
//
// All times are CPU processing time on the calling thread (QElapsedTimer around the call), not
// input-to-display latency. A stroke event is what the canvas does per input event: the engine
// call and the composite refresh of the changed area. Memory is the process's private bytes.
//
// Usage: bench_m11 <section> [events]
//   sections: doc2048 (2048 × 1536 × 10), doc4096 (4096² × 8), sparse8192 (8192² × 2, sparse),
//             sparse16384 (16384² × 2, sparse)
// Run each section in its own process, alternating the two builds, for comparable figures.

#include "BrushPresets.h"
#include "LayeredDocument.h"
#include "MemoryBudget.h"
#include "ProjectFile.h"
#include "RasterDocument.h"
#include "StrokeEngine.h"
#include "TileSnapshot.h"
#include "UndoHistory.h"
#ifdef DRAWAPP_M11
#include "WetLayer.h"
#endif

#include <QElapsedTimer>
#include <QFileInfo>
#include <QSysInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {

constexpr double MiB = 1024.0 * 1024.0;

double privateMiB(bool peak)
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX c{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&c), sizeof(c)))
        return (peak ? c.PeakPagefileUsage : c.PrivateUsage) / MiB;
#endif
    Q_UNUSED(peak)
    return -1;
}

struct Stats { double median = 0, p95 = 0, max = 0, total = 0; };

Stats summarize(std::vector<double> v)
{
    Stats s;
    if (v.empty())
        return s;
    for (double x : v)
        s.total += x;
    std::sort(v.begin(), v.end());
    s.median = v[v.size() / 2];
    s.p95 = v[std::min(v.size() - 1, size_t(v.size() * 0.95))];
    s.max = v.back();
    return s;
}

// The milestone 9/10 benchmark documents (same construction as bench_m9).
LayeredDocument makeDocument(QSize size, int layers)
{
    LayeredDocument doc(size);
    doc.pixels(0).fillRect(doc.pixels(0).rect(), 0xffffffff);
    const int dabsPerLayer = std::max(200, int(qint64(size.width()) * size.height() / 1250));
    for (int n = 1; n < layers; ++n) {
        LayerData layer;
        layer.id = doc.allocateId();
        layer.name = QStringLiteral("Layer %1").arg(n + 1);
        layer.image = TiledImage(doc.size());
        layer.opacity = n % 3 == 0 ? 0.7 : 1.0;
        std::vector<Dab> dabs;
        const int w = size.width() - 80, h = size.height() - 80;
        for (int i = 0; i < dabsPerLayer; ++i)
            dabs.push_back({QPointF(40 + (qint64(i) * 37 + n * 101) % w, 40 + (qint64(i) * 53 + n * 67) % h),
                            qreal(4 + (i + n) % 30)});
        RasterDocument::stampDabs(layer.image, dabs, QColor::fromHsv((n * 47) % 360, 160, 200), BrushMode::Paint);
        doc.insertLayer(doc.count(), layer);
    }
    doc.setActiveLayer(doc.layer(doc.count() - 1).id);
    doc.refreshComposite();
    return doc;
}

LayeredDocument makeSparseDocument(QSize size, int layers)
{
    LayeredDocument doc(size);
    for (int n = 0; n < layers; ++n) {
        if (n > 0) {
            LayerData layer;
            layer.id = doc.allocateId();
            layer.name = QStringLiteral("Sketch %1").arg(n + 1);
            layer.image = TiledImage(doc.size());
            doc.insertLayer(doc.count(), layer);
        }
        std::vector<Dab> dabs;
        for (int s = 0; s < 6; ++s) {
            const QPointF a(size.width() * (0.1 + 0.13 * s), size.height() * (0.1 + 0.11 * n));
            const QPointF b(size.width() * (0.2 + 0.12 * s), size.height() * (0.6 + 0.05 * n));
            const int steps = int(QLineF(a, b).length() / 3);
            for (int i = 0; i <= steps; ++i)
                dabs.push_back({a + (b - a) * (double(i) / steps), 9.0});
        }
        RasterDocument::stampDabs(doc.pixels(n), dabs, QColor::fromHsv((n * 61) % 360, 180, 120), BrushMode::Paint);
    }
    doc.setActiveLayer(doc.layer(doc.count() - 1).id);
    doc.refreshComposite();
    return doc;
}

// A wavy stroke of `events` input events, 4 ms apart, `length` px long, centred in the document,
// with simulated pressure (not a real pen).
std::vector<StrokeSample> trace(QSize size, int events, double length)
{
    std::vector<StrokeSample> v;
    const QPointF c(size.width() / 2.0, size.height() / 2.0);
    for (int i = 0; i < events; ++i) {
        const double t = double(i) / std::max(1, events - 1);
        StrokeSample s;
        s.pos = c + QPointF(length * (t - 0.5), length * 0.15 * std::sin(t * 12.0));
        s.pressure = 0.3 + 0.7 * (0.5 + 0.5 * std::sin(t * 7.0));
        s.timeMs = i * 4.0;
        v.push_back(s);
    }
    return v;
}

int tilesIn(const QRect &r)
{
    if (r.isEmpty())
        return 0;
    return (r.right() / 64 - r.left() / 64 + 1) * (r.bottom() / 64 - r.top() / 64 + 1);
}

struct Result
{
    Stats event;
    double firstPixelMs = -1; // from pen-down to the first call that changed pixels
    Stats tiles;              // 64 × 64 tiles in each event's changed area
    qint64 dabs = 0;
    double undoMiB = 0, engineMiB = 0, undoMs = 0;
};

Result runStroke(LayeredDocument &doc, const BrushSettings &settings, const std::vector<StrokeSample> &samples)
{
    Result r;
    const int active = doc.activeIndex();
    StrokeEngine engine(settings, QColor(40, 90, 160), 7, StrokeEngine::smoothingRadius(settings));
    TileSnapshot before;
    std::vector<double> events, tiles;
    QElapsedTimer sinceDown;
    sinceDown.start();
    for (size_t i = 0; i <= samples.size(); ++i) {
        QElapsedTimer t;
        t.start();
        const QRect changed = i == 0                ? engine.begin(doc.pixels(active), samples[0], before)
                            : i < samples.size()    ? engine.moveTo(doc.pixels(active), samples[i], before)
                                                    : engine.finish(doc.pixels(active), before);
        doc.refreshComposite(changed);
        events.push_back(t.nsecsElapsed() / 1e6);
        tiles.push_back(tilesIn(changed));
        if (r.firstPixelMs < 0 && !changed.isEmpty())
            r.firstPixelMs = sinceDown.nsecsElapsed() / 1e6;
    }
    r.event = summarize(events);
    r.tiles = summarize(tiles);
    r.dabs = engine.dabCount();
    r.engineMiB = engine.bufferBytes() / MiB;
    before.dropUnchanged(doc.layer(active).image);
    r.undoMiB = before.byteSize() / MiB;
    UndoHistory history;
    history.push(UndoCommand::pixels(doc.layer(active).id, std::move(before), QStringLiteral("stroke")));
    QElapsedTimer t;
    t.start();
    history.undo(doc); // leaves the document as it was
    r.undoMs = t.nsecsElapsed() / 1e6;
    return r;
}

void header()
{
    std::printf("| Brush | size px | dabs | first pixel ms | event median ms | p95 ms | max ms | tiles/event median | max | "
                "stroke buffers MiB | undo MiB | undo ms |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n");
}

void line(const QString &name, double size, const Result &r)
{
    std::printf("| %s | %.0f | %lld | %.3f | %.4f | %.4f | %.3f | %.0f | %.0f | %.2f | %.2f | %.2f |\n", qPrintable(name), size,
                r.dabs, r.firstPixelMs, r.event.median, r.event.p95, r.event.max, r.tiles.median, r.tiles.max, r.engineMiB,
                r.undoMiB, r.undoMs);
}

void documentBench(const char *title, QSize size, int layers, bool sparse, int events)
{
    std::printf("\n### %s: %d × %d, %d layers%s\n\n", title, size.width(), size.height(), layers, sparse ? ", sparse" : "");
    const double startMiB = privateMiB(false);
    LayeredDocument doc = sparse ? makeSparseDocument(size, layers) : makeDocument(size, layers);
    std::printf("Document private memory %.0f MiB.\n\n", privateMiB(false) - startMiB);
    header();
    // The milestone 10 presets at three sizes: these rows are the A/B comparison.
    const char *m10[] = {"sketch-pencil", "clean-ink", "solid-round", "soft-airbrush", "textured-paint", "blend-smudge",
                         "hard-eraser", "soft-eraser"};
    const double length = std::min(1600.0, size.width() * 0.8);
    for (const char *id : m10) {
        const BrushPreset *p = BrushPresets::builtIn(QStringLiteral("builtin.") + QLatin1String(id));
        if (!p)
            continue;
        for (const double sz : {12.0, 60.0, 200.0}) {
            BrushSettings s = p->settings;
            s.size = sz;
            s.airbrush = false; // the synthetic trace's timing is not what is measured
            line(QLatin1String(id), sz, runStroke(doc, s, trace(size, events, length)));
        }
    }
    // Long stroke: 2000 events.
    {
        BrushSettings s = BrushPresets::builtIn(QStringLiteral("builtin.solid-round"))->settings;
        s.size = 60;
        const Result r = runStroke(doc, s, trace(size, 2000, length));
        std::printf("\nLong stroke (2000 events, Solid Round 60 px): total %.1f ms, median %.4f ms, max %.3f ms.\n", r.event.total,
                    r.event.median, r.event.max);
        BrushSettings t = BrushPresets::builtIn(QStringLiteral("builtin.textured-paint"))->settings;
        const Result rt = runStroke(doc, t, trace(size, 2000, length));
        std::printf("Long stroke (2000 events, Textured Paint %.0f px): total %.1f ms, median %.4f ms, max %.3f ms.\n", t.size,
                    rt.event.total, rt.event.median, rt.event.max);
    }
#ifdef DRAWAPP_M11
    std::printf("\nMilestone 11 engines (preset size, then 200 px):\n\n");
    header();
    const char *m11[] = {"graphite-hb", "compressed-charcoal", "soft-pastel", "g-pen", "brush-pen", "dry-ink", "oil-blender",
                         "color-mixer", "smear-knife", "round-bristle", "flat-bristle", "dry-brush", "watercolor-wash",
                         "watercolor-round", "hair", "fur", "grass", "foliage", "splatter", "stitches", "chevron-trim",
                         "crosshatch-pen", "airbrush-spray"};
    for (const char *id : m11) {
        const BrushPreset *p = BrushPresets::builtIn(QStringLiteral("builtin.") + QLatin1String(id));
        if (!p)
            continue;
        for (const double sz : {p->settings.size, 200.0}) {
            BrushSettings s = p->settings;
            s.size = sz;
            line(QLatin1String(id), sz, runStroke(doc, s, trace(size, events, length)));
        }
    }
    // Watercolour on the canvas: the stroke paints into the layer's wet layer, which then dries
    // in ticks of two 16 ms steps (the canvas timer runs every 33 ms).
    for (const char *id : {"watercolor-wash", "watercolor-round"}) {
        BrushSettings s = BrushPresets::builtIn(QStringLiteral("builtin.") + QLatin1String(id))->settings;
        WetLayer wet(size);
        const int active = doc.activeIndex();
        TileSnapshot before;
        StrokeEngine engine(s, QColor(40, 90, 160), 7, 0);
        engine.setWetLayer(&wet);
        const auto samples = trace(size, events, length);
        std::vector<double> ev;
        for (size_t i = 0; i < samples.size(); ++i) {
            QElapsedTimer t;
            t.start();
            const QRect changed = i == 0 ? engine.begin(doc.pixels(active), samples[i], before)
                                         : engine.moveTo(doc.pixels(active), samples[i], before);
            doc.refreshComposite(changed);
            ev.push_back(t.nsecsElapsed() / 1e6);
        }
        doc.refreshComposite(engine.finish(doc.pixels(active), before));
        std::vector<double> ticks;
        TiledSurface surface(doc.pixels(active));
        int n = 0;
        while (wet.isWet() && n < 2000) {
            QElapsedTimer t;
            t.start();
            doc.refreshComposite(wet.step(surface, before, 2));
            ticks.push_back(t.nsecsElapsed() / 1e6);
            ++n;
        }
        const Stats e = summarize(ev), k = summarize(ticks);
        std::printf("\nWatercolour on a canvas wet layer, %s %.0f px: stroke event median %.3f / p95 %.3f / max %.3f ms; "
                    "%d drying ticks (2 steps each): median %.3f / p95 %.3f / max %.3f ms, total %.0f ms; wet layer %d tiles, "
                    "%.1f MiB.\n",
                    id, s.size, e.median, e.p95, e.max, n, k.median, k.p95, k.max, k.total, wet.tileCount(), wet.bytes() / MiB);
        before.swapWith(doc.pixels(active));
        doc.refreshComposite();

        // As the canvas runs it (CanvasItemWet.cpp): 4 ms of stepping per stroke event, then a
        // 50 ms timer with 8 ms of stepping per tick; what does not fit is not caught up, so the
        // paint dries more slowly instead of stalling the UI.
        WetLayer budgeted(size);
        budgeted.setStepBudgetMs(4.0);
        TileSnapshot before2;
        StrokeEngine engine2(s, QColor(40, 90, 160), 7, 0);
        engine2.setWetLayer(&budgeted);
        std::vector<double> ev2;
        for (size_t i = 0; i < samples.size(); ++i) {
            QElapsedTimer t;
            t.start();
            const QRect changed = i == 0 ? engine2.begin(doc.pixels(active), samples[i], before2)
                                         : engine2.moveTo(doc.pixels(active), samples[i], before2);
            doc.refreshComposite(changed);
            ev2.push_back(t.nsecsElapsed() / 1e6);
        }
        doc.refreshComposite(engine2.finish(doc.pixels(active), before2));
        std::vector<double> ticks2;
        double owed = 0, simulated = 0, stepMs = 0, renderMs = 0, compositeMs = 0;
        int nTicks = 0;
        while (budgeted.isWet() && nTicks < 20000) {
            QElapsedTimer t;
            t.start();
            owed += 50.0;
            const int stepsOwed = int(owed / WetLayer::StepMs);
            owed -= stepsOwed * WetLayer::StepMs;
            int done = 0;
            while (done < stepsOwed) {
                budgeted.advance(1);
                ++done;
                if (t.nsecsElapsed() / 1e6 >= 8.0)
                    break;
            }
            simulated += done * WetLayer::StepMs;
            const double a = t.nsecsElapsed() / 1e6;
            const QRect written = budgeted.render(surface, before2);
            const double r = t.nsecsElapsed() / 1e6;
            doc.refreshComposite(written);
            const double c = t.nsecsElapsed() / 1e6;
            stepMs += a;
            renderMs += r - a;
            compositeMs += c - r;
            ticks2.push_back(t.nsecsElapsed() / 1e6);
            ++nTicks;
        }
        const Stats e2 = summarize(ev2), k2 = summarize(ticks2);
        std::printf("With the canvas budgets: stroke event median %.3f / p95 %.3f / max %.3f ms; %d ticks: median %.3f / p95 "
                    "%.3f / max %.3f ms; dried after %.1f s on the timer for %.1f s of simulated drying.\n",
                    e2.median, e2.p95, e2.max, nTicks, k2.median, k2.p95, k2.max, nTicks * 0.050, simulated / 1000.0);
        std::printf("Per tick on average: stepping %.2f ms, writing the wet tiles' pixels %.2f ms, composite refresh %.2f ms.\n",
                    stepMs / std::max(1, nTicks), renderMs / std::max(1, nTicks), compositeMs / std::max(1, nTicks));
        before2.swapWith(doc.pixels(active));
        doc.refreshComposite();
    }
#endif
    // Save and reopen.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bench.drawapp"));
    const DocumentData snapshot = doc.snapshot();
    std::vector<double> save, open;
    for (int i = 0; i < 3; ++i) {
        QElapsedTimer t;
        t.start();
        ProjectFile::save(snapshot, path);
        save.push_back(t.nsecsElapsed() / 1e6);
        t.restart();
        ProjectFile::load(path);
        open.push_back(t.nsecsElapsed() / 1e6);
    }
    std::printf("\nSave median %.0f ms, open median %.0f ms, file %.1f MiB. Peak private %.0f MiB; MemoryBudget peak %.0f MiB.\n",
                summarize(save).median, summarize(open).median, QFileInfo(path).size() / MiB, privateMiB(true),
                MemoryBudget::peakTotal() / MiB);
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const QString section = argc > 1 ? QString::fromLatin1(argv[1]) : QStringLiteral("doc2048");
    const int events = argc > 2 ? std::max(20, std::atoi(argv[2])) : 300;
#ifdef DRAWAPP_M11
    const char *build = "milestone 11";
#else
    const char *build = "milestone 10 baseline";
#endif
    std::printf("bench_m11 (%s), %s, %s; %d events per stroke; Release build (MinGW, -O2)\n", build,
                qPrintable(QSysInfo::prettyProductName()), qPrintable(QSysInfo::currentCpuArchitecture()), events);
    if (section == QLatin1String("doc2048"))
        documentBench("2048 × 1536 × 10", QSize(2048, 1536), 10, false, events);
    else if (section == QLatin1String("doc4096"))
        documentBench("4096² × 8", QSize(4096, 4096), 8, false, events);
    else if (section == QLatin1String("sparse8192"))
        documentBench("sparse 8192²", QSize(8192, 8192), 2, true, events);
    else if (section == QLatin1String("sparse16384"))
        documentBench("sparse 16384²", QSize(16384, 16384), 2, true, events);
    return 0;
}

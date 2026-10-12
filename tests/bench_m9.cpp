// Milestone 9 benchmarks: the required document workloads, every brush engine, long strokes,
// multi-layer transforms, undo/redo and save/load. Prints Markdown tables.
//
// All times are CPU processing time on the calling thread (QElapsedTimer around the call), not
// input-to-display latency or frame time. Memory is the process's private (committed) bytes.
//
// Usage: bench_m9 [repetitions] [section]
//   sections: doc512, doc2048, doc4096, doc8192, brushes, long, transforms (default: all)
// Run each document section in its own process for a meaningful peak-memory figure.

#include "BrushPresets.h"
#include "BrushProperties.h"
#include "LayeredDocument.h"
#include "MemoryBudget.h"
#include "ProjectFile.h"
#include "RecoveryFile.h"
#include "RasterDocument.h"
#include "Selection.h"
#include "StrokeEngine.h"
#include "TileSnapshot.h"
#include "TransformApply.h"
#include "TransformSession.h"
#include "UndoHistory.h"
#include "ViewPyramid.h"
#include "ViewRenderer.h"
#include "ViewTransform.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QPainter>
#include <QSysInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {

constexpr double MiB = 1024.0 * 1024.0;

struct Memory { double privateMiB = -1, peakPrivateMiB = -1, workingSetMiB = -1, peakWorkingSetMiB = -1; };

Memory memory()
{
    Memory m;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX c{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&c), sizeof(c))) {
        m.privateMiB = c.PrivateUsage / MiB;
        m.peakPrivateMiB = c.PeakPagefileUsage / MiB;
        m.workingSetMiB = c.WorkingSetSize / MiB;
        m.peakWorkingSetMiB = c.PeakWorkingSetSize / MiB;
    }
#endif
    return m;
}

struct Stats { double medianMs = 0, p95Ms = 0, maxMs = 0, totalMs = 0; int count = 0; };

Stats summarize(std::vector<double> ms)
{
    Stats s;
    if (ms.empty())
        return s;
    for (double v : ms)
        s.totalMs += v;
    std::sort(ms.begin(), ms.end());
    s.medianMs = ms[ms.size() / 2];
    s.p95Ms = ms[std::min(ms.size() - 1, size_t(ms.size() * 0.95))];
    s.maxMs = ms.back();
    s.count = int(ms.size());
    return s;
}

template <typename F>
Stats measure(int reps, F &&f)
{
    std::vector<double> ms;
    for (int i = 0; i < reps; ++i) {
        QElapsedTimer t;
        t.start();
        f();
        ms.push_back(t.nsecsElapsed() / 1e6);
    }
    return summarize(ms);
}

void row(const char *label, const Stats &s, int decimals = 2)
{
    std::printf("| %s | %.*f | %.*f | %.*f |\n", label, decimals, s.medianMs, decimals, s.p95Ms, decimals, s.maxMs);
}

// Representative artwork: a white background and layers of brush dabs, density scaled to the area.
LayeredDocument makeDocument(QSize size, int layers)
{
    LayeredDocument doc(size);
    doc.pixels(0).fillRect(doc.pixels(0).rect(), 0xffffffff);
    doc.layerRef(0).name = QStringLiteral("Background");
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

// A mostly empty large canvas: a few layers with a handful of long strokes each (sketching on a
// big sheet), the case sparse storage is for.
LayeredDocument makeSparseDocument(QSize size, int layers)
{
    LayeredDocument doc(size);
    doc.layerRef(0).name = QStringLiteral("Sketch 1");
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

// An input trace across the document: a wavy line with simulated pressure, 4 ms per event.
std::vector<StrokeSample> trace(QSize size, int events, double pressureFloor = 0.3)
{
    std::vector<StrokeSample> v;
    for (int i = 0; i < events; ++i) {
        const double t = double(i) / std::max(1, events - 1);
        StrokeSample s;
        s.pos = QPointF(size.width() * (0.05 + 0.9 * t), size.height() * (0.5 + 0.3 * std::sin(t * 12.0)));
        s.pressure = pressureFloor + (1.0 - pressureFloor) * (0.5 + 0.5 * std::sin(t * 7.0));
        s.timeMs = i * 4.0;
        v.push_back(s);
    }
    return v;
}

struct StrokeResult
{
    Stats perEvent;
    qint64 dabs = 0;
    qsizetype engineBytes = 0;
    qsizetype undoBytes = 0;
    Stats undo, redo;
};

// A stroke exactly as CanvasItem runs it: engine call + composite refresh per input event, then
// the undo entry; undo and redo are timed too.
StrokeResult runStroke(LayeredDocument &doc, const BrushSettings &settings, const std::vector<StrokeSample> &samples,
                       int undoReps = 5)
{
    StrokeResult r;
    const int active = doc.activeIndex();
    StrokeEngine engine(settings, QColor(40, 90, 160), 7, StrokeEngine::smoothingRadius(settings));
    TileSnapshot before;
    std::vector<double> events;
    for (size_t i = 0; i < samples.size(); ++i) {
        QElapsedTimer t;
        t.start();
        const QRect changed = i == 0 ? engine.begin(doc.pixels(active), samples[i], before)
                                     : engine.moveTo(doc.pixels(active), samples[i], before);
        doc.refreshComposite(changed);
        events.push_back(t.nsecsElapsed() / 1e6);
    }
    {
        QElapsedTimer t;
        t.start();
        doc.refreshComposite(engine.finish(doc.pixels(active), before));
        events.push_back(t.nsecsElapsed() / 1e6);
    }
    r.perEvent = summarize(events);
    r.dabs = engine.dabCount();
    r.engineBytes = engine.bufferBytes();
    before.dropUnchanged(doc.layer(active).image);
    r.undoBytes = before.byteSize();
    UndoHistory history;
    history.push(UndoCommand::pixels(doc.layer(active).id, std::move(before), QStringLiteral("stroke")));
    std::vector<double> u, rd;
    for (int i = 0; i < undoReps; ++i) {
        QElapsedTimer t;
        t.start();
        history.undo(doc);
        u.push_back(t.nsecsElapsed() / 1e6);
        t.restart();
        history.redo(doc);
        rd.push_back(t.nsecsElapsed() / 1e6);
    }
    history.undo(doc); // leave the document as it was
    r.undo = summarize(u);
    r.redo = summarize(rd);
    return r;
}

void documentBench(QSize size, int wantedLayers, int reps, bool sparse = false)
{
    const int maxLayers = LayeredDocument::maxLayersFor(size);
    const int layers = std::min(wantedLayers, maxLayers);
    std::printf("\n### %d x %d, %d layer(s)%s\n\n", size.width(), size.height(), wantedLayers,
                sparse ? ", sparse (a few long strokes per layer)" : "");
    if (layers < wantedLayers)
        std::printf("Refused by design: at most %d layers. Measured with %d layers instead.\n\n", maxLayers, layers);
    const Memory start = memory();
    QElapsedTimer t;
    t.start();
    LayeredDocument doc = sparse ? makeSparseDocument(size, layers) : makeDocument(size, layers);
    const double createMs = t.nsecsElapsed() / 1e6;
    const Memory loaded = memory();
    const double layerMiB = double(size.width()) * size.height() * 4 / MiB;
    qint64 layerBytes = 0;
    for (const LayerData &l : doc.layers())
        layerBytes += l.image.allocatedBytes();

    std::printf("Dense equivalent: %.0f MiB of layers + %.0f MiB composite. Allocated tiles: layers %.1f MiB, composite "
                "%.1f MiB. Private memory added by the document: %.0f MiB (process %.0f MiB, working set %.0f MiB).\n\n",
                layers * layerMiB, layerMiB, layerBytes / MiB, doc.composite().allocatedBytes() / MiB,
                loaded.privateMiB - start.privateMiB, loaded.privateMiB, loaded.workingSetMiB);
    std::printf("| Operation | median ms | p95 ms | max ms |\n|---|---|---|---|\n");
    std::printf("| create the test document (setup, not an app operation) | %.0f | - | - |\n", createMs);
    row("full composite rebuild", measure(std::max(3, reps / 3), [&] { doc.refreshComposite(); }));
    ViewTransform view;
    view.center(QSizeF(size), QSizeF(1920, 1080), std::min(1.0, std::min(1920.0 / size.width(), 1080.0 / size.height())));
    QImage target(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    row("render the view without the pyramid (fit; copies the whole visible area each frame; not the canvas's path)", measure(reps, [&] {
        QPainter p(&target);
        ViewRenderer::render(p, doc.composite(), view, QRect(0, 0, 1920, 1080));
    }), 3);
    QImage view100(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    ViewTransform actual;
    actual.center(QSizeF(size), QSizeF(1920, 1080), 1.0);
    row("render the view without the pyramid (100 %)", measure(reps, [&] {
        QPainter p(&view100);
        ViewRenderer::render(p, doc.composite(), actual, QRect(0, 0, 1920, 1080));
    }), 3);
    {
        // As the canvas draws (milestone 10): through the view pyramid. The first frame builds the
        // level; later frames reuse it.
        ViewPyramid pyramid;
        QElapsedTimer first;
        first.start();
        {
            QPainter p(&target);
            ViewRenderer::render(p, doc.composite(), pyramid, view, QRect(0, 0, 1920, 1080));
        }
        const double firstMs = first.nsecsElapsed() / 1e6;
        std::printf("| render the view as the canvas does (fit, level %d of the pyramid), first frame (builds the level) | %.1f | - | - |\n",
                    ViewPyramid::levelFor(view.zoom(), size), firstMs);
        row("render the view as the canvas does (fit), later frames", measure(reps, [&] {
            QPainter p(&target);
            ViewRenderer::render(p, doc.composite(), pyramid, view, QRect(0, 0, 1920, 1080));
        }), 3);
        ViewPyramid pyramid100;
        {
            QPainter p(&view100);
            ViewRenderer::render(p, doc.composite(), pyramid100, actual, QRect(0, 0, 1920, 1080));
        }
        row("render the view as the canvas does (100 %), later frames", measure(reps, [&] {
            QPainter p(&view100);
            ViewRenderer::render(p, doc.composite(), pyramid100, actual, QRect(0, 0, 1920, 1080));
        }), 3);
        ViewTransform panning = view;
        row("render the view as the canvas does (fit), while panning 7 px per frame", measure(reps, [&] {
            panning.panBy(QPointF(7, 3));
            QPainter p(&target);
            ViewRenderer::render(p, doc.composite(), pyramid, panning, QRect(0, 0, 1920, 1080));
        }), 3);
        ViewTransform smallest;
        smallest.center(QSizeF(size), QSizeF(1920, 1080), ViewTransform::MinZoom);
        row("render the view as the canvas does (5 %)", measure(reps, [&] {
            QPainter p(&target);
            ViewRenderer::render(p, doc.composite(), pyramid, smallest, QRect(0, 0, 1920, 1080));
        }), 3);
        std::printf("| (view pyramid memory after these frames: %.1f MiB) | - | - | - |\n", pyramid.byteSize() / MiB);
    }

    BrushSettings solid = BrushPresets::builtIn(QStringLiteral("builtin.solid-round"))->settings;
    solid.size = 24;
    const StrokeResult s = runStroke(doc, solid, trace(size, 1000));
    std::printf("| stroke input event, Solid Round 24 px (engine + undo capture + composite refresh), %lld dabs | %.4f | %.4f | %.4f |\n",
                s.dabs, s.perEvent.medianMs, s.perEvent.p95Ms, s.perEvent.maxMs);
    std::printf("| undo that stroke (%.1f MiB of tiles) | %.2f | %.2f | %.2f |\n", s.undoBytes / MiB, s.undo.medianMs,
                s.undo.p95Ms, s.undo.maxMs);
    std::printf("| redo that stroke | %.2f | %.2f | %.2f |\n", s.redo.medianMs, s.redo.p95Ms, s.redo.maxMs);
    BrushSettings textured = BrushPresets::builtIn(QStringLiteral("builtin.textured-paint"))->settings;
    const StrokeResult tx = runStroke(doc, textured, trace(size, 1000));
    std::printf("| stroke input event, Textured Paint %.0f px, %lld dabs | %.4f | %.4f | %.4f |\n", textured.size, tx.dabs,
                tx.perEvent.medianMs, tx.perEvent.p95Ms, tx.perEvent.maxMs);

    QTemporaryDir dir;
    const QString path = dir.filePath("bench.drawapp");
    const int ioReps = std::max(2, reps / 10);
    const DocumentData snapshot = doc.snapshot();
    row("save project (encode + atomic write)", measure(ioReps, [&] { ProjectFile::save(snapshot, path); }), 1);
    row("open project (read + verify + decode)", measure(ioReps, [&] { ProjectFile::load(path); }), 1);
    {
        // A recovery snapshot as the canvas writes it: copy on the UI thread, encode on a worker.
        RecoveryFile::Metadata meta;
        const Stats snap = measure(std::max(3, reps / 3), [&] { DocumentData d = doc.snapshot(); });
        row("recovery: snapshot on the UI thread", snap, 4);
        row("recovery: encode (worker)", measure(ioReps, [&] { RecoveryFile::encode(snapshot, meta); }), 1);
    }
    const Memory end = memory();
    std::printf("\nFile %.1f MiB. Process peak private memory %.0f MiB, peak working set %.0f MiB. MemoryBudget peak %.0f MiB.\n",
                QFileInfo(path).size() / MiB, end.peakPrivateMiB, end.peakWorkingSetMiB, MemoryBudget::peakTotal() / MiB);
}

struct EngineCase
{
    const char *label;
    std::function<void(BrushSettings &)> setup;
};

std::vector<EngineCase> engineCases()
{
    auto preset = [](const char *id) {
        return [id](BrushSettings &s) { s = BrushPresets::builtIn(QLatin1String(id))->settings; };
    };
    auto dyn = [](BrushSettings &s, DynamicTarget t, Sensor sensor) {
        ParameterDynamics d;
        d.enabled = true;
        d.minimum = 0.2;
        d.mappings.push_back({sensor, SensorCurve::exponential(1.5), 0});
        s.dynamicsFor(t) = d;
    };
    return {
        {"Solid Round 12 px (milestone 6 default)", [](BrushSettings &s) { s = BrushSettings(); }},
        {"Solid Round 128 px", [](BrushSettings &s) { s = BrushSettings(); s.size = 128; }},
        {"Clean Ink (preset)", preset("builtin.clean-ink")},
        {"Sketch Pencil (preset, textured)", preset("builtin.sketch-pencil")},
        {"Textured Paint 40 px (stroke texture)", preset("builtin.textured-paint")},
        {"Textured Paint 40 px, texture each dab + rotated grain",
         [=](BrushSettings &s) { preset("builtin.textured-paint")(s); s.textureEachDab = true; s.textureRotation = 30; }},
        {"Dense textured 120 px, build-up, each dab",
         [=](BrushSettings &s) { preset("builtin.textured-paint")(s); s.size = 120; s.textureEachDab = true; s.spacing = 0.05; }},
        {"Soft Airbrush 90 px (preset)", preset("builtin.soft-airbrush")},
        {"Smudge 36 px (preset)", preset("builtin.blend-smudge")},
        {"Smudge 36 px, dulling + colour rate",
         [=](BrushSettings &s) { preset("builtin.blend-smudge")(s); s.smudgeMode = SmudgeMode::Dulling; s.smudgeColorRate = 0.3; }},
        {"Wet paint 40 px", [](BrushSettings &s) { s = BrushSettings(); s.blend = BrushBlend::WetPaint; s.size = 40; }},
        {"Wet paint 120 px", [](BrushSettings &s) { s = BrushSettings(); s.blend = BrushBlend::WetPaint; s.size = 120; }},
        {"Spray 80 px, 60 particles",
         [](BrushSettings &s) { s = BrushSettings(); s.stamp = BrushStamp::Spray; s.size = 80; s.sprayDensity = 60; s.spacing = 0.3; }},
        {"Hatching 80 px", [](BrushSettings &s) { s = BrushSettings(); s.stamp = BrushStamp::Hatching; s.size = 80; }},
        {"Colour dynamics 30 px (hue jitter: per-pixel colour)", [](BrushSettings &s) { s = BrushSettings(); s.size = 30; s.hueJitter = 40; }},
        {"Dynamics-heavy 30 px (size, opacity, rotation, scatter; 4 sensors)",
         [=](BrushSettings &s) {
             s = BrushSettings();
             s.size = 30;
             s.roundness = 0.5;
             s.scatter = 0.3;
             dyn(s, DynamicTarget::Size, Sensor::Pressure);
             dyn(s, DynamicTarget::Opacity, Sensor::Speed);
             dyn(s, DynamicTarget::Rotation, Sensor::Direction);
             dyn(s, DynamicTarget::Scatter, Sensor::Random);
         }},
        {"Count 4 + scatter 30 px", [](BrushSettings &s) { s = BrushSettings(); s.size = 30; s.count = 4; s.scatter = 0.6; }},
        {"End taper 200 px, 24 px", [](BrushSettings &s) { s = BrushSettings(); s.size = 24; s.taperEnd = 200; s.taperStart = 100; }},
    };
}

void brushBench(int reps)
{
    const QSize size(2048, 1536);
    LayeredDocument doc = makeDocument(size, 10);
    std::printf("\n### Brush engines: one input event of a 1000-event stroke (2048 x 1536, 10 layers)\n\n"
                "Engine + undo capture + composite refresh per input event (4 ms apart, simulated pressure). "
                "Dab throughput = dabs / total processing time of the stroke.\n\n"
                "| Brush | median ms | p95 ms | max ms | dabs | dabs per s | engine buffers KiB | undo snapshot MiB |\n"
                "|---|---|---|---|---|---|---|---|\n");
    const std::vector<StrokeSample> samples = trace(size, 1000);
    for (const EngineCase &c : engineCases()) {
        BrushSettings s;
        c.setup(s);
        std::vector<double> medians, p95s, maxes;
        StrokeResult last;
        double total = 0;
        const int runs = std::max(1, reps / 10);
        for (int r = 0; r < runs; ++r) {
            last = runStroke(doc, s, samples, 1);
            medians.push_back(last.perEvent.medianMs);
            p95s.push_back(last.perEvent.p95Ms);
            maxes.push_back(last.perEvent.maxMs);
            total += last.perEvent.totalMs;
        }
        std::sort(medians.begin(), medians.end());
        std::sort(p95s.begin(), p95s.end());
        std::sort(maxes.begin(), maxes.end());
        const double dabsPerS = last.dabs / (total / runs / 1000.0);
        std::printf("| %s | %.4f | %.4f | %.3f | %lld | %.0f | %.0f | %.1f |\n", c.label, medians[medians.size() / 2],
                    p95s[p95s.size() / 2], maxes[maxes.size() / 2], last.dabs, dabsPerS, last.engineBytes / 1024.0,
                    last.undoBytes / MiB);
    }
}

void longStrokeBench()
{
    const QSize size(2048, 1536);
    LayeredDocument doc = makeDocument(size, 10);
    std::printf("\n### Long strokes: 10 000 input events (2048 x 1536, 10 layers)\n\n"
                "| Brush | total ms | per event median ms | p95 ms | max ms | dabs | engine buffers MiB | undo snapshot MiB | process private MiB after |\n"
                "|---|---|---|---|---|---|---|---|---|\n");
    // A spiral that covers much of the canvas and crosses itself.
    std::vector<StrokeSample> samples;
    for (int i = 0; i < 10000; ++i) {
        const double t = i / 9999.0, a = t * 40 * M_PI, r = 80 + 600 * t;
        StrokeSample s;
        s.pos = QPointF(1024 + r * std::cos(a), 768 + 0.7 * r * std::sin(a));
        s.pressure = 0.4 + 0.6 * std::abs(std::sin(t * 50));
        s.timeMs = i * 4.0;
        samples.push_back(s);
    }
    for (const EngineCase &c : {EngineCase{"Solid Round 24 px", [](BrushSettings &s) { s = BrushSettings(); s.size = 24; }},
                                EngineCase{"Textured Paint 40 px", [](BrushSettings &s) {
                                               s = BrushPresets::builtIn(QStringLiteral("builtin.textured-paint"))->settings;
                                           }},
                                EngineCase{"Smudge 36 px", [](BrushSettings &s) {
                                               s = BrushPresets::builtIn(QStringLiteral("builtin.blend-smudge"))->settings;
                                           }},
                                EngineCase{"Wet paint 40 px", [](BrushSettings &s) {
                                               s = BrushSettings();
                                               s.blend = BrushBlend::WetPaint;
                                               s.size = 40;
                                           }}}) {
        BrushSettings s;
        c.setup(s);
        const StrokeResult r = runStroke(doc, s, samples, 1);
        std::printf("| %s | %.0f | %.4f | %.4f | %.2f | %lld | %.1f | %.1f | %.0f |\n", c.label, r.perEvent.totalMs,
                    r.perEvent.medianMs, r.perEvent.p95Ms, r.perEvent.maxMs, r.dabs, r.engineBytes / MiB, r.undoBytes / MiB,
                    memory().privateMiB);
    }
}

void transformBench(QSize size, int wantedLayers, int reps)
{
    const int layers = std::min(wantedLayers, LayeredDocument::maxLayersFor(size));
    LayeredDocument doc = makeDocument(size, layers);
    std::vector<TiledImage> images;
    std::vector<quint64> ids;
    for (int i = 1; i < doc.count(); ++i) { // every layer except the background, as when all are selected
        images.push_back(doc.layer(i).image);
        ids.push_back(doc.layer(i).id);
    }
    std::printf("\n### Multi-layer transform: %d layers of %d x %d (all but the background), no selection\n\n"
                "| Operation | median ms | p95 ms | max ms |\n|---|---|---|---|\n",
                int(images.size()), size.width(), size.height());
    const Memory before = memory();
    TransformSession t;
    bool begun = true;
    const Stats begin = measure(std::max(2, reps / 10), [&] { begun = t.begin(images, Selection()); });
    if (!begun) {
        std::printf("| begin: **refused** (%s) | - | - | - |\n", qPrintable(MemoryBudget::shortageText()));
        return;
    }
    row("begin (cache each layer's source and reductions)", begin, 1);
    const qsizetype sessionBytes = t.byteSize();
    t.reset();
    t.setSize(t.sourceRect().width() * 1.2, t.sourceRect().height() * 1.2);
    t.setRotation(15);
    const QRect area = t.changedRect();
    // A preview frame as CanvasItem draws it: every part rendered (fast) into its preview layer,
    // then the preview composite over the changed area.
    std::vector<LayerData> preview = doc.layers();
    TiledImage composite = doc.composite();
    const Stats frame = measure(std::max(3, reps / 3), [&] {
        for (int p = 0; p < t.partCount(); ++p)
            t.render(p, preview[size_t(p) + 1].image, area, TransformSession::Quality::Fast);
        compositeLayers(composite, area, preview);
    });
    row("preview frame, per layer: scale 1.2 + rotate 15 (fast) of every layer + composite", frame, 1);
    {
        // The milestone 9 preview of adjacent layers without a selection: one merged copy.
        QElapsedTimer mt;
        mt.start();
        TiledImage flat(size, TiledImage::DefaultTileSize, MemoryBudget::Category::Preview);
        std::vector<LayerData> stack(doc.layers().begin() + 1, doc.layers().end());
        const bool merged = compositeLayers(flat, flat.rect(), stack);
        std::vector<TiledImage> withMerged = images;
        withMerged.push_back(flat);
        TransformSession m;
        if (!merged || !m.begin(withMerged, Selection())) {
            std::printf("| merged copy: **refused** (%s); the app then previews each layer | - | - | - |\n",
                        qPrintable(MemoryBudget::shortageText()));
        } else {
            const double setupMs = mt.nsecsElapsed() / 1e6;
            m.setSize(m.sourceRect().width() * 1.2, m.sourceRect().height() * 1.2);
            m.setRotation(15);
            std::vector<LayerData> mergedStack = doc.layers();
            TiledImage mergedPreview = flat;
            const int mergedPart = m.partCount() - 1;
            const Stats mergedFrame = measure(std::max(3, reps / 3), [&] {
                m.render(mergedPart, mergedPreview, area, TransformSession::Quality::Fast);
                mergedStack[1].image = mergedPreview;
                mergedStack[1].opacity = 1.0;
                for (size_t i = 2; i < mergedStack.size(); ++i)
                    mergedStack[i].visible = false;
                compositeLayers(composite, area, mergedStack);
            });
            row("preview frame, merged copy (milestone 9): fast render of one merged layer + composite", mergedFrame, 1);
            const Stats mergedFinal = measure(std::max(3, reps / 3), [&] {
                m.render(mergedPart, mergedPreview, area, TransformSession::Quality::Final);
                compositeLayers(composite, area, mergedStack);
            });
            row("preview after release, merged copy (bicubic)", mergedFinal, 1);
            std::printf("| begin with the merged copy (merge + cache, once per transform) | %.1f | - | - |\n", setupMs);
        }
    }
    // Apply as CanvasItem does (milestone 10): TransformApply prepares each layer's result as a copy
    // of the layer with the changed tiles rewritten (on a worker thread in the app; timed here on
    // this thread), then the commit exchanges tile references.
    std::vector<TiledImage> results;
    bool prepared = true;
    const Stats prepare = measure(std::max(2, reps / 10), [&] {
        TransformApply::Input in;
        in.session = t.workerCopy();
        in.layerIds = ids;
        in.layers = images;
        for (const TiledImage &img : images)
            in.versions.push_back(img.version());
        in.area = area;
        TransformApply::Result r = TransformApply::run(std::move(in));
        prepared = r.ok;
        results = std::move(r.layers);
    });
    if (!prepared) {
        std::printf("| apply: **refused** (%s); nothing changed | - | - | - |\n", qPrintable(MemoryBudget::shortageText()));
        return;
    }
    row("apply: prepare all layers (bicubic; on the worker thread in the app)", prepare, 1);
    UndoHistory history;
    QElapsedTimer timer;
    timer.start();
    std::vector<UndoCommand> commands;
    for (int p = 0; p < t.partCount(); ++p) {
        const int index = doc.indexOf(ids[size_t(p)]);
        TileSnapshot snap;
        snap.capture(doc.layer(index).image, area);
        doc.pixels(index) = results[size_t(p)];
        snap.dropUnchanged(doc.layer(index).image);
        commands.push_back(UndoCommand::pixels(ids[size_t(p)], std::move(snap), QStringLiteral("Transform")));
    }
    history.push(UndoCommand::group(std::move(commands), QStringLiteral("Transform")));
    doc.refreshComposite(area);
    const double commitMs = timer.nsecsElapsed() / 1e6;
    std::printf("| apply: commit (exchange tile references + one group undo entry, %.0f MiB kept by history) + composite | %.1f | - | - |\n",
                history.bytesUsed() / MiB, commitMs);
    std::vector<double> u, r;
    for (int i = 0; i < std::max(2, reps / 10); ++i) {
        timer.restart();
        history.undo(doc);
        doc.refreshComposite(area);
        u.push_back(timer.nsecsElapsed() / 1e6);
        timer.restart();
        history.redo(doc);
        doc.refreshComposite(area);
        r.push_back(timer.nsecsElapsed() / 1e6);
    }
    row("undo the transform (all layers) + composite", summarize(u), 1);
    row("redo the transform (all layers) + composite", summarize(r), 1);
    qint64 previewBytes = 0;
    for (size_t i = 1; i < preview.size(); ++i)
        for (int r = preview[i].image.rowOf(area.top()); r <= preview[i].image.rowOf(area.bottom()); ++r)
            for (int c = preview[i].image.columnOf(area.left()); c <= preview[i].image.columnOf(area.right()); ++c)
                if (preview[i].image.hasTile(c, r) && !preview[i].image.sharesTileWith(doc.layer(int(i)).image, c, r))
                    previewBytes += preview[i].image.tileBytes();
    std::printf("\nTransform session memory %.0f MiB (sources and reductions); preview layers %.0f MiB of their own tiles; "
                "process private memory grew by %.0f MiB during the transform; peak private %.0f MiB.\n",
                sessionBytes / MiB, previewBytes / MiB, memory().privateMiB - before.privateMiB, memory().peakPrivateMiB);
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // nothing lost if a section fails
    const int reps = argc > 1 ? std::max(3, std::atoi(argv[1])) : 30;
    const QString section = argc > 2 ? QString::fromLatin1(argv[2]) : QStringLiteral("all");
    auto want = [&](const char *name) { return section == QLatin1String("all") || section == QLatin1String(name); };
    std::printf("%s, %s; %d repetitions; Release build (MinGW, -O2)\n", qPrintable(QSysInfo::prettyProductName()),
                qPrintable(QSysInfo::currentCpuArchitecture()), reps);
    if (want("doc512"))
        documentBench(QSize(512, 512), 3, reps);
    if (want("doc512x1"))
        documentBench(QSize(512, 512), 1, reps);
    if (want("doc2048"))
        documentBench(QSize(2048, 1536), 10, reps);
    if (want("doc4096x8"))
        documentBench(QSize(4096, 4096), 8, reps); // the milestone 9 limit, for comparison
    if (want("doc4096"))
        documentBench(QSize(4096, 4096), 20, reps);
    if (want("doc8192"))
        documentBench(QSize(8192, 8192), 2, reps); // milestone 9's largest
    if (want("doc8192x10"))
        documentBench(QSize(8192, 8192), 10, reps);
    if (want("sparse16384"))
        documentBench(QSize(16384, 16384), 4, reps, true);
    if (want("brushes"))
        brushBench(reps);
    if (want("long"))
        longStrokeBench();
    if (want("transforms")) {
        transformBench(QSize(2048, 1536), 10, reps);
        transformBench(QSize(4096, 4096), 8, reps); // 7 layers moved, as in milestone 9
    }
    if (want("transforms20"))
        transformBench(QSize(4096, 4096), 20, reps);
    return 0;
}

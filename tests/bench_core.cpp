// Times the CPU paths the app uses, for documents with 1, 5 and 10 layers. Prints Markdown tables.
// These are CPU processing times on the calling thread, not input-to-display latency.
// Usage: bench_core [repetitions] [presets]   ("presets": only the brush preset tables)

#include "BrushPresets.h"
#include "LayeredDocument.h"
#include "ProjectFile.h"
#include "RasterDocument.h"
#include "RecoveryFile.h"
#include "SafeSave.h"
#include "StrokeEngine.h"
#include "StrokeSmoother.h"
#include "StrokeSpacer.h"
#include "TileSnapshot.h"
#include "UndoHistory.h"
#include "ViewRenderer.h"
#include "ViewTransform.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>
#include <QSysInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {

// Private (committed) memory of this process in MiB, or -1 where not implemented.
double privateMiB()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)))
        return counters.PrivateUsage / (1024.0 * 1024.0);
#endif
    return -1;
}

struct Stats { double medianMs, p95Ms, maxMs; };

Stats summarize(std::vector<double> ms)
{
    std::sort(ms.begin(), ms.end());
    return {ms[ms.size() / 2], ms[std::min(ms.size() - 1, size_t(ms.size() * 0.95))], ms.back()};
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

std::vector<StrokeSample> zigZag()
{
    std::vector<StrokeSample> samples;
    for (int i = 0; i < 1000; ++i) {
        const double t = i / 999.0;
        samples.push_back({QPointF(100 + 1800 * t, 768 + 500 * std::sin(t * 12.0)), 0.5 + 0.5 * std::sin(t * 7.0)});
    }
    return samples;
}

// A representative illustration: an opaque white background plus layers with a few thousand
// brush dabs each (some at reduced opacity, one hidden when there are five or more).
LayeredDocument makeDocument(int layers)
{
    LayeredDocument doc;
    doc.pixels(0).fill(Qt::white);
    doc.layerRef(0).name = QStringLiteral("Background");
    for (int n = 1; n < layers; ++n) {
        LayerData layer;
        layer.id = doc.allocateId();
        layer.name = QStringLiteral("Layer %1").arg(n + 1);
        layer.image = RasterDocument::blank(doc.size());
        layer.opacity = n % 3 == 0 ? 0.7 : 1.0;
        layer.visible = n != 4;
        std::vector<Dab> dabs;
        for (int i = 0; i < 2500; ++i)
            dabs.push_back({QPointF(80 + (i * 37 + n * 101) % 1880, 80 + (i * 53 + n * 67) % 1370), qreal(4 + (i + n) % 30)});
        RasterDocument::stampDabs(layer.image, dabs, QColor::fromHsv((n * 47) % 360, 160, 200), BrushMode::Paint);
        doc.insertLayer(doc.count(), layer);
    }
    doc.setActiveLayer(doc.layer(doc.count() / 2).id);
    doc.refreshComposite();
    return doc;
}

void layerBench(int layers, int reps)
{
    QTemporaryDir dir;
    const double memBefore = privateMiB();
    LayeredDocument doc = makeDocument(layers);
    const double memDocument = privateMiB() - memBefore;
    const qsizetype layerBytes = qsizetype(doc.size().width()) * doc.size().height() * 4;
    const int active = doc.activeIndex();

    // Stroke events exactly as CanvasItem runs them (100 % opacity, no smoothing), on the middle layer.
    const std::vector<StrokeSample> samples = zigZag();
    std::vector<double> events;
    for (int r = 0; r < std::max(1, reps / 10); ++r) {
        StrokeEngine engine(BrushSettings{}, Qt::black, 1, 0);
        TileSnapshot before;
        UndoHistory history;
        for (size_t i = 0; i < samples.size(); ++i) {
            QElapsedTimer t;
            t.start();
            const QRect changed = i == 0 ? engine.begin(doc.pixels(active), samples[i], before)
                                         : engine.moveTo(doc.pixels(active), samples[i], before);
            doc.refreshComposite(changed);
            events.push_back(t.nsecsElapsed() / 1e6);
        }
        doc.refreshComposite(engine.finish(doc.pixels(active), before));
        before.dropUnchanged(doc.layer(active).image);
        history.push(UndoCommand::pixels(doc.layer(active).id, std::move(before), QStringLiteral("stroke")));
        history.undo(doc);
    }
    const Stats stroke = summarize(events);

    const Stats fullComposite = measure(reps, [&] { doc.refreshComposite(); });

    // Recovery: snapshot on the UI thread, then the first edit of the shared layer pays for a copy.
    std::vector<double> snapshotMs, detachMs, normalMs;
    for (int r = 0; r < reps; ++r) {
        QElapsedTimer t;
        t.start();
        DocumentData snapshot = doc.snapshot();
        snapshotMs.push_back(t.nsecsElapsed() / 1e6);
        BrushSettings dot;
        dot.size = 10;
        TileSnapshot before;
        t.restart();
        StrokeEngine::render(doc.pixels(active), dot, Qt::red, {{QPointF(500 + r, 500), 1}}, 1, &before);
        detachMs.push_back(t.nsecsElapsed() / 1e6);
        t.restart();
        StrokeEngine::render(doc.pixels(active), dot, Qt::red, {{QPointF(520 + r, 500), 1}}, 1, &before);
        normalMs.push_back(t.nsecsElapsed() / 1e6);
    }
    const DocumentData snapshot = doc.snapshot();
    RecoveryFile::Metadata meta;
    meta.savedAt = QDateTime::currentDateTimeUtc();
    const QString recoveryPath = dir.filePath("bench.drawrecovery");
    const int ioReps = std::max(3, reps / 5);
    const Stats recoveryWrite = measure(ioReps, [&] {
        const QByteArray bytes = RecoveryFile::encode(snapshot, meta);
        QSaveFile f(recoveryPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(bytes);
            f.commit();
        }
    });
    const QString path = dir.filePath("bench.drawapp");
    const Stats save = measure(ioReps, [&] { ProjectFile::save(snapshot, path); });
    const QByteArray encoded = ProjectFile::encode(snapshot);
    const Stats verify = measure(ioReps, [&] { ProjectFile::verify(encoded); });
    const Stats safeSave = measure(ioReps, [&] { SafeSave::saveProject(snapshot, dir.filePath("safe.drawapp"), {}); });
    const Stats open = measure(ioReps, [&] { ProjectFile::load(path); });
    const QString png = dir.filePath("bench.png");
    const Stats exportPng = measure(ioReps, [&] { ProjectFile::exportPng(snapshot.composite(), png); });

    const double mib = 1024.0 * 1024.0;
    std::printf("\n### %d layer(s): layer pixels %.0f MiB + composite %.0f MiB; file %.2f MiB, recovery file %.2f MiB\n\n",
                layers, layers * layerBytes / mib, layerBytes / mib, QFileInfo(path).size() / mib,
                QFileInfo(recoveryPath).size() / mib);
    std::printf("Process private memory added by creating the document: %.0f MiB.\n\n", memDocument);
    std::printf("| Operation | median ms | p95 ms | max ms |\n|---|---|---|---|\n");
    std::printf("| stroke event (12 px brush, compositor): dabs + undo capture + composite refresh | %.4f | %.4f | %.4f |\n",
                stroke.medianMs, stroke.p95Ms, stroke.maxMs);
    std::printf("| full composite rebuild (visibility/opacity/order change) | %.2f | %.2f | %.2f |\n",
                fullComposite.medianMs, fullComposite.p95Ms, fullComposite.maxMs);
    const Stats snap = summarize(snapshotMs), det = summarize(detachMs), nor = summarize(normalMs);
    std::printf("| recovery snapshot on UI thread (shallow copy) | %.4f | %.4f | %.4f |\n", snap.medianMs, snap.p95Ms, snap.maxMs);
    std::printf("| first dab on the active layer after a snapshot (copies that layer) | %.3f | %.3f | %.3f |\n",
                det.medianMs, det.p95Ms, det.maxMs);
    std::printf("| same dab without a pending snapshot | %.4f | %.4f | %.4f |\n", nor.medianMs, nor.p95Ms, nor.maxMs);
    std::printf("| recovery encode + atomic write (worker thread) | %.1f | %.1f | %.1f |\n",
                recoveryWrite.medianMs, recoveryWrite.p95Ms, recoveryWrite.maxMs);
    std::printf("| save project (encode + atomic write) | %.1f | %.1f | %.1f |\n", save.medianMs, save.p95Ms, save.maxMs);
    std::printf("| verify encoded project (checksum + decompress each layer) | %.1f | %.1f | %.1f |\n", verify.medianMs,
                verify.p95Ms, verify.maxMs);
    std::printf("| app save to a local file (SafeSave: encode + verify + space check + atomic write) | %.1f | %.1f | %.1f |\n",
                safeSave.medianMs, safeSave.p95Ms, safeSave.maxMs);
    std::printf("| open project | %.1f | %.1f | %.1f |\n", open.medianMs, open.p95Ms, open.maxMs);
    std::printf("| export PNG (composite + encode) | %.1f | %.1f | %.1f |\n", exportPng.medianMs, exportPng.p95Ms,
                exportPng.maxMs);
}

// The same single-layer document and stroke the milestone 2 benchmark used (4000 dabs on a
// transparent canvas; 12 px zig-zag stroke with undo capture), for a like-for-like comparison.
void singleLayerComparison(int reps)
{
    QTemporaryDir dir;
    LayeredDocument doc;
    std::vector<Dab> dabs;
    for (int i = 0; i < 4000; ++i)
        dabs.push_back({QPointF(100 + (i * 37) % 1850, 100 + (i * 53) % 1340), qreal(6 + i % 40)});
    RasterDocument::stampDabs(doc.pixels(0), dabs, QColor(40, 90, 200), BrushMode::Paint);
    doc.refreshComposite();

    const std::vector<StrokeSample> samples = zigZag();
    std::vector<double> events;
    for (int r = 0; r < std::max(1, reps / 10); ++r) {
        StrokeSpacer spacer(12);
        TileSnapshot before;
        std::vector<Dab> pending;
        for (size_t i = 0; i < samples.size(); ++i) {
            QElapsedTimer t;
            t.start();
            if (i == 0)
                spacer.begin(samples[i], pending);
            else
                spacer.moveTo(samples[i], pending);
            doc.refreshComposite(RasterDocument::stampDabs(doc.pixels(0), pending, Qt::black, BrushMode::Paint, &before));
            pending.clear();
            events.push_back(t.nsecsElapsed() / 1e6);
        }
    }
    const Stats stroke = summarize(events);
    const DocumentData snapshot = doc.snapshot();
    const QString path = dir.filePath("single.drawapp");
    const int ioReps = std::max(3, reps / 3);
    const Stats save = measure(ioReps, [&] { ProjectFile::save(snapshot, path); });
    const Stats open = measure(ioReps, [&] { ProjectFile::load(path); });
    const Stats exportPng = measure(std::max(3, reps / 3), [&] {
        ProjectFile::exportPng(snapshot.composite(), dir.filePath("single.png"));
    });
    std::printf("\n### Like-for-like single layer (same content as the milestone 2 benchmark)\n\n");
    std::printf("| Operation | median ms | p95 ms | max ms |\n|---|---|---|---|\n");
    std::printf("| stroke event + undo capture (+ composite refresh), brush 12 px | %.4f | %.4f | %.4f |\n",
                stroke.medianMs, stroke.p95Ms, stroke.maxMs);
    std::printf("| save project, painted dabs (%.2f MiB file) | %.1f | - | %.1f |\n",
                QFileInfo(path).size() / (1024.0 * 1024.0), save.medianMs, save.maxMs);
    std::printf("| open project, painted dabs | %.1f | - | %.1f |\n", open.medianMs, open.maxMs);
    std::printf("| export PNG, painted dabs | %.1f | - | %.1f |\n", exportPng.medianMs, exportPng.maxMs);
}

// Per input event on one opaque 2048 × 1536 layer: the 1000-event zig-zag stroke with undo capture
// and composite refresh, through the milestone 4 path (QPainter dabs) and the milestone 5 path
// (smoother -> spacer -> uniform-opacity compositor).
void brushEngineBench(int reps)
{
    struct Variant
    {
        const char *label;
        bool legacy;
        qreal size, opacity, smoothing;
    };
    const Variant variants[] = {
        {"milestone 4 path (QPainter dabs), 12 px", true, 12, 1.0, 0},
        {"stroke engine, 12 px, 100 %, smoothing off", false, 12, 1.0, 0},
        {"stroke engine, 12 px, 50 %, smoothing off", false, 12, 0.5, 0},
        {"stroke engine, 12 px, 50 %, smoothing 20 px", false, 12, 0.5, 20},
        {"milestone 4 path (QPainter dabs), 128 px", true, 128, 1.0, 0},
        {"stroke engine, 128 px, 50 %, smoothing off", false, 128, 0.5, 0},
    };
    const std::vector<StrokeSample> samples = zigZag();
    std::printf("\n### Brush engine: one input event of a 1000-event stroke (stroke processing + undo capture + composite refresh)\n\n");
    std::printf("| Variant | median ms | p95 ms | max ms | coverage tiles |\n|---|---|---|---|---|\n");
    for (const Variant &v : variants) {
        LayeredDocument doc;
        doc.pixels(0).fill(Qt::white);
        doc.refreshComposite();
        std::vector<double> events;
        int maskTiles = 0;
        for (int r = 0; r < std::max(1, reps / 10); ++r) {
            BrushSettings settings;
            settings.size = v.size;
            settings.opacity = v.opacity;
            StrokeEngine engine(settings, Qt::black, 1, v.smoothing);
            StrokeSpacer spacer(v.size);
            TileSnapshot before;
            std::vector<Dab> pending;
            for (size_t i = 0; i < samples.size(); ++i) {
                QElapsedTimer t;
                t.start();
                QRect changed;
                if (v.legacy) {
                    if (i == 0)
                        spacer.begin(samples[i], pending);
                    else
                        spacer.moveTo(samples[i], pending);
                    changed = RasterDocument::stampDabs(doc.pixels(0), pending, Qt::black, BrushMode::Paint, &before);
                    pending.clear();
                } else {
                    changed = i == 0 ? engine.begin(doc.pixels(0), samples[i], before)
                                     : engine.moveTo(doc.pixels(0), samples[i], before);
                    if (i + 1 == samples.size())
                        changed |= engine.finish(doc.pixels(0), before);
                }
                doc.refreshComposite(changed);
                events.push_back(t.nsecsElapsed() / 1e6);
            }
            maskTiles = engine.coverageTileCount();
            before.swapWith(doc.pixels(0)); // undo, so every repetition starts from the same pixels
            doc.refreshComposite();
        }
        const Stats s = summarize(events);
        std::printf("| %s | %.4f | %.4f | %.4f | %s |\n", v.label, s.medianMs, s.p95Ms, s.maxMs,
                    v.legacy ? "-" : qPrintable(QString::number(maskTiles)));
    }

    // Smoothing alone (no painting), per event.
    for (qreal radius : {0.0, 20.0, 80.0}) {
        std::vector<double> events;
        for (int r = 0; r < std::max(1, reps / 10); ++r) {
            StrokeSmoother smoother(radius);
            std::vector<StrokeSample> out;
            smoother.begin(samples.front(), out);
            for (size_t i = 1; i < samples.size(); ++i) {
                out.clear();
                QElapsedTimer t;
                t.start();
                smoother.moveTo(samples[i], out);
                events.push_back(t.nsecsElapsed() / 1e6);
            }
        }
        const Stats s = summarize(events);
        std::printf("| smoothing only, string %.0f px (no painting) | %.4f | %.4f | %.4f | - |\n", radius, s.medianMs,
                    s.p95Ms, s.maxMs);
    }
}

// Every built-in preset at its own size and at a large size, per input event of the 1000-event
// zig-zag (8 ms apart, pressure varying), on documents with 1, 5 and 10 layers: engine + undo
// capture + composite refresh. Then the first event after a recovery snapshot (copy-on-write
// layer copy) against the next one, for a small and a large brush.
void presetBench(int reps)
{
    std::vector<StrokeSample> samples = zigZag();
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i].timeMs = i * 8.0;
    const int strokes = std::max(1, reps / 10);
    std::printf("\n### Brush presets: one input event of a 1000-event stroke (engine + undo capture + composite refresh)\n\n");
    std::printf("Coverage tiles are 64 x 64 x 16-bit (8 KiB); the undo snapshot holds the touched 64 x 64 layer tiles.\n");
    for (int layers : {1, 5, 10}) {
        LayeredDocument doc = makeDocument(layers);
        const int active = doc.activeIndex();
        std::printf("\n%d layer(s):\n\n| Preset | size px | median ms | p95 ms | max ms | coverage tiles | engine KiB | undo snapshot MiB |\n"
                    "|---|---|---|---|---|---|---|---|\n", layers);
        for (const BrushPreset &preset : BrushPresets::builtIns()) {
            for (double size : {preset.settings.size, 200.0}) {
                BrushSettings settings = preset.settings;
                settings.size = size;
                std::vector<double> events;
                int tiles = 0;
                qsizetype engineBytes = 0, snapshotBytes = 0;
                for (int r = 0; r < strokes; ++r) {
                    StrokeEngine engine(settings, QColor(30, 40, 90), quint64(r + 1), 0);
                    TileSnapshot before;
                    for (size_t i = 0; i < samples.size(); ++i) {
                        QElapsedTimer t;
                        t.start();
                        QRect changed = i == 0 ? engine.begin(doc.pixels(active), samples[i], before)
                                               : engine.moveTo(doc.pixels(active), samples[i], before);
                        if (i + 1 == samples.size())
                            changed |= engine.finish(doc.pixels(active), before);
                        doc.refreshComposite(changed);
                        events.push_back(t.nsecsElapsed() / 1e6);
                    }
                    tiles = engine.coverageTileCount();
                    engineBytes = engine.bufferBytes();
                    snapshotBytes = before.byteSize();
                    before.swapWith(doc.pixels(active)); // undo, so every repetition starts from the same pixels
                    doc.refreshComposite();
                }
                const Stats st = summarize(events);
                std::printf("| %s | %.0f | %.3f | %.3f | %.3f | %d | %.0f | %.1f |\n", qPrintable(settings.name), size,
                            st.medianMs, st.p95Ms, st.maxMs, tiles, engineBytes / 1024.0, snapshotBytes / 1048576.0);
            }
        }
        // First stroke event after a snapshot (autosave / save) pays for copying the shared layer.
        for (const char *id : {"builtin.clean-ink", "builtin.textured-paint"}) {
            BrushSettings settings = BrushPresets::builtIn(QString::fromLatin1(id))->settings;
            if (QLatin1String(id) == QLatin1String("builtin.textured-paint"))
                settings.size = 200;
            std::vector<double> first, next;
            for (int r = 0; r < reps; ++r) {
                const DocumentData snapshot = doc.snapshot();
                StrokeEngine engine(settings, Qt::black, 1, 0);
                TileSnapshot before;
                QElapsedTimer t;
                t.start();
                doc.refreshComposite(engine.begin(doc.pixels(active), samples[0], before));
                first.push_back(t.nsecsElapsed() / 1e6);
                t.restart();
                doc.refreshComposite(engine.moveTo(doc.pixels(active), samples[1], before));
                next.push_back(t.nsecsElapsed() / 1e6);
                before.swapWith(doc.pixels(active));
                doc.refreshComposite();
            }
            const Stats a = summarize(first), b = summarize(next);
            std::printf("| first event after a snapshot, %s %.0f px | | %.3f | %.3f | %.3f | (next event: median %.3f) | | |\n",
                        qPrintable(settings.name), settings.size, a.medianMs, a.p95Ms, a.maxMs, b.medianMs);
        }
        std::printf("\nProcess private memory with this document: %.0f MiB\n", privateMiB());
    }
}

void renderBench(const char *label, qreal zoom, const QRect &exposed, int reps)
{
    LayeredDocument doc = makeDocument(5);
    ViewTransform view;
    view.center(QSizeF(doc.size()), QSizeF(1920, 1080), zoom);
    QImage target(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    const Stats s = measure(reps, [&] {
        QPainter p(&target);
        ViewRenderer::render(p, doc.composite(), view, exposed);
    });
    std::printf("| render %s | %.4f | - | %.4f |\n", label, s.medianMs, s.maxMs);
}

} // namespace

int main(int argc, char **argv)
{
    const int reps = argc > 1 ? std::max(5, std::atoi(argv[1])) : 30;
    std::printf("%s, %s; canvas %dx%d\n", qPrintable(QSysInfo::prettyProductName()),
                qPrintable(QSysInfo::currentCpuArchitecture()), LayeredDocument::DefaultWidth,
                LayeredDocument::DefaultHeight);
    if (argc > 2 && std::strcmp(argv[2], "presets") == 0) {
        presetBench(reps);
        return 0;
    }
    singleLayerComparison(reps);
    brushEngineBench(reps);
    presetBench(reps);
    for (int layers : {1, 5, 10})
        layerBench(layers, reps);
    std::printf("\n### View rendering (composite cache drawn through the view; independent of layer count)\n\n");
    std::printf("| Operation | median ms | p95 ms | max ms |\n|---|---|---|---|\n");
    renderBench("full 1920x1080 view, zoom 0.5", 0.5, QRect(0, 0, 1920, 1080), reps);
    renderBench("full 1920x1080 view, zoom 1.0", 1.0, QRect(0, 0, 1920, 1080), reps);
    renderBench("64x64 dirty rect, zoom 1.0", 1.0, QRect(900, 500, 64, 64), reps * 10);
    return 0;
}

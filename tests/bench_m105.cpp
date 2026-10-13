// M10.5: matching deterministic M10 fixtures, real CanvasItem commit timings.
// Offscreen CPU wall times, never presentation FPS. One workload/mode per process.
#include "CanvasItem.h"
#include "ProjectFile.h"
#include "TransformPreview.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QTest>
#include <QLineF>
#include <cstdio>
#include <cmath>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
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


void row(const char *phase, int iteration, double ms)
{
    qint64 privateBytes = -1, peakPrivate = -1;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX p{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&p), sizeof(p))) {
        privateBytes = p.PrivateUsage;
        peakPrivate = p.PeakPagefileUsage;
    }
#endif
    std::printf("%s,%d,%.4f,%lld,%lld,%lld,%lld\n", phase, iteration, ms,
                MemoryBudget::usedTotal(), MemoryBudget::used(MemoryBudget::Category::Preview), privateBytes, peakPrivate);
}
template<class F> void timed(const char *phase, int i, F f)
{
    QElapsedTimer timer; timer.start(); f(); row(phase, i, timer.nsecsElapsed() / 1e6);
}
void require(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "REFUSED/FAILED: %s (%s)\n", what, qPrintable(MemoryBudget::shortageText()));
        std::exit(2);
    }
}
}
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const QString workload = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("4096x7");
    const bool legacy = argc > 2 && QString::fromLocal8Bit(argv[2]) == "legacy";
    const int reps = argc > 3 ? std::max(3, std::atoi(argv[3])) : 15;
    const QSize size = workload == "2048x10" ? QSize(2048,1536) : workload == "8192" ? QSize(8192,8192)
                         : workload == "16384" ? QSize(16384,16384) : QSize(4096,4096);
    const bool sparse = size.width() >= 8192;
    const int count = sparse ? 4 : workload == "2048x10" ? 10 : workload == "4096x20" ? 20 : 8;
    std::printf("# %s mode=%s iterations=%d viewport=1920x1080 physical pixels; Qt=%s; no presentation FPS\n",
                qPrintable(workload), legacy ? "legacy" : "viewport", reps, qVersion());
    std::printf("phase,iteration,ms,budget_bytes,preview_bytes,private_bytes,peak_private_bytes\n");
    QTemporaryDir temp;
    qputenv("DRAWAPP_BRUSH_FILE", temp.filePath("brushes.json").toUtf8());
    LayeredDocument document = sparse ? makeSparseDocument(size, count) : makeDocument(size, count);
    row("fixture", 0, 0);
    if (legacy) {
        TransformSession session;
        std::vector<TiledImage> images;
        for (int i = 1; i < count; ++i) images.push_back(document.layer(i).image);
        timed("begin", 0, [&] { require(session.begin(images, Selection()), "legacy begin"); });
        session.setSize(session.width()*1.2, session.height()*1.2);
        std::vector<LayerData> preview = document.layers();
        TiledImage composite = document.composite();
        for (int i = 0; i < reps; ++i) {
            session.setRotation(15 + (i % 5));
            timed("preview", i, [&] {
                for (int part = 0; part < session.partCount(); ++part)
                    require(session.render(part, preview[size_t(part)+1].image, composite.rect(), TransformSession::Quality::Fast), "legacy render");
                require(compositeLayers(composite, composite.rect(), preview), "legacy composite");
            });
        }
        return 0;
    }
    const QString path = temp.filePath("fixture.drawapp");
    require(ProjectFile::save(document.snapshot(), path).isEmpty(), "fixture save");
    document = LayeredDocument(QSize(1,1));
    CanvasItem canvas;
    canvas.setWidth(1920); canvas.setHeight(1080);
    require(canvas.openFile(QUrl::fromLocalFile(path)).isEmpty(), "open start");
    require(QTest::qWaitFor([&] { return !canvas.fileBusy(); }, 120000), "open completion");
    require(canvas.document().size() == size && canvas.layerCount() == count, "fixture reopened");
    canvas.fitToView();
    for (int i = 1; i < count; ++i) canvas.setLayerSelected(int(canvas.document().layer(i).id), true);
    canvas.setBackgroundApplyThresholdForTesting(0);
    QObject::connect(&canvas, &CanvasItem::notice, &canvas, [](const QString &message) {
        std::fprintf(stderr, "NOTICE: %s\n", qPrintable(message));
    });
    for (int run = 0; run < 3; ++run) {
        timed("begin", run, [&] { require(canvas.beginTransform().isEmpty(), "begin"); });
        canvas.setTransformValue("width", canvas.transformWidth()*1.2);
        canvas.setTransformValue("rotation", 15);
        timed("first_preview", run, [&] { canvas.flushDisplayPreviewForTesting(true); });
        require(!canvas.displayPreviewImage().isNull(), "viewport preview produced");
        std::printf("# preview %dx%d level=%d\n", canvas.displayPreviewImage().width(), canvas.displayPreviewImage().height(), canvas.displayPreviewLevel());
        if (run == 0) {
            for (int i = 0; i < reps; ++i) {
                timed("preview", i, [&] {
                    canvas.setTransformValue("rotation", 15 + (i % 5));
                    canvas.flushDisplayPreviewForTesting(true);
                });
            }
            for (int i = 0; i < 3; ++i)
                timed("refinement", i, [&] { canvas.flushDisplayPreviewForTesting(); });
        }
        timed("apply_dispatch", run, [&] { canvas.applyTransform(); });
        require(QTest::qWaitFor([&] { return !canvas.isApplying(); }, 120000), "Apply completion");
        require(!canvas.isTransforming(), "Apply committed");
        row("apply_worker", run, double(canvas.lastApplyWorkerMs()));
        row("apply_commit", run, canvas.lastApplyCommitMs());
        timed("undo", run, [&] { canvas.undo(); });
        timed("redo", run, [&] { canvas.redo(); });
        canvas.undo();
    }
    for (int i = 0; i < 3; ++i) {
        require(canvas.beginTransform().isEmpty(), "cancel begin");
        canvas.setTransformValue("rotation", 19);
        const int steps = canvas.history().undoCount(), redo = canvas.history().redoCount();
        canvas.applyTransform();
        QThread::msleep(25);
        timed("cancel", i, [&] { canvas.cancelApply(); });
        require(canvas.history().undoCount() == steps && canvas.history().redoCount() == redo, "cancel preserves history");
        canvas.cancelTransform();
    }
    row("cleanup", 0, 0);
    return 0;
}

// Drives the real CanvasItem in a window with synthetic 250 Hz mouse strokes while crash-recovery
// autosaves (every second) and one asynchronous project save run. Reports, per document size:
// input-handler time of ordinary events vs the first event after a snapshot (which copies the
// edited layer's shared pixels), event-loop/render time after each event, the largest UI stall,
// and worker / total times for recovery writes and the save.
//
// These are CPU times on this machine with synthetic events (run with QT_QPA_PLATFORM=offscreen
// for no desktop windows). They are not pen-to-display latency, which also includes OS input
// delivery, GPU upload, display refresh and compositor delays.

#include "CanvasItem.h"
#include "ProjectFile.h"
#include "RasterDocument.h"
#include "RecoveryManager.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct Summary { double median, p95, max; size_t n; };

Summary summarize(std::vector<double> v)
{
    if (v.empty())
        return {0, 0, 0, 0};
    std::sort(v.begin(), v.end());
    return {v[v.size() / 2], v[std::min(v.size() - 1, size_t(v.size() * 0.95))], v.back(), v.size()};
}

DocumentData makeDocument(int layers)
{
    DocumentData doc;
    doc.size = QSize(2048, 1536);
    for (int n = 0; n < layers; ++n) {
        LayerData layer;
        layer.id = quint64(n + 1);
        layer.name = n == 0 ? QStringLiteral("Background") : QStringLiteral("Layer %1").arg(n + 1);
        layer.image = RasterDocument::blank(doc.size);
        if (n == 0) {
            layer.image.fill(Qt::white);
        } else {
            std::vector<Dab> dabs;
            for (int i = 0; i < 2500; ++i)
                dabs.push_back({QPointF(80 + (i * 37 + n * 101) % 1880, 80 + (i * 53 + n * 67) % 1370), qreal(4 + (i + n) % 30)});
            RasterDocument::stampDabs(layer.image, dabs, QColor::fromHsv((n * 47) % 360, 160, 200), BrushMode::Paint);
        }
        layer.opacity = n % 3 == 0 && n > 0 ? 0.7 : 1.0;
        doc.layers.push_back(layer);
    }
    doc.activeLayerId = doc.layers[doc.layers.size() / 2].id;
    return doc;
}

qreal g_opacity = 1.0;
int g_smoothing = 0;
QString g_preset;

void run(int layers)
{
    QTemporaryDir dir;
    const QString project = dir.filePath("bench.drawapp");
    ProjectFile::save(makeDocument(layers), project);

    QQuickWindow window;
    window.resize(1400, 900);
    auto *canvas = new CanvasItem(window.contentItem());
    canvas->setSize(window.size());
    window.show();
    if (!QTest::qWaitForWindowExposed(&window))
        return;
    QSignalSpy opened(canvas, &CanvasItem::openFinished);
    canvas->openFile(QUrl::fromLocalFile(project));
    if (!QTest::qWaitFor([&] { return opened.count() > 0; }, 30000))
        return;
    if (!g_preset.isEmpty()) {
        // A preset as it is, with its own size and settings.
        canvas->brushes()->select(g_preset);
    } else {
        canvas->setBrushSize(12);
        canvas->setBrushOpacity(g_opacity);
        canvas->setSmoothing(g_smoothing);
    }

    RecoveryManager recovery(dir.filePath("recovery"));
    canvas->setAutosaveInterval(1000);
    canvas->setRecoveryManager(&recovery);

    bool snapshotPending = false;
    QObject::connect(canvas, &CanvasItem::recoverySnapshotTaken, [&] { snapshotPending = true; });
    std::vector<double> recoveryWorker, saveWorker;
    QObject::connect(&recovery, &RecoveryManager::writeFinished,
                     [&](bool, bool, const QString &, quint64, quint64, qint64 ms) { recoveryWorker.push_back(double(ms)); });
    QObject::connect(&canvas->projectIo(), &ProjectIo::saveFinished,
                     [&](quint64, const QString &, const SafeSave::Result &, qint64 ms) { saveWorker.push_back(double(ms)); });
    QElapsedTimer saveTotal;
    double saveTotalMs = -1, saveRequestMs = -1;
    QObject::connect(canvas, &CanvasItem::saveFinished, [&](bool, const QString &) {
        saveTotalMs = saveTotal.nsecsElapsed() / 1e6;
    });

    std::vector<double> ordinary, firstAfterSnapshot, frames, iterations;
    const auto toScene = [&](QPointF doc) { return canvas->mapToScene(canvas->viewTransform().docToView(doc)).toPoint(); };
    QElapsedTimer clock;
    clock.start();
    bool saveStarted = false;
    int event = 0;
    while (clock.elapsed() < 12000) {
        // One stroke per second: press, ~240 moves at 4 ms, release.
        const qreal y0 = 200 + (event / 250 % 10) * 110;
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, toScene(QPointF(150, y0)));
        for (int i = 1; i < 240; ++i, ++event) {
            const qint64 tickStart = clock.nsecsElapsed();
            const QPoint p = toScene(QPointF(150 + i * 7, y0 + 60 * std::sin(i / 15.0)));
            const bool first = snapshotPending;
            snapshotPending = false;
            QElapsedTimer t;
            t.start();
            QTest::mouseMove(&window, p);
            const double handler = t.nsecsElapsed() / 1e6;
            t.restart();
            QCoreApplication::processEvents(); // timers (autosave), worker results, rendering
            const double frame = t.nsecsElapsed() / 1e6;
            (first ? firstAfterSnapshot : ordinary).push_back(handler);
            frames.push_back(frame);
            iterations.push_back(handler + frame);
            if (!saveStarted && clock.elapsed() > 5000) {
                saveStarted = true;
                saveTotal.start();
                QElapsedTimer r;
                r.start();
                canvas->saveFileAs(QUrl::fromLocalFile(dir.filePath("saved.drawapp")));
                saveRequestMs = r.nsecsElapsed() / 1e6;
                snapshotPending = true; // the save took a snapshot too
            }
            const qint64 remaining = 4'000'000 - (clock.nsecsElapsed() - tickStart);
            if (remaining > 0)
                QThread::usleep(quint64(remaining / 1000));
        }
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, toScene(QPointF(150 + 240 * 7, y0)));
        QCoreApplication::processEvents();
    }
    if (!QTest::qWaitFor([&] { return !canvas->fileBusy() && !recovery.isWriting(); }, 30000))
        std::printf("(timed out waiting for background writes)\n");

    const Summary o = summarize(ordinary), f = summarize(firstAfterSnapshot), fr = summarize(frames),
                  it = summarize(iterations), rw = summarize(recoveryWorker);
    std::printf("\n### %d layer(s), %.1f s of strokes, %zu input events\n\n", layers, clock.elapsed() / 1000.0,
                ordinary.size() + firstAfterSnapshot.size());
    std::printf("| Measure (ms) | median | p95 | max | n |\n|---|---|---|---|---|\n");
    std::printf("| input handler, ordinary event | %.3f | %.3f | %.3f | %zu |\n", o.median, o.p95, o.max, o.n);
    std::printf("| input handler, first event after a snapshot (layer copy) | %.3f | %.3f | %.3f | %zu |\n",
                f.median, f.p95, f.max, f.n);
    std::printf("| event loop + render after each event | %.3f | %.3f | %.3f | %zu |\n", fr.median, fr.p95, fr.max, fr.n);
    std::printf("| whole iteration (largest UI stall = max) | %.3f | %.3f | %.3f | %zu |\n", it.median, it.p95, it.max, it.n);
    std::printf("| recovery write, worker thread | %.0f | %.0f | %.0f | %zu |\n", rw.median, rw.p95, rw.max, rw.n);
    std::printf("| save request on UI thread (snapshot + start) | %.3f | - | - | 1 |\n", saveRequestMs);
    std::printf("| save, worker thread | %.0f | - | - | %zu |\n", saveWorker.empty() ? -1.0 : saveWorker.front(),
                saveWorker.size());
    std::printf("| save, request to finished signal (elapsed while drawing) | %.0f | - | - | 1 |\n", saveTotalMs);
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    // Usage: bench_canvas [brush opacity 0..1] [smoothing 0..100] [preset id, e.g. builtin.textured-paint]
    if (argc > 1)
        g_opacity = std::atof(argv[1]);
    if (argc > 2)
        g_smoothing = std::atoi(argv[2]);
    if (argc > 3)
        g_preset = QString::fromLocal8Bit(argv[3]);
    if (!g_preset.isEmpty())
        std::printf("Preset: %s (its own settings; opacity and smoothing arguments ignored).\n", argv[3]);
    std::printf("Platform: %s. Synthetic mouse strokes at 250 Hz; autosave every 1 s; one async save at ~5 s; "
                "brush 12 px, opacity %.0f %%, smoothing %d.\n",
                qPrintable(QGuiApplication::platformName()), g_opacity * 100, g_smoothing);
    for (int layers : {1, 5, 10})
        run(layers);
    return 0;
}

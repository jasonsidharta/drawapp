// Prototype baseline: times the CPU raster paths used by the app. Prints a Markdown table.
// Usage: bench_core [repetitions]

#include "ProjectFile.h"
#include "RasterDocument.h"
#include "StrokeSpacer.h"
#include "TileSnapshot.h"
#include "UndoHistory.h"
#include "ViewRenderer.h"
#include "ViewTransform.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QPainter>
#include <QSysInfo>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct Stats { double medianMs, maxMs; };

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
    std::sort(ms.begin(), ms.end());
    return {ms[ms.size() / 2], ms.back()};
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

// The same stroke with undo capture, then one undo and one redo of it, as CanvasItem does.
void undoBench(int brush, int reps)
{
    const std::vector<StrokeSample> samples = zigZag();
    std::vector<double> perEvent, undoMs, redoMs;
    qsizetype bytes = 0;
    for (int r = 0; r < reps; ++r) {
        RasterDocument doc;
        UndoHistory history;
        StrokeSpacer spacer(brush);
        TileSnapshot before;
        std::vector<Dab> pending;
        for (size_t i = 0; i < samples.size(); ++i) {
            QElapsedTimer t;
            t.start();
            if (i == 0)
                spacer.begin(samples[i], pending);
            else
                spacer.moveTo(samples[i], pending);
            doc.stampDabs(pending, Qt::black, BrushMode::Paint, &before);
            pending.clear();
            perEvent.push_back(t.nsecsElapsed() / 1e6);
        }
        QElapsedTimer t;
        t.start();
        before.dropUnchanged(doc.image());
        bytes = before.byteSize();
        history.push(std::move(before), QStringLiteral("stroke"));
        perEvent.back() += t.nsecsElapsed() / 1e6; // commit cost lands on the release event
        t.restart();
        doc.undo(history);
        undoMs.push_back(t.nsecsElapsed() / 1e6);
        t.restart();
        doc.redo(history);
        redoMs.push_back(t.nsecsElapsed() / 1e6);
    }
    std::sort(perEvent.begin(), perEvent.end());
    std::sort(undoMs.begin(), undoMs.end());
    std::printf("| stroke event + undo capture, brush %d px | %.4f | %.4f | %.4f |\n", brush,
                perEvent[perEvent.size() / 2], perEvent[size_t(perEvent.size() * 0.95)], perEvent.back());
    std::printf("| undo (or redo) of that whole stroke, %.1f MiB of tiles | %.4f | - | %.4f |\n",
                bytes / (1024.0 * 1024.0), undoMs[undoMs.size() / 2], undoMs.back());
}

void fileBench(int reps)
{
    QTemporaryDir dir;
    RasterDocument doc;
    std::vector<Dab> dabs;
    for (int i = 0; i < 4000; ++i)
        dabs.push_back({QPointF(100 + (i * 37) % 1850, 100 + (i * 53) % 1340), qreal(6 + i % 40)});
    doc.stampDabs(dabs, QColor(40, 90, 200), BrushMode::Paint);

    QImage noise(doc.size(), QImage::Format_ARGB32_Premultiplied);
    quint32 x = 12345;
    for (int y = 0; y < noise.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(noise.scanLine(y));
        for (int i = 0; i < noise.width(); ++i) {
            x = x * 1664525u + 1013904223u;
            const int a = int(x >> 24);
            line[i] = qRgba(int((x >> 8) & 0xff) * a / 255, int((x >> 16) & 0xff) * a / 255, a / 2, a);
        }
    }

    for (const auto &[label, image] : {std::pair<const char *, QImage>{"painted dabs", doc.image()},
                                       std::pair<const char *, QImage>{"random noise (worst case)", noise}}) {
        const QString path = dir.filePath("bench.drawapp");
        const Stats save = measure(reps, [&] { ProjectFile::save(image, path); });
        const qint64 size = QFileInfo(path).size();
        const Stats load = measure(reps, [&] { ProjectFile::load(path); });
        const QString png = dir.filePath("bench.png");
        const Stats exportPng = measure(std::max(3, reps / 3), [&] { ProjectFile::exportPng(image, png); });
        std::printf("| save project, %s (%.2f MiB file) | %.1f | - | %.1f |\n", label, size / (1024.0 * 1024.0),
                    save.medianMs, save.maxMs);
        std::printf("| open project, %s | %.1f | - | %.1f |\n", label, load.medianMs, load.maxMs);
        std::printf("| export PNG, %s (%.2f MiB file) | %.1f | - | %.1f |\n", label,
                    QFileInfo(png).size() / (1024.0 * 1024.0), exportPng.medianMs, exportPng.maxMs);
    }
}

// One input event of a 1000-event zig-zag stroke: spacer + stamp, as CanvasItem does.
void strokeBench(int brush, int reps)
{
    std::vector<StrokeSample> samples;
    for (int i = 0; i < 1000; ++i) {
        const double t = i / 999.0;
        samples.push_back({QPointF(100 + 1800 * t, 768 + 500 * std::sin(t * 12.0)), 0.5 + 0.5 * std::sin(t * 7.0)});
    }
    std::vector<double> perEvent;
    long long dabs = 0;
    for (int r = 0; r < reps; ++r) {
        RasterDocument doc;
        StrokeSpacer spacer(brush);
        std::vector<Dab> pending;
        for (size_t i = 0; i < samples.size(); ++i) {
            QElapsedTimer t;
            t.start();
            if (i == 0)
                spacer.begin(samples[i], pending);
            else
                spacer.moveTo(samples[i], pending);
            dabs += static_cast<long long>(pending.size());
            doc.stampDabs(pending, Qt::black, BrushMode::Paint);
            pending.clear();
            perEvent.push_back(t.nsecsElapsed() / 1e6);
        }
    }
    std::sort(perEvent.begin(), perEvent.end());
    const double p95 = perEvent[size_t(perEvent.size() * 0.95)];
    std::printf("| stroke event, brush %d px (~%lld dabs/event) | %.4f | %.4f | %.4f |\n", brush,
                dabs / (long long)perEvent.size(), perEvent[perEvent.size() / 2], p95, perEvent.back());
}

void renderBench(const char *label, qreal zoom, const QRect &exposed, int reps)
{
    RasterDocument doc;
    std::vector<Dab> dabs;
    for (int i = 0; i < 400; ++i)
        dabs.push_back({QPointF(100 + i * 4.5, 300 + (i % 50) * 18), 24});
    doc.stampDabs(dabs, QColor(30, 90, 200), BrushMode::Paint);

    ViewTransform view;
    view.center(QSizeF(doc.size()), QSizeF(1920, 1080), zoom);
    QImage target(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    const Stats s = measure(reps, [&] {
        QPainter p(&target);
        ViewRenderer::render(p, doc.image(), view, exposed);
    });
    std::printf("| render %s | %.4f | - | %.4f |\n", label, s.medianMs, s.maxMs);
}

} // namespace

int main(int argc, char **argv)
{
    const int reps = argc > 1 ? std::max(3, std::atoi(argv[1])) : 30;
    std::printf("CPU: %s, %s\n", qPrintable(QSysInfo::prettyProductName()), qPrintable(QSysInfo::currentCpuArchitecture()));
    std::printf("Document: %dx%d ARGB32 premultiplied = %.1f MiB\n\n", RasterDocument::DefaultWidth,
                RasterDocument::DefaultHeight,
                RasterDocument::DefaultWidth * RasterDocument::DefaultHeight * 4 / (1024.0 * 1024.0));
    std::printf("| Operation | median ms | p95 ms | max ms |\n|---|---|---|---|\n");
    for (int brush : {4, 12, 64, 256})
        strokeBench(brush, std::max(1, reps / 10));
    renderBench("full 1920x1080 view, zoom 0.5", 0.5, QRect(0, 0, 1920, 1080), reps);
    renderBench("full 1920x1080 view, zoom 1.0", 1.0, QRect(0, 0, 1920, 1080), reps);
    renderBench("full 1920x1080 view, zoom 4.0", 4.0, QRect(0, 0, 1920, 1080), reps);
    renderBench("64x64 dirty rect, zoom 1.0", 1.0, QRect(900, 500, 64, 64), reps * 10);
    for (int brush : {12, 64})
        undoBench(brush, std::max(1, reps / 10));
    fileBench(std::max(3, reps / 3));
    {
        RasterDocument doc;
        const Stats s = measure(reps, [&] { doc.clear(); });
        std::printf("| clear document | %.4f | - | %.4f |\n", s.medianMs, s.maxMs);
    }
    return 0;
}

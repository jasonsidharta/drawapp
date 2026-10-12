// Milestone 10: TiledImage storage costs for tile sizes 64, 128 and 256, on workloads shaped like
// DrawApp's (brush dabs, strokes on sparse canvases, undo capture, whole-canvas composites, view
// assembly, conversions). Prints Markdown. CPU time on the calling thread.
// Usage: bench_tiles [repetitions]

#include "MemoryBudget.h"
#include "TiledImage.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QSysInfo>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double MiB = 1024.0 * 1024.0;

struct Stats { double median = 0, p95 = 0; int n = 0; };

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
    return {ms[ms.size() / 2], ms[std::min(ms.size() - 1, size_t(ms.size() * 0.95))], int(ms.size())};
}

// A round dab of radius r written through row pointers, the way StrokeEngine composites.
void dab(TiledImage &img, double cx, double cy, double r, QRgb colour)
{
    const QRect box = QRect(int(cx - r) - 1, int(cy - r) - 1, int(2 * r) + 3, int(2 * r) + 3) & img.rect();
    for (int y = box.top(); y <= box.bottom(); ++y) {
        int x = box.left();
        while (x <= box.right()) {
            const int tileEnd = std::min(box.right(), (img.columnOf(x) + 1) * img.tileSize() - 1);
            QRgb *row = img.writableRow(x, y);
            for (int i = x; i <= tileEnd; ++i) {
                const double dx = i + 0.5 - cx, dy = y + 0.5 - cy;
                if (dx * dx + dy * dy <= r * r)
                    row[i - x] = colour;
            }
            x = tileEnd + 1;
        }
    }
}

std::vector<QPointF> wavyStroke(QSize size, int dabs, double phase)
{
    std::vector<QPointF> v;
    for (int i = 0; i < dabs; ++i) {
        const double t = double(i) / dabs;
        v.emplace_back(size.width() * (0.05 + 0.9 * t), size.height() * (0.5 + 0.3 * std::sin(t * 12 + phase)));
    }
    return v;
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const int reps = argc > 1 ? std::max(3, std::atoi(argv[1])) : 15;
    std::printf("%s, %s; %d repetitions; Release build (MinGW, -O2)\n\n", qPrintable(QSysInfo::prettyProductName()),
                qPrintable(QSysInfo::currentCpuArchitecture()), reps);
    std::printf("| Workload | 64 | 128 | 256 |\n|---|---|---|---|\n");

    auto row = [&](const char *label, auto fn) {
        std::printf("| %s |", label);
        for (int t : {64, 128, 256})
            std::printf(" %s |", fn(t).c_str());
        std::printf("\n");
    };
    auto ms = [](const Stats &s) { return QStringLiteral("%1 / %2 ms").arg(s.median, 0, 'f', 2).arg(s.p95, 0, 'f', 2).toStdString(); };

    // 1. Brush dabs: 3000 dabs of radius 12 along a stroke on a 2048 x 1536 layer.
    row("3000 dabs r = 12 on 2048 x 1536 (median / p95)", [&](int t) {
        const auto pts = wavyStroke(QSize(2048, 1536), 3000, 0);
        return ms(measure(reps, [&] {
            TiledImage img(QSize(2048, 1536), t);
            for (const QPointF &p : pts)
                dab(img, p.x(), p.y(), 12, 0xff202020);
        }));
    });
    // 2. Same with large dabs.
    row("300 dabs r = 100 on 2048 x 1536", [&](int t) {
        const auto pts = wavyStroke(QSize(2048, 1536), 300, 1);
        return ms(measure(reps, [&] {
            TiledImage img(QSize(2048, 1536), t);
            for (const QPointF &p : pts)
                dab(img, p.x(), p.y(), 100, 0xff202020);
        }));
    });
    // 3. Memory of a sparse drawing: 40 strokes of 24 px on a 16384 x 16384 canvas.
    row("memory: 40 strokes (24 px) on 16384^2 (dense would be 1024 MiB)", [&](int t) {
        TiledImage img(QSize(16384, 16384), t);
        for (int s = 0; s < 40; ++s)
            for (const QPointF &p : wavyStroke(QSize(16384, 16384), 1500, s * 0.7))
                dab(img, p.x(), p.y() * (0.2 + s * 0.02), 12, 0xff202020);
        return QStringLiteral("%1 MiB, %2 tiles").arg(img.allocatedBytes() / MiB, 0, 'f', 1).arg(img.allocatedTileCount()).toStdString();
    });
    // 4. Undo capture: copy-on-write cost of one stroke on a full layer (snapshot + writes).
    row("undo: snapshot + one 24 px stroke on a full 2048 x 1536 layer (time; bytes copied)", [&](int t) {
        TiledImage img(QSize(2048, 1536), t);
        img.fillRect(img.rect(), 0xff808080);
        const auto pts = wavyStroke(QSize(2048, 1536), 1500, 2);
        qint64 copied = 0;
        const Stats s = measure(reps, [&] {
            const TiledImage snapshot = img;
            const qint64 before = MemoryBudget::used(MemoryBudget::Category::Document);
            for (const QPointF &p : pts)
                dab(img, p.x(), p.y(), 12, 0xff202020);
            copied = MemoryBudget::used(MemoryBudget::Category::Document) - before;
        });
        return QStringLiteral("%1; %2 MiB").arg(QString::fromStdString(ms(s))).arg(copied / MiB, 0, 'f', 2).toStdString();
    });
    // 5. Whole-canvas composite: 10 full layers 2048 x 1536 into a composite, tile by tile.
    row("composite of 10 full 2048 x 1536 layers (QPainter per tile)", [&](int t) {
        std::vector<TiledImage> layers;
        for (int i = 0; i < 10; ++i) {
            TiledImage l(QSize(2048, 1536), t);
            l.fillRect(QRect(i * 100, 0, 2048 - i * 100, 1536), qRgba(i * 20, 100, 50, 255));
            layers.push_back(l);
        }
        TiledImage composite(QSize(2048, 1536), t, MemoryBudget::Category::Composite);
        return ms(measure(reps, [&] {
            for (int r = 0; r < composite.rows(); ++r)
                for (int c = 0; c < composite.columns(); ++c) {
                    QImage out = composite.writableTileImage(c, r);
                    out.fill(Qt::transparent);
                    QPainter p(&out);
                    for (const TiledImage &l : layers) {
                        const QImage tile = l.tileImage(c, r);
                        if (!tile.isNull()) {
                            p.setOpacity(0.9);
                            p.drawImage(0, 0, tile);
                        }
                    }
                }
        }));
    });
    // 6. View assembly: copy a 1920 x 1080 region out of a full layer.
    row("copy a 1920 x 1080 region (view assembly)", [&](int t) {
        TiledImage img(QSize(4096, 4096), t);
        img.fillRect(img.rect(), 0xff406080);
        return ms(measure(reps, [&] { img.copy(QRect(1000, 1000, 1920, 1080)); }));
    });
    // 7. Conversion of a full 4096 x 4096 layer.
    row("fromImage of a full 4096 x 4096 image", [&](int t) {
        QImage dense(4096, 4096, QImage::Format_ARGB32_Premultiplied);
        dense.fill(0xff102030);
        return ms(measure(std::max(3, reps / 3), [&] { TiledImage::fromImage(dense, t); }));
    });
    // 8. Snapshot of a 16384^2 layer and the first write after it (grid detach).
    row("snapshot of a 16384^2 layer + first write (grid detach)", [&](int t) {
        TiledImage img(QSize(16384, 16384), t);
        img.setPixel(10, 10, 0xff000000);
        return ms(measure(reps, [&] {
            const TiledImage snap = img;
            img.setPixel(20, 20, 0xff000000);
        }));
    });
    // 9. Over-allocation of a thin 1 px diagonal line across 2048 x 1536.
    row("memory: 1 px diagonal line across 2048 x 1536", [&](int t) {
        TiledImage img(QSize(2048, 1536), t);
        for (int i = 0; i < 1536; ++i)
            img.setPixel(i * 4 / 3, i, 0xff000000);
        return QStringLiteral("%1 MiB").arg(img.allocatedBytes() / MiB, 0, 'f', 2).toStdString();
    });
    return 0;
}

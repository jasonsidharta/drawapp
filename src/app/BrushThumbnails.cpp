#include "BrushThumbnails.h"

#include "StrokeEngine.h"
#include "TileSnapshot.h"

#include <QMutex>
#include <QPainter>
#include <cmath>
#include <map>

namespace {

QMutex registryMutex;
std::map<QString, BrushSettings> &registry()
{
    static std::map<QString, BrushSettings> map;
    return map;
}

} // namespace

BrushThumbnails::BrushThumbnails()
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading)
{
}

void BrushThumbnails::publish(const QString &presetId, const BrushSettings &settings)
{
    QMutexLocker lock(&registryMutex);
    registry()[presetId] = settings;
}

QImage BrushThumbnails::render(const BrushSettings &brush, QSize size)
{
    const int w = std::max(16, size.width()), h = std::max(12, size.height());
    BrushSettings s = brush;
    // Large brushes are drawn smaller so the whole stroke fits; everything else is as painted.
    s.size = std::min(s.size, h * 0.55);
    s.smoothing = 0;

    QImage layer(w, h, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    const bool onColour = s.blend != BrushBlend::Paint;
    if (onColour) {
        // Something to erase or smudge: colour bands.
        const QColor bands[] = {QColor(205, 70, 60), QColor(235, 190, 70), QColor(60, 120, 200)};
        QPainter p(&layer);
        for (int i = 0; i < 3; ++i)
            p.fillRect(QRectF(w * i / 3.0, 0, w / 3.0 + 1, h), bands[i]);
    }
    // An S-stroke with pressure rising and falling, 8 ms between events.
    std::vector<StrokeSample> samples;
    const double margin = s.size / 2 + 4;
    for (int i = 0; i <= 60; ++i) {
        const double t = i / 60.0;
        StrokeSample sample;
        sample.pos = QPointF(margin + (w - 2 * margin) * t, h / 2.0 - (h / 2.0 - margin) * 0.8 * std::sin(t * 2 * M_PI));
        sample.pressure = 0.15 + 0.85 * std::sin(t * M_PI);
        sample.timeMs = i * 8.0;
        samples.push_back(sample);
    }
    StrokeEngine::render(layer, s, QColor(30, 34, 48), samples, 1);

    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    if (onColour) {
        for (int y = 0; y < h; y += 6)
            for (int x = 0; x < w; x += 6)
                p.fillRect(x, y, 6, 6, ((x + y) / 6) % 2 ? QColor(205, 205, 205) : QColor(245, 245, 245));
    } else {
        out.fill(QColor(252, 251, 248));
    }
    p.drawImage(0, 0, layer);
    return out;
}

QImage BrushThumbnails::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // id is "<preset id>/<revision>"; the revision only defeats the image cache.
    const QString presetId = id.section(QLatin1Char('/'), 0, 0);
    BrushSettings settings;
    {
        QMutexLocker lock(&registryMutex);
        const auto it = registry().find(presetId);
        if (it == registry().end())
            return QImage();
        settings = it->second;
    }
    const QSize want(requestedSize.width() > 0 ? requestedSize.width() : DefaultWidth,
                     requestedSize.height() > 0 ? requestedSize.height() : DefaultHeight);
    const QImage image = render(settings, want.boundedTo(QSize(1024, 512)));
    if (size)
        *size = image.size();
    return image;
}

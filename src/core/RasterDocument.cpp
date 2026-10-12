#include "RasterDocument.h"

#include "TileSnapshot.h"

#include <QPainter>
#include <algorithm>

RasterDocument::RasterDocument(QSize size)
    : m_image(blank(size))
{
}

RasterDocument::RasterDocument(const QImage &image)
    : m_image(image.convertToFormat(QImage::Format_ARGB32_Premultiplied))
{
}

QImage RasterDocument::blank(QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

QRect RasterDocument::stampDabs(QImage &image, const std::vector<Dab> &dabs, const QColor &color, BrushMode mode,
                                TileSnapshot *before)
{
    if (dabs.empty())
        return {};

    QRect dirty;
    for (const Dab &dab : dabs) {
        const qreal r = dab.diameter / 2.0;
        // One extra pixel for anti-aliased edges.
        const QRect dabRect = QRectF(dab.center.x() - r, dab.center.y() - r, dab.diameter, dab.diameter)
                                  .toAlignedRect().adjusted(-1, -1, 1, 1) & image.rect();
        // Per dab, not the union: a long diagonal segment must not capture its whole bounding box.
        if (before)
            before->capture(image, dabRect);
        dirty |= dabRect;
    }
    if (dirty.isEmpty())
        return {};

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    if (mode == BrushMode::Erase) {
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.setBrush(QColor(0, 0, 0, 255));
    } else {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setBrush(color);
    }
    for (const Dab &dab : dabs) {
        const qreal r = dab.diameter / 2.0;
        painter.drawEllipse(dab.center, r, r);
    }
    return dirty;
}

QRect RasterDocument::stampDabs(TiledImage &image, const std::vector<Dab> &dabs, const QColor &color, BrushMode mode,
                                TileSnapshot *before)
{
    std::vector<QRect> rects;
    QRect dirty;
    for (const Dab &dab : dabs) {
        const qreal r = dab.diameter / 2.0;
        rects.push_back(QRectF(dab.center.x() - r, dab.center.y() - r, dab.diameter, dab.diameter)
                            .toAlignedRect().adjusted(-1, -1, 1, 1) & image.rect());
        dirty |= rects.back();
    }
    if (dirty.isEmpty())
        return {};
    // Phase 1: every touched tile writable (erasing a missing tile changes nothing).
    struct Target
    {
        int column, row;
        QImage pixels;
    };
    std::vector<Target> targets;
    for (int row = image.rowOf(dirty.top()); row <= image.rowOf(dirty.bottom()); ++row)
        for (int column = image.columnOf(dirty.left()); column <= image.columnOf(dirty.right()); ++column) {
            const QRect tile = image.tileRect(column, row);
            if (std::none_of(rects.begin(), rects.end(), [&](const QRect &r) { return r.intersects(tile); }))
                continue;
            if (mode == BrushMode::Erase && !image.hasTile(column, row))
                continue;
            targets.push_back({column, row, QImage()});
        }
    if (before)
        for (const QRect &r : rects)
            before->capture(image, r);
    for (Target &t : targets) {
        t.pixels = image.writableTileImage(t.column, t.row);
        if (t.pixels.isNull())
            return {}; // tiles made writable so far hold the same pixels: nothing changed
    }
    // Phase 2: the dabs, tile by tile. Translation by whole pixels rasterises exactly as on one
    // dense image.
    for (Target &t : targets) {
        const QRect tile = image.tileRect(t.column, t.row);
        QPainter painter(&t.pixels);
        painter.translate(-tile.topLeft());
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        if (mode == BrushMode::Erase) {
            painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            painter.setBrush(QColor(0, 0, 0, 255));
        } else {
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.setBrush(color);
        }
        for (size_t i = 0; i < dabs.size(); ++i) {
            if (!rects[i].intersects(tile))
                continue;
            const qreal r = dabs[i].diameter / 2.0;
            painter.drawEllipse(dabs[i].center, r, r);
        }
    }
    return dirty;
}

QRect RasterDocument::clear(TiledImage &image, TileSnapshot *before)
{
    if (before)
        before->captureAllocated(image);
    image.clear();
    return image.rect();
}

QRect RasterDocument::clear(QImage &image, TileSnapshot *before)
{
    if (before)
        before->captureNonTransparent(image);
    image.fill(Qt::transparent);
    return image.rect();
}

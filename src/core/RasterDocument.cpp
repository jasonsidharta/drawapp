#include "RasterDocument.h"

#include "TileSnapshot.h"

#include <QPainter>

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

QRect RasterDocument::clear(QImage &image, TileSnapshot *before)
{
    if (before)
        before->captureNonTransparent(image);
    image.fill(Qt::transparent);
    return image.rect();
}

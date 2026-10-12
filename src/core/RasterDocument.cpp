#include "RasterDocument.h"

#include <QPainter>
#include <cmath>

RasterDocument::RasterDocument(QSize size)
    : m_image(size, QImage::Format_ARGB32_Premultiplied)
{
    m_image.fill(Qt::transparent);
}

QRect RasterDocument::stampDabs(const std::vector<Dab> &dabs, const QColor &color, BrushMode mode)
{
    if (dabs.empty())
        return {};

    QRectF changed;
    for (const Dab &dab : dabs) {
        const qreal r = dab.diameter / 2.0;
        changed |= QRectF(dab.center.x() - r, dab.center.y() - r, dab.diameter, dab.diameter);
    }
    // One extra pixel for anti-aliased edges.
    const QRect dirty = changed.toAlignedRect().adjusted(-1, -1, 1, 1) & bounds();
    if (dirty.isEmpty())
        return {};

    QPainter painter(&m_image);
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

QRect RasterDocument::clear()
{
    m_image.fill(Qt::transparent);
    return bounds();
}

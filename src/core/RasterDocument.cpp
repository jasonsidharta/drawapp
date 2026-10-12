#include "RasterDocument.h"

#include "TileSnapshot.h"
#include "UndoHistory.h"

#include <QPainter>

RasterDocument::RasterDocument(QSize size)
    : m_image(size, QImage::Format_ARGB32_Premultiplied)
{
    m_image.fill(Qt::transparent);
}

RasterDocument::RasterDocument(const QImage &image)
    : m_image(image.convertToFormat(QImage::Format_ARGB32_Premultiplied))
{
}

QRect RasterDocument::stampDabs(const std::vector<Dab> &dabs, const QColor &color, BrushMode mode,
                                TileSnapshot *before)
{
    if (dabs.empty())
        return {};

    QRect dirty;
    for (const Dab &dab : dabs) {
        const qreal r = dab.diameter / 2.0;
        // One extra pixel for anti-aliased edges.
        const QRect dabRect = QRectF(dab.center.x() - r, dab.center.y() - r, dab.diameter, dab.diameter)
                                  .toAlignedRect().adjusted(-1, -1, 1, 1) & bounds();
        // Per dab, not the union: a long diagonal segment must not capture its whole bounding box.
        if (before)
            before->capture(m_image, dabRect);
        dirty |= dabRect;
    }
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

QRect RasterDocument::clear(TileSnapshot *before)
{
    if (before)
        before->captureNonTransparent(m_image);
    m_image.fill(Qt::transparent);
    return bounds();
}

QRect RasterDocument::undo(UndoHistory &history)
{
    return history.undo(m_image);
}

QRect RasterDocument::redo(UndoHistory &history)
{
    return history.redo(m_image);
}

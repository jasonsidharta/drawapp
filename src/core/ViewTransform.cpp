#include "ViewTransform.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

QRectF ViewTransform::docToView(const QRectF &doc) const
{
    return QRectF(docToView(doc.topLeft()), doc.size() * m_zoom);
}

QRectF ViewTransform::viewToDoc(const QRectF &view) const
{
    return QRectF(viewToDoc(view.topLeft()), view.size() / m_zoom);
}

QTransform ViewTransform::toQTransform() const
{
    return QTransform(m_zoom, 0, 0, m_zoom, m_pan.x(), m_pan.y());
}

qreal ViewTransform::clampZoom(qreal zoom)
{
    if (!std::isfinite(zoom))
        return 1.0;
    return std::clamp(zoom, MinZoom, MaxZoom);
}

void ViewTransform::setZoomAt(qreal zoom, const QPointF &viewAnchor)
{
    const QPointF docAnchor = viewToDoc(viewAnchor);
    m_zoom = clampZoom(zoom);
    m_pan = viewAnchor - docAnchor * m_zoom;
}

void ViewTransform::fit(const QSizeF &docSize, const QSizeF &viewSize, qreal margin)
{
    if (docSize.isEmpty() || viewSize.isEmpty())
        return;
    const qreal availW = std::max<qreal>(1.0, viewSize.width() - 2 * margin);
    const qreal availH = std::max<qreal>(1.0, viewSize.height() - 2 * margin);
    const qreal zoom = std::min({availW / docSize.width(), availH / docSize.height(), 1.0});
    center(docSize, viewSize, zoom);
}

void ViewTransform::center(const QSizeF &docSize, const QSizeF &viewSize, qreal zoom)
{
    m_zoom = clampZoom(zoom);
    m_pan = QPointF((viewSize.width() - docSize.width() * m_zoom) / 2.0,
                    (viewSize.height() - docSize.height() * m_zoom) / 2.0);
}

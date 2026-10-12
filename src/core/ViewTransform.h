#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>

// Maps between document pixels and view (item) pixels: view = doc * zoom + pan.
// Only the view changes when zooming or panning; document pixels are never touched.
class ViewTransform
{
public:
    static constexpr qreal MinZoom = 0.05;
    static constexpr qreal MaxZoom = 32.0;

    qreal zoom() const { return m_zoom; }
    QPointF pan() const { return m_pan; }

    QPointF docToView(const QPointF &doc) const { return doc * m_zoom + m_pan; }
    QPointF viewToDoc(const QPointF &view) const { return (view - m_pan) / m_zoom; }
    QRectF docToView(const QRectF &doc) const;
    QRectF viewToDoc(const QRectF &view) const;
    QTransform toQTransform() const;

    void setPan(const QPointF &pan) { m_pan = pan; }
    void panBy(const QPointF &delta) { m_pan += delta; }

    // Sets the zoom so that the document point under viewAnchor stays under viewAnchor.
    void setZoomAt(qreal zoom, const QPointF &viewAnchor);
    void zoomByAt(qreal factor, const QPointF &viewAnchor) { setZoomAt(m_zoom * factor, viewAnchor); }

    // Centers the document in the view at the largest zoom (max 1.0) that fits with a margin.
    void fit(const QSizeF &docSize, const QSizeF &viewSize, qreal margin = 24.0);
    // Shows the document at 100 % centered in the view.
    void center(const QSizeF &docSize, const QSizeF &viewSize, qreal zoom = 1.0);

    static qreal clampZoom(qreal zoom);

private:
    qreal m_zoom = 1.0;
    QPointF m_pan;
};

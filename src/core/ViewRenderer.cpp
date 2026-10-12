#include "ViewRenderer.h"

#include <QPainter>

namespace {

const QBrush &checkerBrush()
{
    static const QBrush brush = [] {
        QImage tile(2 * ViewRenderer::CheckerSize, 2 * ViewRenderer::CheckerSize, QImage::Format_ARGB32_Premultiplied);
        tile.fill(ViewRenderer::CheckerLight);
        QPainter p(&tile);
        const int s = ViewRenderer::CheckerSize;
        p.fillRect(s, 0, s, s, QColor::fromRgb(ViewRenderer::CheckerDark));
        p.fillRect(0, s, s, s, QColor::fromRgb(ViewRenderer::CheckerDark));
        return QBrush(tile);
    }();
    return brush;
}

} // namespace

void ViewRenderer::render(QPainter &painter, const QImage &document, const ViewTransform &view, const QRect &exposed)
{
    if (exposed.isEmpty())
        return;

    painter.save();
    painter.setClipRect(exposed, Qt::IntersectClip);
    painter.fillRect(exposed, QColor::fromRgb(WorkspaceColor));

    const QRectF docInView = view.docToView(QRectF(document.rect()));
    const QRectF visible = docInView & QRectF(exposed);
    if (!visible.isEmpty()) {
        // The checkerboard stays screen-aligned so it reads as "no paint here" at any zoom.
        painter.fillRect(visible, checkerBrush());

        // Source pixels needed for the visible part only, with a one-pixel margin for filtering.
        const QRect source = view.viewToDoc(visible).toAlignedRect().adjusted(-1, -1, 1, 1) & document.rect();
        if (!source.isEmpty()) {
            painter.setClipRect(visible, Qt::IntersectClip);
            painter.setTransform(view.toQTransform(), true);
            // Smooth when shrinking; hard pixel edges when magnifying so artists see real pixels.
            painter.setRenderHint(QPainter::SmoothPixmapTransform, view.zoom() < 1.0);
            painter.drawImage(QRectF(source), document, source);
        }
    }
    painter.restore();
}

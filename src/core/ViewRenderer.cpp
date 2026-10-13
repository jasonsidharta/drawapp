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

// Background, checkerboard, then draw(source) with the view transform set, where source is the
// document area needed (with a one-pixel margin for filtering).
template <typename Draw>
void renderWith(QPainter &painter, const QRect &documentRect, const ViewTransform &view, const QRect &exposed, Draw draw)
{
    using namespace ViewRenderer;
    if (exposed.isEmpty())
        return;

    painter.save();
    painter.setClipRect(exposed, Qt::IntersectClip);
    painter.fillRect(exposed, QColor::fromRgb(WorkspaceColor));

    const QRectF docInView = view.docToView(QRectF(documentRect));
    const QRectF visible = docInView & QRectF(exposed);
    if (!visible.isEmpty()) {
        // The checkerboard stays screen-aligned so it reads as "no paint here" at any zoom.
        painter.fillRect(visible, checkerBrush());

        // Source pixels needed for the visible part only, with a one-pixel margin for filtering.
        const QRect source = view.viewToDoc(visible).toAlignedRect().adjusted(-1, -1, 1, 1) & documentRect;
        if (!source.isEmpty()) {
            painter.setClipRect(visible, Qt::IntersectClip);
            painter.setTransform(view.toQTransform(), true);
            // Smooth when shrinking; hard pixel edges when magnifying so artists see real pixels.
            painter.setRenderHint(QPainter::SmoothPixmapTransform, view.zoom() < 1.0);
            draw(source);
        }
    }
    painter.restore();
}

} // namespace

void ViewRenderer::render(QPainter &painter, const QImage &document, const ViewTransform &view, const QRect &exposed)
{
    renderWith(painter, document.rect(), view, exposed,
               [&](const QRect &source) { painter.drawImage(QRectF(source), document, source); });
}

void ViewRenderer::render(QPainter &painter, const TiledImage &document, ViewPyramid &pyramid, const ViewTransform &view,
                          const QRect &exposed)
{
    renderWith(painter, document.rect(), view, exposed, [&](const QRect &source) {
        for (int level = ViewPyramid::levelFor(view.zoom(), document.size()); level <= ViewPyramid::MaxLevel; ++level) {
            // The level's pixels covering source (which has a margin for filtering already; a
            // reduced level gets one of its own). Level 0: exactly the overload above.
            const int m = level ? 1 : 0;
            const QRect area = QRect(QPoint(source.left() >> level, source.top() >> level),
                                     QPoint(source.right() >> level, source.bottom() >> level))
                                   .adjusted(-m, -m, m, m);
            const QImage pixels = pyramid.pixels(document, level, area);
            if (pixels.isNull())
                continue; // memory refused: a smaller level
            const QRect inside = area & QRect(QPoint(0, 0), ViewPyramid::levelSize(document.size(), level));
            const double scale = double(qint64(1) << level);
            // A level pixel covers scale x scale document pixels.
            painter.setRenderHint(QPainter::SmoothPixmapTransform, view.zoom() * scale < 1.0);
            painter.drawImage(QRectF(inside.left() * scale, inside.top() * scale, inside.width() * scale,
                                     inside.height() * scale),
                              pixels, pixels.rect());
            return;
        }
    });
}

void ViewRenderer::render(QPainter &painter, const TiledImage &document, const ViewTransform &view, const QRect &exposed)
{
    renderWith(painter, document.rect(), view, exposed, [&](const QRect &source) {
        const QImage visible = document.copy(source); // null if the memory budget refuses: checkerboard only
        if (!visible.isNull())
            painter.drawImage(QRectF(source), visible, visible.rect());
    });
}

void ViewRenderer::renderPreview(QPainter &painter, QSize documentSize, const QImage &pixels, const QRectF &documentArea,
                                 const ViewTransform &view, const QRect &exposed)
{
    renderWith(painter, QRect(QPoint(), documentSize), view, exposed, [&](const QRect &) {
        painter.drawImage(documentArea, pixels, pixels.rect());
    });
}

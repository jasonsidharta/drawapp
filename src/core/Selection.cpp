#include "Selection.h"

#include <QPainter>
#include <algorithm>
#include <cstring>

namespace {

QImage blankMask(QSize size)
{
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    return mask;
}

} // namespace

Selection Selection::all(QSize size)
{
    Selection s;
    s.m_mask = QImage(size, QImage::Format_Grayscale8);
    s.m_mask.fill(255);
    s.m_bounds = s.m_mask.rect();
    return s;
}

Selection Selection::fromMask(const QImage &mask)
{
    Selection s;
    if (mask.isNull())
        return s;
    s.m_mask = mask.format() == QImage::Format_Grayscale8 ? mask : mask.convertToFormat(QImage::Format_Grayscale8);
    s.updateBounds();
    return s;
}

void Selection::updateBounds()
{
    int left = m_mask.width(), right = -1, top = m_mask.height(), bottom = -1;
    for (int y = 0; y < m_mask.height(); ++y) {
        const uchar *row = m_mask.constScanLine(y);
        int x0 = 0;
        while (x0 < m_mask.width() && row[x0] == 0)
            ++x0;
        if (x0 == m_mask.width())
            continue;
        int x1 = m_mask.width() - 1;
        while (row[x1] == 0)
            --x1;
        left = std::min(left, x0);
        right = std::max(right, x1);
        top = std::min(top, y);
        bottom = y;
    }
    m_bounds = right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

int Selection::coverageAt(int x, int y) const
{
    if (!isActive())
        return 255;
    if (x < 0 || y < 0 || x >= m_mask.width() || y >= m_mask.height())
        return 0;
    return m_mask.constScanLine(y)[x];
}

bool Selection::operator==(const Selection &other) const
{
    if (isActive() != other.isActive())
        return false;
    return !isActive() || m_mask == other.m_mask;
}

QImage Selection::rasterize(QSize size, const QPainterPath &path, bool antialias)
{
    // QPainter fills Alpha8 images directly; the bytes are then the coverage.
    QImage alpha(size, QImage::Format_Alpha8);
    alpha.fill(0);
    if (!path.isEmpty()) {
        QPainterPath filled = path;
        filled.setFillRule(Qt::WindingFill);
        QPainter p(&alpha);
        p.setRenderHint(QPainter::Antialiasing, antialias);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawPath(filled);
    }
    alpha.reinterpretAsFormat(QImage::Format_Grayscale8);
    return alpha;
}

Selection Selection::combined(const QImage &shape, SelectionMode mode) const
{
    const QImage shapeMask =
        shape.format() == QImage::Format_Grayscale8 ? shape : shape.convertToFormat(QImage::Format_Grayscale8);
    if (mode == SelectionMode::Replace || (!isActive() && mode == SelectionMode::Add))
        return fromMask(shapeMask);
    // "None" combines as nothing selected.
    const QImage old = isActive() ? m_mask : blankMask(shapeMask.size());
    QImage out(shapeMask.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < out.height(); ++y) {
        const uchar *a = old.constScanLine(y);
        const uchar *b = shapeMask.constScanLine(y);
        uchar *o = out.scanLine(y);
        for (int x = 0; x < out.width(); ++x) {
            switch (mode) {
            case SelectionMode::Add: o[x] = uchar(std::min(255, a[x] + b[x])); break;
            case SelectionMode::Subtract: o[x] = uchar(std::max(0, a[x] - b[x])); break;
            default: o[x] = std::min(a[x], b[x]); break;
            }
        }
    }
    Selection s;
    s.m_mask = out;
    s.updateBounds();
    return s;
}

Selection Selection::inverted() const
{
    if (!isActive())
        return *this;
    Selection s;
    s.m_mask = QImage(m_mask.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < m_mask.height(); ++y) {
        const uchar *a = m_mask.constScanLine(y);
        uchar *o = s.m_mask.scanLine(y);
        for (int x = 0; x < m_mask.width(); ++x)
            o[x] = uchar(255 - a[x]);
    }
    s.updateBounds();
    return s;
}

QPainterPath Selection::outline() const
{
    QPainterPath path;
    if (!isActive() || m_bounds.isEmpty())
        return path;
    const int w = m_mask.width(), h = m_mask.height();
    auto in = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && m_mask.constScanLine(y)[x] >= 128;
    };
    const QRect r = m_bounds.adjusted(-1, -1, 1, 1);
    // Horizontal edges: between row y − 1 and row y, merged into runs.
    for (int y = r.top() + 1; y <= r.bottom(); ++y) {
        int start = -1;
        for (int x = r.left(); x <= r.right() + 1; ++x) {
            const bool edge = x <= r.right() && in(x, y - 1) != in(x, y);
            if (edge && start < 0)
                start = x;
            if (!edge && start >= 0) {
                path.moveTo(start, y);
                path.lineTo(x, y);
                start = -1;
            }
        }
    }
    // Vertical edges: between column x − 1 and column x.
    for (int x = r.left() + 1; x <= r.right(); ++x) {
        int start = -1;
        for (int y = r.top(); y <= r.bottom() + 1; ++y) {
            const bool edge = y <= r.bottom() && in(x - 1, y) != in(x, y);
            if (edge && start < 0)
                start = y;
            if (!edge && start >= 0) {
                path.moveTo(x, start);
                path.lineTo(x, y);
                start = -1;
            }
        }
    }
    return path;
}

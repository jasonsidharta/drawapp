#include "StrokeCompositor.h"

#include "TileSnapshot.h"

#include <algorithm>
#include <cmath>

namespace {

qint64 tileKey(int column, int row)
{
    return (qint64(row) << 32) | quint32(column);
}

// x * y / 255, rounded, for 0..255 operands.
inline quint32 mul255(quint32 x, quint32 y)
{
    const quint32 t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

} // namespace

StrokeCompositor::StrokeCompositor(const QColor &color, qreal opacity, BrushMode mode)
    : m_color(qRgb(color.red(), color.green(), color.blue()))
    , m_opacity(qRound(std::clamp(opacity, 0.0, 1.0) * 256))
    , m_mode(mode)
{
}

StrokeCompositor::MaskTile &StrokeCompositor::maskTile(int column, int row)
{
    const auto [it, inserted] = m_mask.try_emplace(tileKey(column, row));
    if (inserted)
        it->second.fill(0);
    return it->second;
}

QRect StrokeCompositor::addDabs(QImage &image, const std::vector<Dab> &dabs, TileSnapshot &before)
{
    Q_ASSERT(image.format() == QImage::Format_ARGB32_Premultiplied);
    QRect dirty;
    std::unordered_map<qint64, QRect> touched; // per tile: the area to recompute
    for (const Dab &dab : dabs) {
        const qreal r = dab.diameter / 2.0;
        // One extra pixel for the anti-aliased edge.
        const QRect rect = QRectF(dab.center.x() - r, dab.center.y() - r, dab.diameter, dab.diameter)
                               .toAlignedRect().adjusted(-1, -1, 1, 1) & image.rect();
        if (rect.isEmpty())
            continue;
        // Per dab, not the union: a long diagonal segment must not capture its whole bounding box.
        before.capture(image, rect);
        rasterize(dab, rect);
        for (int row = rect.top() / Tile; row <= rect.bottom() / Tile; ++row) {
            for (int column = rect.left() / Tile; column <= rect.right() / Tile; ++column) {
                const QRect part = rect & QRect(column * Tile, row * Tile, Tile, Tile);
                QRect &area = touched[tileKey(column, row)];
                area |= part;
            }
        }
        dirty |= rect;
    }
    // Each pixel is recomputed once per call, however many dabs covered it.
    for (const auto &[key, area] : touched)
        composite(image, area, before);
    return dirty;
}

void StrokeCompositor::rasterize(const Dab &dab, const QRect &rect)
{
    const qreal radius = dab.diameter / 2.0;
    const qreal inner = std::max(0.0, radius - 0.5);
    const qreal innerSq = inner * inner;
    const qreal outerSq = (radius + 0.5) * (radius + 0.5);
    for (int row = rect.top() / Tile; row <= rect.bottom() / Tile; ++row) {
        for (int column = rect.left() / Tile; column <= rect.right() / Tile; ++column) {
            const QRect part = rect & QRect(column * Tile, row * Tile, Tile, Tile);
            MaskTile &mask = maskTile(column, row);
            for (int y = part.top(); y <= part.bottom(); ++y) {
                const qreal dy = y + 0.5 - dab.center.y();
                quint8 *line = mask.data() + (y - row * Tile) * Tile;
                const int ox = column * Tile;
                for (int x = part.left(); x <= part.right(); ++x) {
                    const qreal dx = x + 0.5 - dab.center.x();
                    const qreal distSq = dx * dx + dy * dy;
                    if (distSq >= outerSq)
                        continue;
                    // Coverage of the pixel by the disc, approximated by its distance to the edge.
                    const int coverage = distSq <= innerSq
                        ? 255
                        : std::clamp(int(std::lround((radius + 0.5 - std::sqrt(distSq)) * 255.0)), 0, 255);
                    if (coverage > line[x - ox])
                        line[x - ox] = quint8(coverage);
                }
            }
        }
    }
}

void StrokeCompositor::composite(QImage &image, const QRect &rect, const TileSnapshot &before)
{
    const int column = rect.left() / Tile, row = rect.top() / Tile;
    const QImage *original = before.storedTile(column, row);
    Q_ASSERT(original); // addDabs captured every tile it touches
    if (!original)
        return;
    const MaskTile &mask = m_mask.at(tileKey(column, row));
    const int ox = column * Tile, oy = row * Tile;
    const quint32 cr = qRed(m_color), cg = qGreen(m_color), cb = qBlue(m_color);
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        QRgb *out = reinterpret_cast<QRgb *>(image.scanLine(y));
        const QRgb *src = reinterpret_cast<const QRgb *>(original->constScanLine(y - oy));
        const quint8 *cov = mask.data() + (y - oy) * Tile;
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const quint32 a = (quint32(cov[x - ox]) * quint32(m_opacity) + 128) >> 8; // 0..255
            const QRgb p = src[x - ox];
            if (a == 0) {
                out[x] = p;
                continue;
            }
            const quint32 keep = 255 - a;
            if (m_mode == BrushMode::Erase) {
                out[x] = qRgba(int(mul255(qRed(p), keep)), int(mul255(qGreen(p), keep)), int(mul255(qBlue(p), keep)),
                               int(mul255(qAlpha(p), keep)));
            } else {
                // Source-over with a premultiplied source (color × a, alpha a).
                out[x] = qRgba(int(mul255(cr, a) + mul255(qRed(p), keep)), int(mul255(cg, a) + mul255(qGreen(p), keep)),
                               int(mul255(cb, a) + mul255(qBlue(p), keep)), int(a + mul255(qAlpha(p), keep)));
            }
        }
    }
}

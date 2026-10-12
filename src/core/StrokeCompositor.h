#pragma once

#include "RasterDocument.h"
#include "StrokeSpacer.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <array>
#include <unordered_map>
#include <vector>

class TileSnapshot;

// Paints one stroke with uniform opacity.
//
// Each dab adds anti-aliased coverage to a per-stroke mask, keeping the maximum of overlapping
// dabs rather than their sum, so the dabs of one stroke never darken each other where they
// overlap (including where the stroke crosses itself). The changed pixels are then recomputed
// from the layer as it was before the stroke, which the stroke's undo snapshot already holds:
//   Paint: result = before composited under (color, opacity × coverage), source-over
//   Erase: result = before × (1 − opacity × coverage)
// A later stroke starts from the result, so separate strokes build up opacity. At 100 % opacity
// the stroke covers fully wherever a dab covers fully, as before.
class StrokeCompositor
{
public:
    StrokeCompositor(const QColor &color, qreal opacity, BrushMode mode);

    // Adds dabs to image. Tiles under each dab are captured into before (the stroke's undo
    // snapshot, also the source of the pre-stroke pixels) before anything changes. Returns the
    // changed area clipped to the image, or an empty rect.
    QRect addDabs(QImage &image, const std::vector<Dab> &dabs, TileSnapshot &before);

    // Mask tiles currently allocated (memory use is 4 KiB per 64 × 64 tile touched).
    int maskTileCount() const { return int(m_mask.size()); }

private:
    static constexpr int Tile = 64;
    using MaskTile = std::array<quint8, Tile * Tile>;

    MaskTile &maskTile(int column, int row);
    void rasterize(const Dab &dab, const QRect &rect);
    void composite(QImage &image, const QRect &rect, const TileSnapshot &before);

    quint32 m_color; // opaque, as ARGB32 (premultiplied equals straight for alpha 255)
    int m_opacity;   // 0..256
    BrushMode m_mode;
    std::unordered_map<qint64, MaskTile> m_mask;
};

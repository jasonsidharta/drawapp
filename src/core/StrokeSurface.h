#pragma once

#include "TileSnapshot.h"
#include "TiledImage.h"

#include <QImage>

// What StrokeEngine paints into: a dense QImage (scratchpad, thumbnails, tests, brush sheet) or
// a tiled document layer. The engine works one 64-pixel tile segment at a time, so it needs only
// row pointers that stay valid to the end of the segment (as libmypaint's tile requests; see
// research/m10-gegl-skia-comparison.md). Premultiplied ARGB32.
class StrokeSurface
{
public:
    virtual ~StrokeSurface() = default;
    virtual QRect rect() const = 0;
    virtual QRgb pixel(int x, int y) const = 0;
    // Read pointer to (x, y), valid to the end of its 64-pixel tile segment; nullptr = transparent.
    virtual const QRgb *constRow(int x, int y) const = 0;
    // Write pointer, same span; nullptr when memory for the tile cannot be had.
    virtual QRgb *writableRow(int x, int y) = 0;
    virtual void capture(TileSnapshot &before, const QRect &rect) const = 0;
    // Puts the snapshot's pixels back (cancel). Returns the area changed.
    virtual QRect restore(TileSnapshot &before) = 0;
};

class DenseSurface final : public StrokeSurface
{
public:
    explicit DenseSurface(QImage &image) : m_image(image) {}
    QRect rect() const override { return m_image.rect(); }
    QRgb pixel(int x, int y) const override { return reinterpret_cast<const QRgb *>(m_image.constScanLine(y))[x]; }
    const QRgb *constRow(int x, int y) const override
    {
        return reinterpret_cast<const QRgb *>(m_image.constScanLine(y)) + x;
    }
    QRgb *writableRow(int x, int y) override { return reinterpret_cast<QRgb *>(m_image.scanLine(y)) + x; }
    void capture(TileSnapshot &before, const QRect &r) const override { before.capture(m_image, r); }
    QRect restore(TileSnapshot &before) override { return before.swapWith(m_image); }

private:
    QImage &m_image;
};

class TiledSurface final : public StrokeSurface
{
public:
    explicit TiledSurface(TiledImage &image) : m_image(image) {}
    QRect rect() const override { return m_image.rect(); }
    QRgb pixel(int x, int y) const override { return m_image.pixel(x, y); }
    const QRgb *constRow(int x, int y) const override { return m_image.constRow(x, y); }
    QRgb *writableRow(int x, int y) override { return m_image.writableRow(x, y); }
    void capture(TileSnapshot &before, const QRect &r) const override { before.capture(m_image, r); }
    QRect restore(TileSnapshot &before) override { return before.swapWith(m_image); }

private:
    TiledImage &m_image;
};

#pragma once

#include "StrokeSpacer.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <vector>

class TileSnapshot;

enum class BrushMode { Paint, Erase };

// One raster in premultiplied ARGB32 (4 bytes per pixel). The static functions apply the same
// raster operations to any image, such as a layer of a LayeredDocument.
class RasterDocument
{
public:
    static constexpr int DefaultWidth = 2048;
    static constexpr int DefaultHeight = 1536;

    explicit RasterDocument(QSize size = QSize(DefaultWidth, DefaultHeight));
    // Takes the pixels of image (converted to premultiplied ARGB32 if needed).
    explicit RasterDocument(const QImage &image);

    const QImage &image() const { return m_image; }
    QSize size() const { return m_image.size(); }
    QRect bounds() const { return m_image.rect(); }

    QRect stampDabs(const std::vector<Dab> &dabs, const QColor &color, BrushMode mode,
                    TileSnapshot *before = nullptr)
    {
        return stampDabs(m_image, dabs, color, mode, before);
    }
    QRect clear(TileSnapshot *before = nullptr) { return clear(m_image, before); }

    static QImage blank(QSize size);
    // Stamps anti-aliased round dabs. Paint composites color over the pixels; Erase removes
    // alpha (destination-out). If before is given, the tiles under each dab are captured into
    // it first. Returns the changed area clipped to the image, or an empty rect.
    static QRect stampDabs(QImage &image, const std::vector<Dab> &dabs, const QColor &color, BrushMode mode,
                           TileSnapshot *before = nullptr);
    // Makes every pixel transparent, capturing painted tiles into before if given.
    static QRect clear(QImage &image, TileSnapshot *before = nullptr);

private:
    QImage m_image;
};

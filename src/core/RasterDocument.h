#pragma once

#include "StrokeSpacer.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <vector>

enum class BrushMode { Paint, Erase };

// One fixed-size raster in premultiplied ARGB32 (4 bytes per pixel), starting fully transparent.
class RasterDocument
{
public:
    static constexpr int DefaultWidth = 2048;
    static constexpr int DefaultHeight = 1536;

    explicit RasterDocument(QSize size = QSize(DefaultWidth, DefaultHeight));

    const QImage &image() const { return m_image; }
    QSize size() const { return m_image.size(); }
    QRect bounds() const { return m_image.rect(); }

    // Stamps anti-aliased round dabs. Paint composites color over the pixels; Erase removes
    // alpha (destination-out). Returns the changed area clipped to the document, or an empty rect.
    QRect stampDabs(const std::vector<Dab> &dabs, const QColor &color, BrushMode mode);

    // Makes every pixel transparent. Returns the whole document rect.
    QRect clear();

private:
    QImage m_image;
};

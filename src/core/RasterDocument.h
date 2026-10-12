#pragma once

#include "StrokeSpacer.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <vector>

class TileSnapshot;
class UndoHistory;

enum class BrushMode { Paint, Erase };

// One raster in premultiplied ARGB32 (4 bytes per pixel). New documents are 2048x1536 and fully
// transparent; opened documents keep their stored size.
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

    // Stamps anti-aliased round dabs. Paint composites color over the pixels; Erase removes
    // alpha (destination-out). If before is given, the tiles about to change are captured into
    // it first. Returns the changed area clipped to the document, or an empty rect.
    QRect stampDabs(const std::vector<Dab> &dabs, const QColor &color, BrushMode mode,
                    TileSnapshot *before = nullptr);

    // Makes every pixel transparent, capturing painted tiles into before if given.
    QRect clear(TileSnapshot *before = nullptr);

    QRect undo(UndoHistory &history);
    QRect redo(UndoHistory &history);

private:
    QImage m_image;
};

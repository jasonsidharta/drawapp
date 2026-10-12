#pragma once

#include "TiledImage.h"

#include <QImage>
#include <QRect>
#include <array>
#include <vector>

// Reduced-resolution copies of an image for drawing it zoomed out (milestone 10): level k is the
// image at 1 / 2^k of its size, each pixel the average of the 2 x 2 pixels of level k − 1 that lie
// inside it (premultiplied, so colour and coverage average correctly). Level 0 is the image
// itself and is never copied.
//
// Levels are built lazily, tile by tile, for the area being drawn, and rebuilt only where the
// image changed: every level tile remembers the stamps (TileBlock::stamp) of the four tiles it was
// made from. So the pyramid follows whatever image is drawn (the composite, a transform preview
// that shares most of its tiles) without being told what changed.
//
// It is a cache: its tiles are counted as MemoryBudget::Category::Cache, and the evictor it
// registers (on the constructing thread, the UI thread) drops it when memory is needed. Not
// thread-safe; used by the thread that draws.
class ViewPyramid
{
public:
    static constexpr int MaxLevel = 14;

    ViewPyramid();
    ~ViewPyramid();
    ViewPyramid(const ViewPyramid &) = delete;
    ViewPyramid &operator=(const ViewPyramid &) = delete;

    // The level to draw at a zoom: the smallest image that is still at least as detailed as the
    // screen (level k for zoom in (2^-(k+1), 2^-k]), so the view reduces it by less than half.
    // 0 when magnifying or above 50 %.
    static int levelFor(double zoom, QSize imageSize);
    static QSize levelSize(QSize imageSize, int level);

    // Level `level` of image, current at least over area (in that level's pixels). Returns
    // nullptr if memory for a tile was refused (what was built stays valid). For level 0 returns
    // &image.
    const TiledImage *level(const TiledImage &image, int level, const QRect &area);

    // Level `level` over area as one dense image (area in that level's pixels), for drawing. The
    // image points into a buffer kept between calls, which holds a tile-aligned area around the
    // request and copies again only the tiles whose stamps changed (so a repaint after a stroke
    // copies a few tiles, and small pans none). Valid until the next call. Null if memory was
    // refused.
    QImage pixels(const TiledImage &image, int level, const QRect &area);

    // Frees every level (they are rebuilt when next drawn). Returns the bytes freed.
    qint64 clear();
    // Frees the drawing buffer of pixels().
    qint64 dropBuffer();
    qint64 byteSize() const;
    // Tests and statistics: level tiles built (computed) since construction.
    qint64 tilesBuilt() const { return m_tilesBuilt; }

private:
    struct Level
    {
        TiledImage image;
        // Per tile: the stamps of the four source tiles it was built from; built = false until then.
        std::vector<std::array<quint64, 4>> sources;
        std::vector<bool> built;
    };

    bool ensure(const TiledImage &image, int level, int column, int row);
    const TiledImage &imageAt(const TiledImage &image, int level) const;
    void build(const TiledImage &below, TiledImage &out, int column, int row, bool *ok);

    QSize m_imageSize;
    std::vector<Level> m_levels; // m_levels[k − 1] is level k
    int m_evictor = -1;
    bool m_busy = false;       // building or reading: the evictor must not clear now
    QImage m_scratch;          // pixels() buffer (one long row of m_scratchBytes / 4 pixels)
    qint64 m_scratchBytes = 0; // reserved as Category::Cache
    // What the buffer holds: an area of one level of an image of this size, and per tile the
    // stamp of the pixels copied (~0 = not copied).
    bool m_bufferValid = false;
    int m_bufferLevel = -1;
    QSize m_bufferImageSize;
    QRect m_bufferArea;
    std::vector<quint64> m_bufferStamps;
    qint64 m_tilesBuilt = 0;
};

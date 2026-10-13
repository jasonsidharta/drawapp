#pragma once

#include "TiledImage.h"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <unordered_map>
#include <vector>

// The pixels an edit is about to change, kept so the edit can be undone (and redone). An edit
// captures the tiles it touches before changing them; swapping the snapshot with the image later
// exchanges old and new pixels, which is both undo and redo.
//
// Two forms, depending on the image (one snapshot never mixes them):
//  - dense QImage (scratchpad, thumbnails, tests): copies of 64 x 64 tiles;
//  - TiledImage (document layers, milestone 10): references to the layer's tiles. Capturing is
//    free; the layer copies a tile the first time it writes to it (copy-on-write), so only
//    changed tiles ever exist twice, and undo/redo exchange references instead of pixels.
class TileSnapshot
{
public:
    static constexpr int TileSize = 64;

    // --- dense images ---
    // Captures every tile touching rect that this snapshot does not hold yet.
    void capture(const QImage &image, const QRect &rect);
    // Captures every tile containing at least one pixel with non-zero alpha.
    void captureNonTransparent(const QImage &image);
    // Drops tiles whose stored pixels equal the image's current pixels.
    void dropUnchanged(const QImage &image);
    // Exchanges stored pixels with the image's pixels. Returns the bounds of the swapped tiles.
    QRect swapWith(QImage &image);

    // --- tiled images ---
    void capture(const TiledImage &image, const QRect &rect);
    // Every allocated tile (e.g. before clearing the whole layer).
    void captureAllocated(const TiledImage &image);
    // Drops tiles the image still shares, or whose pixels are equal (a missing tile equals a
    // transparent one).
    void dropUnchanged(const TiledImage &image);
    QRect swapWith(TiledImage &image);

    bool isEmpty() const { return m_tiles.empty() && m_refs.empty(); }
    // Union of the captured tiles' areas (whole tiles; may extend past the image's edge).
    QRect bounds() const;
    int tileCount() const { return int(m_tiles.size() + m_refs.size()); }
    // Memory this snapshot keeps alive: pixel copies, or referenced tiles (a referenced tile the
    // layer still shares is not counted: it would exist anyway).
    qsizetype byteSize() const { return m_bytes; }
    qsizetype reclaimableBytes() const;
    // The stored (pre-edit) pixel at (x, y): a pointer valid to the end of its 64-pixel tile
    // segment, or nullptr if that tile was not captured. A captured missing tile reads as
    // transparent.
    const QRgb *storedRow(int x, int y) const;
    // Dense form only: the stored tile image, or nullptr.
    const QImage *storedTile(int column, int row) const;

private:
    struct Tile
    {
        QPoint origin; // top-left pixel of the tile in the image
        QImage pixels;
    };
    struct Ref
    {
        int column;
        int row;
        TileRef tile;
    };

    void captureTile(const QImage &image, int column, int row);
    static QRect tileRect(const QImage &image, const QPoint &origin);

    std::vector<Tile> m_tiles;
    std::vector<Ref> m_refs;
    int m_refTileSize = 0;
    std::unordered_map<qint64, size_t> m_index; // tile key -> position in m_tiles or m_refs
    qsizetype m_bytes = 0;
};

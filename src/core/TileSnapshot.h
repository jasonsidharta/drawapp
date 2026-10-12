#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <unordered_set>
#include <vector>

// Copies of selected 64x64 tiles of an image. An edit captures the tiles it is about to change
// before changing them; swapping the snapshot with the image later exchanges old and new pixels,
// which is both undo and redo.
class TileSnapshot
{
public:
    static constexpr int TileSize = 64;

    // Captures every tile touching rect that this snapshot does not hold yet.
    void capture(const QImage &image, const QRect &rect);
    // Captures every tile containing at least one pixel with non-zero alpha.
    void captureNonTransparent(const QImage &image);
    // Drops tiles whose stored pixels equal the image's current pixels.
    void dropUnchanged(const QImage &image);
    // Exchanges stored pixels with the image's pixels. Returns the bounds of the swapped tiles.
    QRect swapWith(QImage &image);

    bool isEmpty() const { return m_tiles.empty(); }
    int tileCount() const { return int(m_tiles.size()); }
    qsizetype byteSize() const { return m_bytes; }

private:
    struct Tile
    {
        QPoint origin; // top-left pixel of the tile in the image
        QImage pixels;
    };

    void captureTile(const QImage &image, int column, int row);
    static QRect tileRect(const QImage &image, const QPoint &origin);

    std::vector<Tile> m_tiles;
    std::unordered_set<qint64> m_keys;
    qsizetype m_bytes = 0;
};

#include "TileSnapshot.h"

#include <cstring>

namespace {

qint64 tileKey(int column, int row)
{
    return (qint64(row) << 32) | quint32(column);
}

} // namespace

QRect TileSnapshot::tileRect(const QImage &image, const QPoint &origin)
{
    return QRect(origin, QSize(TileSize, TileSize)) & image.rect();
}

void TileSnapshot::capture(const QImage &image, const QRect &rect)
{
    const QRect area = rect & image.rect();
    if (area.isEmpty())
        return;
    for (int row = area.top() / TileSize; row <= area.bottom() / TileSize; ++row)
        for (int column = area.left() / TileSize; column <= area.right() / TileSize; ++column)
            captureTile(image, column, row);
}

void TileSnapshot::captureNonTransparent(const QImage &image)
{
    Q_ASSERT(image.format() == QImage::Format_ARGB32_Premultiplied);
    const int rows = (image.height() + TileSize - 1) / TileSize;
    const int columns = (image.width() + TileSize - 1) / TileSize;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const QRect r = tileRect(image, QPoint(column * TileSize, row * TileSize));
            bool hasPaint = false;
            for (int y = r.top(); y <= r.bottom() && !hasPaint; ++y) {
                const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = r.left(); x <= r.right(); ++x) {
                    if (qAlpha(line[x]) != 0) {
                        hasPaint = true;
                        break;
                    }
                }
            }
            if (hasPaint)
                captureTile(image, column, row);
        }
    }
}

void TileSnapshot::captureTile(const QImage &image, int column, int row)
{
    if (!m_index.emplace(tileKey(column, row), m_tiles.size()).second)
        return;
    const QPoint origin(column * TileSize, row * TileSize);
    Tile tile{origin, image.copy(tileRect(image, origin))};
    m_bytes += tile.pixels.sizeInBytes();
    m_tiles.push_back(std::move(tile));
}

const QImage *TileSnapshot::storedTile(int column, int row) const
{
    const auto it = m_index.find(tileKey(column, row));
    return it == m_index.end() ? nullptr : &m_tiles[it->second].pixels;
}

void TileSnapshot::dropUnchanged(const QImage &image)
{
    std::vector<Tile> kept;
    kept.reserve(m_tiles.size());
    m_index.clear();
    for (Tile &tile : m_tiles) {
        const QRect r = tileRect(image, tile.origin);
        const qsizetype rowBytes = qsizetype(r.width()) * 4;
        bool same = true;
        for (int y = 0; y < r.height() && same; ++y) {
            const uchar *current = image.constScanLine(r.top() + y) + qsizetype(r.left()) * 4;
            same = std::memcmp(current, tile.pixels.constScanLine(y), size_t(rowBytes)) == 0;
        }
        if (same) {
            m_bytes -= tile.pixels.sizeInBytes();
        } else {
            m_index.emplace(tileKey(tile.origin.x() / TileSize, tile.origin.y() / TileSize), kept.size());
            kept.push_back(std::move(tile));
        }
    }
    m_tiles = std::move(kept);
}

QRect TileSnapshot::swapWith(QImage &image)
{
    QRect changed;
    std::vector<uchar> row(size_t(TileSize) * 4);
    for (Tile &tile : m_tiles) {
        const QRect r = tileRect(image, tile.origin);
        const size_t rowBytes = size_t(r.width()) * 4;
        for (int y = 0; y < r.height(); ++y) {
            uchar *current = image.scanLine(r.top() + y) + qsizetype(r.left()) * 4;
            uchar *stored = tile.pixels.scanLine(y);
            std::memcpy(row.data(), current, rowBytes);
            std::memcpy(current, stored, rowBytes);
            std::memcpy(stored, row.data(), rowBytes);
        }
        changed |= r;
    }
    return changed;
}

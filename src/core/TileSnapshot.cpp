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
    if (!m_refs.empty())
        return nullptr;
    const auto it = m_index.find(tileKey(column, row));
    return it == m_index.end() ? nullptr : &m_tiles[it->second].pixels;
}

const QRgb *TileSnapshot::storedRow(int x, int y) const
{
    static const QRgb zeros[256] = {};
    if (m_refTileSize > 0) {
        const int shift = m_refTileSize == 64 ? 6 : (m_refTileSize == 128 ? 7 : 8);
        const auto it = m_index.find(tileKey(x >> shift, y >> shift));
        if (it == m_index.end())
            return nullptr;
        const TileBlock *b = m_refs[it->second].tile.get();
        if (!b)
            return zeros;
        const int mask = m_refTileSize - 1;
        return reinterpret_cast<const QRgb *>(b->data() + (y & mask) * m_refTileSize * 4) + (x & mask);
    }
    const auto it = m_index.find(tileKey(x / TileSize, y / TileSize));
    if (it == m_index.end())
        return nullptr;
    const Tile &t = m_tiles[it->second];
    return reinterpret_cast<const QRgb *>(t.pixels.constScanLine(y - t.origin.y())) + (x - t.origin.x());
}

QRect TileSnapshot::bounds() const
{
    QRect r;
    for (const Tile &t : m_tiles)
        r |= QRect(t.origin, t.pixels.size());
    for (const Ref &ref : m_refs)
        r |= QRect(ref.column * m_refTileSize, ref.row * m_refTileSize, m_refTileSize, m_refTileSize);
    return r;
}

void TileSnapshot::capture(const TiledImage &image, const QRect &rect)
{
    Q_ASSERT(m_tiles.empty());
    const QRect area = rect & image.rect();
    if (area.isEmpty())
        return;
    m_refTileSize = image.tileSize();
    for (int row = image.rowOf(area.top()); row <= image.rowOf(area.bottom()); ++row)
        for (int column = image.columnOf(area.left()); column <= image.columnOf(area.right()); ++column) {
            if (!m_index.emplace(tileKey(column, row), m_refs.size()).second)
                continue;
            const TileRef &t = image.tileRef(column, row);
            if (t)
                m_bytes += t.get()->bytes();
            m_refs.push_back({column, row, t});
        }
}

void TileSnapshot::captureAllocated(const TiledImage &image)
{
    Q_ASSERT(m_tiles.empty());
    m_refTileSize = image.tileSize();
    for (int row = 0; row < image.rows(); ++row)
        for (int column = 0; column < image.columns(); ++column) {
            const TileRef &t = image.tileRef(column, row);
            if (!t || !m_index.emplace(tileKey(column, row), m_refs.size()).second)
                continue;
            m_bytes += t.get()->bytes();
            m_refs.push_back({column, row, t});
        }
}

void TileSnapshot::dropUnchanged(const TiledImage &image)
{
    std::vector<Ref> kept;
    kept.reserve(m_refs.size());
    m_index.clear();
    m_bytes = 0;
    const int bytes = image.tileBytes();
    for (Ref &r : m_refs) {
        const TileRef &now = image.tileRef(r.column, r.row);
        bool same = now == r.tile;
        if (!same) {
            const uchar *a = r.tile ? r.tile.get()->data() : nullptr;
            const uchar *b = now ? now.get()->data() : nullptr;
            if (a && b) {
                same = std::memcmp(a, b, size_t(bytes)) == 0;
            } else {
                const uchar *p = a ? a : b;
                same = true;
                for (int i = 0; i < bytes && same; ++i)
                    same = p[i] == 0;
            }
        }
        if (same)
            continue;
        m_index.emplace(tileKey(r.column, r.row), kept.size());
        if (r.tile)
            m_bytes += r.tile.get()->bytes();
        kept.push_back(std::move(r));
    }
    m_refs = std::move(kept);
}

QRect TileSnapshot::swapWith(TiledImage &image)
{
    QRect changed;
    for (Ref &r : m_refs) {
        TileRef current = image.tileRef(r.column, r.row);
        image.setTileRef(r.column, r.row, r.tile);
        r.tile = std::move(current);
        changed |= image.tileRect(r.column, r.row);
    }
    m_bytes = 0;
    for (const Ref &r : m_refs)
        if (r.tile)
            m_bytes += r.tile.get()->bytes();
    return changed;
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

qsizetype TileSnapshot::reclaimableBytes() const
{
    qsizetype bytes = 0;
    for (const Ref &ref : m_refs)
        if (ref.tile && !ref.tile.get()->isShared())
            bytes += ref.tile.get()->bytes();
    return bytes;
}

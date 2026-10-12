#include "TiledImage.h"

#include <QPainter>
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

const TileRef &nullTile()
{
    static const TileRef none;
    return none;
}

bool allTransparent(const uchar *data, int bytes)
{
    // Premultiplied: a pixel is transparent exactly when all four bytes are zero.
    const quint64 *p = reinterpret_cast<const quint64 *>(data);
    for (int i = 0; i < bytes / 8; ++i)
        if (p[i])
            return false;
    return true;
}

int log2Of(int v)
{
    int s = 0;
    while ((1 << s) < v)
        ++s;
    return s;
}

} // namespace

// --- TileBlock ---

TileBlock *TileBlock::create(int bytes, MemoryBudget::Category category, const TileBlock *source)
{
    if (!MemoryBudget::tryReserve(bytes, category))
        return nullptr;
    void *raw = std::malloc(size_t(HeaderSize) + size_t(bytes));
    if (!raw) {
        MemoryBudget::release(bytes, category);
        return nullptr;
    }
    static_assert(sizeof(TileBlock) <= HeaderSize, "tile header too large");
    TileBlock *block = new (raw) TileBlock;
    block->m_owners.storeRelaxed(1);
    block->m_bytes = bytes;
    block->m_category = quint8(category);
    if (source)
        std::memcpy(block->data(), source->data(), size_t(bytes));
    else
        std::memset(block->data(), 0, size_t(bytes));
    return block;
}

void TileBlock::deref()
{
    if (!m_owners.deref()) {
        const int bytes = m_bytes;
        const auto category = MemoryBudget::Category(m_category);
        this->~TileBlock();
        std::free(this);
        MemoryBudget::release(bytes, category);
    }
}

// --- TiledImage ---

TiledImage::TiledImage(QSize size, int tileSize, MemoryBudget::Category category)
    : m_size(size.isValid() ? size : QSize())
    , m_tileSize(tileSize)
    , m_shift(log2Of(tileSize))
    , m_category(category)
{
    Q_ASSERT(tileSize >= 64 && (tileSize & (tileSize - 1)) == 0);
    if (m_size.isEmpty())
        return;
    m_columns = (m_size.width() + tileSize - 1) >> m_shift;
    m_rows = (m_size.height() + tileSize - 1) >> m_shift;
    m_tiles.resize(qsizetype(m_columns) * m_rows);
}

QRect TiledImage::tileRect(int column, int row) const
{
    return QRect(column * m_tileSize, row * m_tileSize, m_tileSize, m_tileSize) & rect();
}

const TileRef &TiledImage::tileRef(int column, int row) const
{
    if (column < 0 || row < 0 || column >= m_columns || row >= m_rows)
        return nullTile();
    return m_tiles.at(qsizetype(row) * m_columns + column);
}

const uchar *TiledImage::tileData(int column, int row) const
{
    const TileBlock *b = tileRef(column, row).get();
    return b ? b->data() : nullptr;
}

TileRef *TiledImage::mutableSlot(int column, int row)
{
    if (column < 0 || row < 0 || column >= m_columns || row >= m_rows)
        return nullptr;
    try {
        return &m_tiles[qsizetype(row) * m_columns + column]; // detaches a shared grid
    } catch (const std::bad_alloc &) {
        return nullptr;
    }
}

bool TiledImage::prepareTile(int column, int row)
{
    TileRef *slot = mutableSlot(column, row);
    if (!slot)
        return false;
    if (!*slot) {
        TileBlock *b = TileBlock::create(tileBytes(), m_category);
        if (!b)
            return false;
        *slot = TileRef(b);
    } else if (slot->get()->isShared()) {
        TileBlock *b = TileBlock::create(tileBytes(), m_category, slot->get());
        if (!b)
            return false;
        *slot = TileRef(b);
    }
    return true;
}

uchar *TiledImage::writableTileData(int column, int row)
{
    if (!prepareTile(column, row))
        return nullptr;
    ++m_version;
    return m_tiles[qsizetype(row) * m_columns + column].get()->data();
}

QImage TiledImage::tileImage(int column, int row) const
{
    const uchar *d = tileData(column, row);
    return d ? QImage(d, m_tileSize, m_tileSize, tileStride(), QImage::Format_ARGB32_Premultiplied) : QImage();
}

QImage TiledImage::writableTileImage(int column, int row)
{
    uchar *d = writableTileData(column, row);
    return d ? QImage(d, m_tileSize, m_tileSize, tileStride(), QImage::Format_ARGB32_Premultiplied) : QImage();
}

void TiledImage::releaseTile(int column, int row)
{
    if (!hasTile(column, row))
        return;
    if (TileRef *slot = mutableSlot(column, row)) {
        *slot = TileRef();
        ++m_version;
    }
}

bool TiledImage::sharesTileWith(const TiledImage &other, int column, int row) const
{
    const TileRef &a = tileRef(column, row);
    return a && a == other.tileRef(column, row);
}

void TiledImage::setTileRef(int column, int row, TileRef tile)
{
    Q_ASSERT(!tile || tile.get()->bytes() == tileBytes());
    if (TileRef *slot = mutableSlot(column, row)) {
        *slot = std::move(tile);
        ++m_version;
    }
}

int TiledImage::allocatedTileCount() const
{
    int n = 0;
    for (const TileRef &t : m_tiles)
        n += bool(t);
    return n;
}

qsizetype TiledImage::allocatedBytes() const
{
    return qsizetype(allocatedTileCount()) * tileBytes();
}

QRect TiledImage::allocatedBounds() const
{
    QRect r;
    forEachTile([&](int c, int row, const uchar *) { r |= tileRect(c, row); });
    return r;
}

int TiledImage::pruneTransparent(const QRect &area)
{
    const QRect a = area.isNull() ? rect() : (area & rect());
    if (a.isEmpty())
        return 0;
    int released = 0;
    for (int r = rowOf(a.top()); r <= rowOf(a.bottom()); ++r)
        for (int c = columnOf(a.left()); c <= columnOf(a.right()); ++c) {
            const TileBlock *b = tileRef(c, r).get();
            if (b && allTransparent(b->data(), b->bytes())) {
                releaseTile(c, r);
                ++released;
            }
        }
    return released;
}

QRect TiledImage::contentBounds() const
{
    QRect bounds;
    forEachTile([&](int c, int r, const uchar *data) {
        const QRect t = tileRect(c, r);
        int x0 = INT_MAX, y0 = INT_MAX, x1 = -1, y1 = -1;
        for (int y = 0; y < t.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(data + y * tileStride());
            for (int x = 0; x < t.width(); ++x)
                if (row[x]) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
        }
        if (x1 >= 0)
            bounds |= QRect(t.left() + x0, t.top() + y0, x1 - x0 + 1, y1 - y0 + 1);
    });
    return bounds;
}

QRgb TiledImage::pixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= m_size.width() || y >= m_size.height())
        return 0;
    const uchar *d = tileData(x >> m_shift, y >> m_shift);
    if (!d)
        return 0;
    const int mask = m_tileSize - 1;
    return reinterpret_cast<const QRgb *>(d + (y & mask) * tileStride())[x & mask];
}

bool TiledImage::setPixel(int x, int y, QRgb value)
{
    if (x < 0 || y < 0 || x >= m_size.width() || y >= m_size.height())
        return true;
    if (!value && !hasTile(x >> m_shift, y >> m_shift))
        return true;
    QRgb *row = writableRow(x, y);
    if (!row)
        return false;
    *row = value;
    return true;
}

const QRgb *TiledImage::constRow(int x, int y) const
{
    const uchar *d = tileData(x >> m_shift, y >> m_shift);
    if (!d)
        return nullptr;
    const int mask = m_tileSize - 1;
    return reinterpret_cast<const QRgb *>(d + (y & mask) * tileStride()) + (x & mask);
}

QRgb *TiledImage::writableRow(int x, int y)
{
    uchar *d = writableTileData(x >> m_shift, y >> m_shift);
    if (!d)
        return nullptr;
    const int mask = m_tileSize - 1;
    return reinterpret_cast<QRgb *>(d + (y & mask) * tileStride()) + (x & mask);
}

QImage TiledImage::copy(const QRect &requested) const
{
    const QRect r = requested.normalized();
    if (r.isEmpty())
        return QImage();
    const qint64 bytes = qint64(r.width()) * r.height() * BytesPerPixel;
    if (!MemoryBudget::tryReserve(bytes, MemoryBudget::Category::Temporary))
        return QImage();
    // The image's own buffer is not tracked after it is returned; the reservation only checks
    // that there is room for it now.
    MemoryBudget::release(bytes, MemoryBudget::Category::Temporary);
    QImage out(r.size(), QImage::Format_ARGB32_Premultiplied);
    if (out.isNull())
        return QImage();
    out.fill(Qt::transparent);
    const QRect inside = r & rect();
    if (inside.isEmpty())
        return out;
    for (int tr = rowOf(inside.top()); tr <= rowOf(inside.bottom()); ++tr)
        for (int tc = columnOf(inside.left()); tc <= columnOf(inside.right()); ++tc) {
            const uchar *d = tileData(tc, tr);
            if (!d)
                continue;
            const QRect part = tileRect(tc, tr) & inside;
            const int ox = part.left() - tc * m_tileSize, oy = part.top() - tr * m_tileSize;
            for (int y = 0; y < part.height(); ++y)
                std::memcpy(out.scanLine(part.top() - r.top() + y) + (part.left() - r.left()) * BytesPerPixel,
                            d + (oy + y) * tileStride() + ox * BytesPerPixel, size_t(part.width()) * BytesPerPixel);
        }
    return out;
}

QImage TiledImage::toImage() const
{
    return copy(rect());
}

bool TiledImage::write(const QPoint &pos, const QImage &image)
{
    if (image.isNull() || isNull())
        return true;
    const QImage src = image.format() == QImage::Format_ARGB32_Premultiplied
        ? image
        : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QRect target = QRect(pos, src.size()) & rect();
    if (target.isEmpty())
        return true;
    // Which tiles receive a non-transparent pixel or already exist: only those are written.
    const int c0 = columnOf(target.left()), c1 = columnOf(target.right());
    const int r0 = rowOf(target.top()), r1 = rowOf(target.bottom());
    std::vector<char> needed(size_t(c1 - c0 + 1) * (r1 - r0 + 1), 0);
    for (int tr = r0; tr <= r1; ++tr)
        for (int tc = c0; tc <= c1; ++tc) {
            char &need = needed[size_t(tr - r0) * (c1 - c0 + 1) + (tc - c0)];
            if (hasTile(tc, tr)) {
                need = 1;
                continue;
            }
            const QRect part = tileRect(tc, tr) & target;
            for (int y = part.top(); y <= part.bottom() && !need; ++y) {
                const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y - pos.y())) + (part.left() - pos.x());
                for (int x = 0; x < part.width(); ++x)
                    if (s[x]) {
                        need = 1;
                        break;
                    }
            }
        }
    // Phase 1: allocate or detach every tile to be written (changes no pixel).
    for (int tr = r0; tr <= r1; ++tr)
        for (int tc = c0; tc <= c1; ++tc)
            if (needed[size_t(tr - r0) * (c1 - c0 + 1) + (tc - c0)] && !prepareTile(tc, tr))
                return false;
    // Phase 2: copy pixels.
    for (int tr = r0; tr <= r1; ++tr)
        for (int tc = c0; tc <= c1; ++tc) {
            if (!needed[size_t(tr - r0) * (c1 - c0 + 1) + (tc - c0)])
                continue;
            uchar *d = m_tiles[qsizetype(tr) * m_columns + tc].get()->data();
            const QRect part = tileRect(tc, tr) & target;
            const int ox = part.left() - tc * m_tileSize, oy = part.top() - tr * m_tileSize;
            for (int y = 0; y < part.height(); ++y)
                std::memcpy(d + (oy + y) * tileStride() + ox * BytesPerPixel,
                            src.constScanLine(part.top() - pos.y() + y) + (part.left() - pos.x()) * BytesPerPixel,
                            size_t(part.width()) * BytesPerPixel);
        }
    ++m_version;
    return true;
}

TiledImage TiledImage::fromImage(const QImage &image, int tileSize, MemoryBudget::Category category)
{
    TiledImage out(image.size(), tileSize, category);
    if (!out.write(QPoint(0, 0), image))
        return TiledImage();
    return out;
}

void TiledImage::clear()
{
    if (allocatedTileCount() == 0)
        return;
    m_tiles = QList<TileRef>(m_tiles.size());
    ++m_version;
}

bool TiledImage::fillRect(const QRect &area, QRgb value)
{
    const QRect a = area & rect();
    if (a.isEmpty())
        return true;
    const int c0 = columnOf(a.left()), c1 = columnOf(a.right()), r0 = rowOf(a.top()), r1 = rowOf(a.bottom());
    if (!value) {
        // Phase 1: detach every partly covered tile that exists (changes no pixel).
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c) {
                const QRect t = tileRect(c, r);
                if (hasTile(c, r) && (t & a) != t && !prepareTile(c, r))
                    return false;
            }
        // Phase 2: release covered tiles, clear the covered part of the others.
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c) {
                if (!hasTile(c, r))
                    continue;
                const QRect t = tileRect(c, r);
                if ((t & a) == t) {
                    releaseTile(c, r);
                    continue;
                }
                uchar *d = m_tiles[qsizetype(r) * m_columns + c].get()->data();
                const QRect part = t & a;
                for (int y = part.top(); y <= part.bottom(); ++y)
                    std::memset(d + (y - r * m_tileSize) * tileStride() + (part.left() - c * m_tileSize) * BytesPerPixel, 0,
                                size_t(part.width()) * BytesPerPixel);
            }
        ++m_version;
        return true;
    }
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c)
            if (!prepareTile(c, r))
                return false;
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) {
            uchar *d = m_tiles[qsizetype(r) * m_columns + c].get()->data();
            const QRect part = tileRect(c, r) & a;
            for (int y = part.top(); y <= part.bottom(); ++y) {
                QRgb *row = reinterpret_cast<QRgb *>(d + (y - r * m_tileSize) * tileStride()) + (part.left() - c * m_tileSize);
                std::fill(row, row + part.width(), value);
            }
        }
    ++m_version;
    return true;
}

bool TiledImage::operator==(const TiledImage &o) const
{
    if (m_size != o.m_size)
        return false;
    if (m_tileSize == o.m_tileSize) {
        for (int r = 0; r < m_rows; ++r)
            for (int c = 0; c < m_columns; ++c) {
                const TileBlock *a = tileRef(c, r).get(), *b = o.tileRef(c, r).get();
                if (a == b)
                    continue;
                if (!a || !b) {
                    if (!allTransparent((a ? a : b)->data(), tileBytes()))
                        return false;
                } else if (std::memcmp(a->data(), b->data(), size_t(tileBytes())) != 0) {
                    return false;
                }
            }
        return true;
    }
    for (int y = 0; y < height(); ++y)
        for (int x = 0; x < width(); ++x)
            if (pixel(x, y) != o.pixel(x, y))
                return false;
    return true;
}

bool TiledImage::operator==(const QImage &other) const
{
    if (other.size() != m_size)
        return false;
    const QImage img = other.format() == QImage::Format_ARGB32_Premultiplied
        ? other
        : other.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int c = 0; c < m_columns; ++c) {
            const QRect t = tileRect(c, rowOf(y));
            const QRgb *tile = constRow(t.left(), y);
            if (tile) {
                if (std::memcmp(tile, row + t.left(), size_t(t.width()) * BytesPerPixel) != 0)
                    return false;
            } else {
                for (int x = t.left(); x <= t.right(); ++x)
                    if (row[x])
                        return false;
            }
        }
    }
    return true;
}

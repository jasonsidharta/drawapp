#include "ViewPyramid.h"
#include "BudgetImage.h"
#include <QScopeGuard>

#include <algorithm>
#include <climits>
#include <cstring>

ViewPyramid::ViewPyramid()
{
    m_evictor = MemoryBudget::addEvictor([this](qint64) { return m_busy ? 0 : clear() + dropBuffer(); });
}

ViewPyramid::~ViewPyramid()
{
    MemoryBudget::removeEvictor(m_evictor);
    clear();
    dropBuffer();
}

qint64 ViewPyramid::dropBuffer()
{
    const qint64 bytes = m_scratchBytes;
    m_bufferValid = false;
    m_bufferStamps.clear();
    m_scratch = QImage();
    m_scratchBytes = 0;
    return bytes;
}

QSize ViewPyramid::levelSize(QSize size, int level)
{
    for (int k = 0; k < level; ++k)
        size = QSize((size.width() + 1) / 2, (size.height() + 1) / 2);
    return size;
}

int ViewPyramid::levelFor(double zoom, QSize imageSize)
{
    int k = 0;
    while (k < MaxLevel && zoom * double(qint64(1) << (k + 1)) <= 1.0) {
        const QSize current = levelSize(imageSize, k);
        if (current.width() <= 1 && current.height() <= 1)
            break; // one pixel: nothing smaller
        ++k;
    }
    return k;
}

const TiledImage &ViewPyramid::imageAt(const TiledImage &image, int level) const
{
    return level == 0 ? image : m_levels[size_t(level - 1)].image;
}

const TiledImage *ViewPyramid::level(const TiledImage &image, int level, const QRect &area)
{
    if (image.isNull())
        return nullptr;
    if (image.size() != m_imageSize || (!m_levels.empty() && m_levels.front().image.tileSize() != image.tileSize())) {
        clear();
        m_imageSize = image.size();
    }
    level = std::clamp(level, 0, MaxLevel);
    if (level == 0)
        return &image;
    while (int(m_levels.size()) < level) {
        Level l;
        l.image = TiledImage(levelSize(m_imageSize, int(m_levels.size()) + 1), image.tileSize(),
                             MemoryBudget::Category::Cache);
        const size_t tiles = size_t(l.image.columns()) * size_t(l.image.rows());
        l.sources.assign(tiles, {});
        l.built.assign(tiles, false);
        m_levels.push_back(std::move(l));
    }
    const TiledImage &out = m_levels[size_t(level - 1)].image;
    const QRect a = area & out.rect();
    if (a.isEmpty())
        return &out;
    const bool wasBusy = m_busy;
    const auto restoreBusy = qScopeGuard([&] { m_busy = wasBusy; });
    m_busy = true; // tile allocations below must not let the evictor clear what is being built
    bool ok = true;
    for (int r = out.rowOf(a.top()); r <= out.rowOf(a.bottom()) && ok; ++r)
        for (int c = out.columnOf(a.left()); c <= out.columnOf(a.right()) && ok; ++c)
            ok = ensure(image, level, c, r);
    m_busy = wasBusy;
    return ok ? &out : nullptr;
}

bool ViewPyramid::ensure(const TiledImage &image, int level, int column, int row)
{
    if (level == 0)
        return true;
    Level &l = m_levels[size_t(level - 1)];
    const TiledImage &below = imageAt(image, level - 1);
    std::array<quint64, 4> stamps{};
    for (int i = 0; i < 4; ++i) {
        const int c = 2 * column + (i & 1), r = 2 * row + (i >> 1);
        if (c >= below.columns() || r >= below.rows())
            continue;
        if (!ensure(image, level - 1, c, r))
            return false;
        stamps[size_t(i)] = below.tileStamp(c, r);
    }
    const size_t index = size_t(row) * size_t(l.image.columns()) + size_t(column);
    if (l.built[index] && l.sources[index] == stamps)
        return true;
    bool ok = true;
    build(below, l.image, column, row, &ok);
    if (!ok)
        return false;
    l.sources[index] = stamps;
    l.built[index] = true;
    ++m_tilesBuilt;
    return true;
}

void ViewPyramid::build(const TiledImage &below, TiledImage &out, int column, int row, bool *ok)
{
    const int ts = out.tileSize();
    bool any = false;
    for (int i = 0; i < 4 && !any; ++i) {
        const int c = 2 * column + (i & 1), r = 2 * row + (i >> 1);
        any = c < below.columns() && r < below.rows() && below.hasTile(c, r);
    }
    if (!any) {
        out.releaseTile(column, row);
        return;
    }
    uchar *dst = out.writableTileData(column, row);
    if (!dst) {
        *ok = false;
        return;
    }
    const QRect t = out.tileRect(column, row);
    const int bw = below.width(), bh = below.height();
    for (int y = 0; y < t.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(dst + qsizetype(y) * out.tileStride());
        const int sy = 2 * (t.top() + y);
        const QRgb *rows[2] = {nullptr, nullptr}; // from segX to the end of its tile; nullptr = transparent
        const bool inside[2] = {true, sy + 1 < bh};
        int segX = 0;
        for (int x = 0; x < t.width(); ++x) {
            const int sx = 2 * (t.left() + x);
            if (x == 0 || (sx & (ts - 1)) == 0) { // a new tile of `below` (sx is even: sx + 1 is in it too)
                segX = sx;
                for (int k = 0; k < 2; ++k)
                    rows[k] = inside[k] ? below.constRow(sx, sy + k) : nullptr;
            }
            const bool right = sx + 1 < bw;
            quint32 sum[4] = {0, 0, 0, 0};
            int n = 0;
            for (int k = 0; k < 2; ++k) {
                if (!inside[k])
                    continue;
                const QRgb *src = rows[k]; // nullptr = transparent tile
                for (int d = 0; d < (right ? 2 : 1); ++d) {
                    ++n;
                    if (!src)
                        continue;
                    const QRgb p = src[sx - segX + d];
                    sum[0] += qAlpha(p);
                    sum[1] += qRed(p);
                    sum[2] += qGreen(p);
                    sum[3] += qBlue(p);
                }
            }
            const quint32 h = quint32(n) / 2;
            line[x] = qRgba(int((sum[1] + h) / quint32(n)), int((sum[2] + h) / quint32(n)), int((sum[3] + h) / quint32(n)),
                            int((sum[0] + h) / quint32(n)));
        }
    }
}

QImage ViewPyramid::pixels(const TiledImage &image, int level, const QRect &area)
{
    MemoryBudget::PreserveHistory preserve;
    level = std::clamp(level, 0, MaxLevel);
    const QRect levelRect(QPoint(0, 0), levelSize(image.size(), level));
    const QRect a = area & levelRect;
    if (a.isEmpty())
        return QImage();
    const int ts = image.tileSize();
    // The buffer keeps a tile-aligned area around the request (one tile of margin, so small pans
    // reuse it) and, per tile, the stamp of the pixels it holds: later calls copy only tiles that
    // changed. A different level, image size or an area outside it starts it again.
    if (!m_bufferValid || level != m_bufferLevel || image.size() != m_bufferImageSize || !m_bufferArea.contains(a)) {
        const QRect aligned(QPoint((a.left() / ts - 1) * ts, (a.top() / ts - 1) * ts),
                            QPoint(((a.right() / ts) + 2) * ts - 1, ((a.bottom() / ts) + 2) * ts - 1));
        const QRect buffer = aligned & levelRect;
        // The buffer first (growing it may evict the levels, which are not in use yet).
        const qint64 needed = qint64(buffer.width()) * buffer.height() * 4;
        m_bufferValid = false;
        if (needed > m_scratchBytes) {
            dropBuffer(); // no delta can become stale if reservation evicts this cache
            m_scratch = BudgetImage::create(QSize(int(std::min<qint64>(needed / 4, INT_MAX)), 1),
                                             QImage::Format_ARGB32_Premultiplied, MemoryBudget::Category::Cache);
            if (m_scratch.isNull())
                return QImage();
            m_scratchBytes = needed;
        }
        m_bufferArea = buffer;
        m_bufferLevel = level;
        m_bufferImageSize = image.size();
        const int columns = (buffer.right() / ts) - (buffer.left() / ts) + 1;
        const int rows = (buffer.bottom() / ts) - (buffer.top() / ts) + 1;
        m_bufferStamps.assign(size_t(columns) * size_t(rows), ~quint64(0)); // nothing copied yet
        m_bufferValid = true;
    }
    const TiledImage *source = this->level(image, level, a);
    if (!source)
        return QImage();
    const bool wasBusy = m_busy;
    const auto restoreBusy = qScopeGuard([&] { m_busy = wasBusy; });
    m_busy = true;
    uchar *bits = m_scratch.bits();
    const qsizetype bpl = qsizetype(m_bufferArea.width()) * 4;
    const int c0 = m_bufferArea.left() / ts, r0 = m_bufferArea.top() / ts;
    const int columns = (m_bufferArea.right() / ts) - c0 + 1;
    for (int r = a.top() / ts; r <= a.bottom() / ts; ++r)
        for (int col = a.left() / ts; col <= a.right() / ts; ++col) {
            quint64 &held = m_bufferStamps[size_t(r - r0) * size_t(columns) + size_t(col - c0)];
            const quint64 now = source->tileStamp(col, r);
            if (held == now)
                continue;
            const QRect t = source->tileRect(col, r) & m_bufferArea;
            source->readInto(t, bits + qsizetype(t.top() - m_bufferArea.top()) * bpl + qsizetype(t.left() - m_bufferArea.left()) * 4,
                             bpl);
            held = now;
        }
    m_busy = wasBusy;
    const uchar *origin = bits + qsizetype(a.top() - m_bufferArea.top()) * bpl + qsizetype(a.left() - m_bufferArea.left()) * 4;
    return QImage(origin, a.width(), a.height(), bpl, QImage::Format_ARGB32_Premultiplied);
}

qint64 ViewPyramid::clear()
{
    const qint64 bytes = byteSize();
    m_levels.clear();
    return bytes;
}

qint64 ViewPyramid::byteSize() const
{
    qint64 bytes = 0;
    for (const Level &l : m_levels)
        bytes += l.image.allocatedBytes();
    return bytes;
}

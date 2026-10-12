#pragma once

#include "MemoryBudget.h"

#include <QAtomicInt>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>

// One tile's pixels, reference-counted. Immutable while it has more than one owner (see
// research/m10-concurrency-design.md): writers copy a shared tile before changing it.
class TileBlock
{
public:
    // Nullptr when the memory budget or the allocator refuses. Zero-filled (transparent) unless
    // copying from `source`.
    static TileBlock *create(int bytes, MemoryBudget::Category category, const TileBlock *source = nullptr);

    uchar *data() { return reinterpret_cast<uchar *>(this) + HeaderSize; }
    const uchar *data() const { return reinterpret_cast<const uchar *>(this) + HeaderSize; }
    int bytes() const { return m_bytes; }
    bool isShared() const { return m_owners.loadAcquire() != 1; }
    void ref() { m_owners.ref(); }
    // Drops one owner; frees the block (and its budget) with the last one.
    void deref();

private:
    static constexpr int HeaderSize = 16;
    TileBlock() = default;
    QAtomicInt m_owners;
    int m_bytes = 0;
    quint8 m_category = 0;
};

// A smart pointer to a TileBlock (one owner per TileRef). Null means "transparent".
class TileRef
{
public:
    TileRef() = default;
    explicit TileRef(TileBlock *adopt) : m_block(adopt) {}
    TileRef(const TileRef &o) : m_block(o.m_block)
    {
        if (m_block)
            m_block->ref();
    }
    TileRef(TileRef &&o) noexcept : m_block(o.m_block) { o.m_block = nullptr; }
    TileRef &operator=(TileRef o) noexcept
    {
        std::swap(m_block, o.m_block);
        return *this;
    }
    ~TileRef()
    {
        if (m_block)
            m_block->deref();
    }
    TileBlock *get() const { return m_block; }
    explicit operator bool() const { return m_block != nullptr; }
    bool operator==(const TileRef &o) const { return m_block == o.m_block; }
    bool operator!=(const TileRef &o) const { return m_block != o.m_block; }

private:
    TileBlock *m_block = nullptr;
};

// A sparse, copy-on-write raster image of premultiplied ARGB32 pixels (milestone 10,
// docs/m10-tile-storage.md).
//
// Pixels are kept in square tiles of tileSize() (a power of two, at least 64) on a dense grid
// covering the image. A missing tile reads as transparent, so an empty image costs only its grid
// of null pointers. Writing allocates or copies just the tile written to.
//
// Copies are cheap: the grid is implicitly shared and every tile is reference-counted. The first
// write on either copy detaches the grid, and a tile shared with another copy is duplicated
// before it changes. A copy is therefore an immutable snapshot that other threads may read while
// the original keeps changing on its own thread. A single TiledImage object must not be used by
// two threads at once.
//
// Pixels outside the image (in edge tiles that extend past it) are always zero. Coordinates
// outside the image read as transparent and are ignored by writes.
class TiledImage
{
public:
    // The tile size for document layers, chosen by measurement (docs/m10-tile-storage.md).
    static constexpr int DefaultTileSize = 64;
    static constexpr int BytesPerPixel = 4;

    TiledImage() = default;
    explicit TiledImage(QSize size, int tileSize = DefaultTileSize,
                        MemoryBudget::Category category = MemoryBudget::Category::Document);

    bool isNull() const { return m_size.isEmpty(); }
    QSize size() const { return m_size; }
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }
    QRect rect() const { return QRect(QPoint(0, 0), m_size); }
    int tileSize() const { return m_tileSize; }
    int columns() const { return m_columns; }
    int rows() const { return m_rows; }
    MemoryBudget::Category category() const { return m_category; }
    // Bytes per tile row.
    int tileStride() const { return m_tileSize * BytesPerPixel; }
    int tileBytes() const { return m_tileSize * m_tileSize * BytesPerPixel; }

    // Changes every time pixels may have changed through this object (copies keep the number they
    // had when copied, then count on their own).
    quint64 version() const { return m_version; }

    // --- tiles ---
    int columnOf(int x) const { return x >> m_shift; }
    int rowOf(int y) const { return y >> m_shift; }
    // The tile's pixels inside the image.
    QRect tileRect(int column, int row) const;
    bool hasTile(int column, int row) const { return bool(tileRef(column, row)); }
    // Nullptr for a missing (transparent) tile.
    const uchar *tileData(int column, int row) const;
    // The tile's pixels for writing: allocated (transparent) or copied if shared. Nullptr if the
    // memory budget refuses; the image is then unchanged.
    uchar *writableTileData(int column, int row);
    // The tile as an image for QPainter (no copy). Read-only / writable as above; a missing tile
    // gives a null image for reading.
    QImage tileImage(int column, int row) const;
    QImage writableTileImage(int column, int row);
    void releaseTile(int column, int row);
    bool sharesTileWith(const TiledImage &other, int column, int row) const;
    const TileRef &tileRef(int column, int row) const;
    // Replaces a tile (null = transparent). The tile must have this image's tile size.
    void setTileRef(int column, int row, TileRef tile);
    int allocatedTileCount() const;
    // Bytes of this image's tiles (a tile shared with another image is counted in both).
    qsizetype allocatedBytes() const;
    // Union of the allocated tiles' rects (empty if none).
    QRect allocatedBounds() const;
    // Calls f(column, row, const uchar *data) for every allocated tile.
    template <typename F>
    void forEachTile(F &&f) const
    {
        for (int r = 0; r < m_rows; ++r)
            for (int c = 0; c < m_columns; ++c)
                if (const TileBlock *b = m_tiles.at(r * m_columns + c).get())
                    f(c, r, b->data());
    }

    // Releases tiles in area (all if area is null) whose pixels are all transparent. Returns how
    // many were released.
    int pruneTransparent(const QRect &area = QRect());
    // Bounds of non-transparent pixels (exact, scans allocated tiles).
    QRect contentBounds() const;

    // --- pixels ---
    QRgb pixel(int x, int y) const;
    bool setPixel(int x, int y, QRgb value);
    // Pointer to pixel (x, y) inside its tile, valid up to the tile's right edge; nullptr when the
    // tile is missing (read) or cannot be allocated (write). (x, y) must be inside the image.
    const QRgb *constRow(int x, int y) const;
    QRgb *writableRow(int x, int y);

    // Dense copies (transparent where unallocated or outside the image). Null if the budget or
    // the allocator refuses the buffer.
    QImage toImage() const;
    QImage copy(const QRect &rect) const;
    // Writes src (converted to premultiplied ARGB32) at pos, clipped to the image. Transparent
    // source pixels over missing tiles allocate nothing. All tiles are prepared before any pixel
    // changes, so on failure (false) the image is unchanged.
    bool write(const QPoint &pos, const QImage &src);
    // A tiled copy of a dense image; fully transparent tiles stay unallocated. Null on failure.
    static TiledImage fromImage(const QImage &image, int tileSize = DefaultTileSize,
                                MemoryBudget::Category category = MemoryBudget::Category::Document);
    // Every tile released (transparent).
    void clear();
    // Fills rect with one premultiplied colour (transparent releases whole tiles). False if the
    // budget refuses (unchanged).
    bool fillRect(const QRect &rect, QRgb value);

    // Pixel equality (a missing tile equals a transparent one).
    bool operator==(const TiledImage &other) const;
    bool operator!=(const TiledImage &other) const { return !(*this == other); }
    bool operator==(const QImage &other) const;
    bool operator!=(const QImage &other) const { return !(*this == other); }

private:
    TileRef *mutableSlot(int column, int row); // detaches the grid; nullptr on failure
    bool prepareTile(int column, int row);      // allocate/detach for writing

    QSize m_size;
    int m_tileSize = 0;
    int m_shift = 0;
    int m_columns = 0;
    int m_rows = 0;
    MemoryBudget::Category m_category = MemoryBudget::Category::Document;
    quint64 m_version = 0;
    QList<TileRef> m_tiles; // row-major, columns × rows; shared between copies until written
};

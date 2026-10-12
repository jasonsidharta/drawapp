#include "ProjectFile.h"

#include "Parallel.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr char Magic[8] = {'D', 'R', 'A', 'W', 'A', 'P', 'P', '\n'};
constexpr quint32 PixelFormatRgba8Premultiplied = 1;
constexpr quint32 CompressionZlib = 1;
constexpr qint64 MiB = 1024 * 1024;

bool isContentUri(const QString &path)
{
    return path.startsWith(QLatin1String("content:"), Qt::CaseInsensitive);
}

QString displayName(const QString &path)
{
    const QString name = isContentUri(path) ? QUrl(path).fileName() : QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

ProjectFile::LoadResult failure(const QString &message)
{
    ProjectFile::LoadResult r;
    r.error = message;
    return r;
}

QString damaged(const QString &detail)
{
    return QStringLiteral("The file is damaged or incomplete (%1).").arg(detail);
}

QString outOfMemory()
{
    return QStringLiteral("Not enough memory to open this project: its layers need more than %1 allows.")
        .arg(MemoryBudget::shortageText());
}

// dense: versions 1 and 2, which hold whole layers in memory.
QString tooLarge(quint32 w, quint32 h, quint32 layers, bool dense)
{
    QString message = QStringLiteral("The canvas is too large to open (%1 × %2 pixels, %3 layer(s)). Limits: %4 pixels "
                                     "per side, %5 megapixels per layer, %6 layers")
                          .arg(w).arg(h).arg(layers)
                          .arg(DocumentLimits::MaxSide)
                          .arg(DocumentLimits::MaxLayerPixels / MiB)
                          .arg(DocumentLimits::MaxLayers);
    if (dense)
        message += QStringLiteral(", and %1 MiB of layer pixels in total for projects saved before milestone 10")
                       .arg(DocumentLimits::MaxDensePixelBytes / MiB);
    return message + QLatin1Char('.');
}

bool sizeWithinLimits(quint32 w, quint32 h, quint32 layers, bool dense)
{
    if (w > quint32(DocumentLimits::MaxSide) || h > quint32(DocumentLimits::MaxSide))
        return false;
    if (qint64(w) * h > DocumentLimits::MaxLayerPixels || layers > quint32(DocumentLimits::MaxLayers))
        return false;
    return !dense || qint64(w) * h * 4 * layers <= DocumentLimits::MaxDensePixelBytes;
}

bool validPremultiplied(const uchar *pixels, qsizetype bytes)
{
    // Alpha is the fourth byte in both RGBA (versions 1, 2) and BGRA (version 3).
    for (qsizetype i = 0; i < bytes; i += 4) {
        const uchar a = pixels[i + 3];
        if (pixels[i] > a || pixels[i + 1] > a || pixels[i + 2] > a)
            return false;
    }
    return true;
}

// Version 3 stores B, G, R, A bytes: ARGB32 words in little-endian order.
void swapToFileOrder([[maybe_unused]] uchar *pixels, [[maybe_unused]] qsizetype bytes)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    for (qsizetype i = 0; i < bytes; i += 4)
        qToLittleEndian(qFromUnaligned<quint32>(pixels + i), pixels + i);
#endif
}

// --- versions 1 and 2: whole layers ---

QByteArray compressLayer(const QImage &image)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (rgba.isNull())
        return {};
    const qsizetype rowBytes = qsizetype(rgba.width()) * 4;
    QByteArray raw(rowBytes * rgba.height(), Qt::Uninitialized);
    for (int y = 0; y < rgba.height(); ++y)
        std::memcpy(raw.data() + rowBytes * y, rgba.constScanLine(y), size_t(rowBytes));
    return qCompress(raw);
}

// Decodes one compressed layer block. The declared raw size is checked before qUncompress
// allocates anything.
QImage decompressLayer(QByteArrayView block, int width, int height, QString *error)
{
    const qsizetype rawSize = qsizetype(width) * height * 4;
    if (block.size() < 4 || qFromBigEndian<quint32>(block.data()) != quint32(rawSize)) {
        *error = damaged(QStringLiteral("pixel data size does not match the canvas size"));
        return {};
    }
    const QByteArray raw = qUncompress(reinterpret_cast<const uchar *>(block.data()), block.size());
    if (raw.size() != rawSize) {
        *error = damaged(QStringLiteral("pixel data cannot be decompressed"));
        return {};
    }
    if (!validPremultiplied(reinterpret_cast<const uchar *>(raw.constData()), rawSize)) {
        *error = damaged(QStringLiteral("invalid pixel values"));
        return {};
    }
    const QImage rgba(reinterpret_cast<const uchar *>(raw.constData()), width, height, qsizetype(width) * 4,
                      QImage::Format_RGBA8888_Premultiplied);
    QImage image = rgba.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        *error = QStringLiteral("Not enough memory to open a %1 × %2 canvas.").arg(width).arg(height);
    return image;
}

// --- version 3: tiles ---

// Tiles are coded in parallel, a chunk at a time, so the memory held at once stays bounded.
constexpr int TileChunk = 512;

// One layer's tile stream (see ProjectFile.h). Transparent tiles are not stored. Tiles are
// compressed in parallel; the stream is the same as compressing them one by one.
QByteArray encodeTiles(const TiledImage &image, int *count)
{
    struct Tile
    {
        int column, row;
        const uchar *data;
    };
    std::vector<Tile> tiles;
    image.forEachTile([&](int column, int row, const uchar *data) { tiles.push_back({column, row, data}); });
    QByteArray block(4, '\0');
    *count = 0;
    std::vector<QByteArray> compressed;
    for (size_t first = 0; first < tiles.size(); first += TileChunk) {
        const int n = int(std::min<size_t>(TileChunk, tiles.size() - first));
        compressed.assign(size_t(n), QByteArray());
        parallelFor(0, n, 4, [&](int i) {
            const Tile &t = tiles[first + size_t(i)];
            const QRect r = image.tileRect(t.column, t.row);
            const qsizetype rowBytes = qsizetype(r.width()) * 4;
            QByteArray raw(rowBytes * r.height(), Qt::Uninitialized);
            for (int y = 0; y < r.height(); ++y)
                std::memcpy(raw.data() + rowBytes * y, t.data + qsizetype(y) * image.tileStride(), size_t(rowBytes));
            if (std::all_of(raw.cbegin(), raw.cend(), [](char ch) { return ch == 0; }))
                return; // transparent: not stored
            swapToFileOrder(reinterpret_cast<uchar *>(raw.data()), raw.size());
            compressed[size_t(i)] = qCompress(raw);
        });
        for (int i = 0; i < n; ++i) {
            const QByteArray &data = compressed[size_t(i)];
            if (data.isEmpty())
                continue;
            const Tile &t = tiles[first + size_t(i)];
            char entry[12];
            qToLittleEndian(quint32(t.column), entry);
            qToLittleEndian(quint32(t.row), entry + 4);
            qToLittleEndian(quint32(data.size()), entry + 8);
            block.append(entry, sizeof(entry));
            block.append(data);
            ++*count;
        }
    }
    qToLittleEndian(quint32(*count), block.data());
    return block;
}

// Reads one layer's tile stream into out (a transparent layer of the canvas size), or only checks
// it when out is null. Returns an empty string or the reason it was refused. The entries are
// checked in order first (positions, order, sizes); then tiles are decompressed and checked in
// parallel, a chunk at a time, and written in order on this thread.
QString decodeTiles(QByteArrayView block, int tileSize, QSize canvas, qint64 expectedCount, TiledImage *out)
{
    if (block.size() < 4)
        return damaged(QStringLiteral("invalid layer data size"));
    const qint64 columns = (canvas.width() + tileSize - 1) / tileSize;
    const qint64 rows = (canvas.height() + tileSize - 1) / tileSize;
    const quint32 count = qFromLittleEndian<quint32>(block.data());
    if (count != expectedCount || count > columns * rows)
        return damaged(QStringLiteral("invalid tile count"));
    struct Entry
    {
        QRect rect;
        qsizetype offset;
        quint32 length;
    };
    std::vector<Entry> entries;
    entries.reserve(count);
    qsizetype pos = 4;
    qint64 previous = -1;
    for (quint32 i = 0; i < count; ++i) {
        if (block.size() - pos < 12)
            return damaged(QStringLiteral("tile data is cut short"));
        const quint32 column = qFromLittleEndian<quint32>(block.data() + pos);
        const quint32 row = qFromLittleEndian<quint32>(block.data() + pos + 4);
        const quint32 length = qFromLittleEndian<quint32>(block.data() + pos + 8);
        pos += 12;
        if (column >= columns || row >= rows)
            return damaged(QStringLiteral("a tile lies outside the canvas"));
        const qint64 index = qint64(row) * columns + column;
        if (index <= previous)
            return damaged(QStringLiteral("tiles are out of order or repeated"));
        previous = index;
        if (length < 4 || length > quint64(block.size() - pos))
            return damaged(QStringLiteral("invalid tile data size"));
        const QRect r = QRect(int(column) * tileSize, int(row) * tileSize, tileSize, tileSize)
                        & QRect(QPoint(0, 0), canvas);
        // Checked before qUncompress allocates (at most MaxTileSize² × 4 bytes).
        if (qFromBigEndian<quint32>(block.data() + pos) != quint32(qsizetype(r.width()) * r.height() * 4))
            return damaged(QStringLiteral("tile data size does not match the tile"));
        entries.push_back({r, pos, length});
        pos += length;
    }
    if (pos != block.size())
        return damaged(QStringLiteral("unexpected data after the last tile"));

    std::vector<QByteArray> raw;
    std::vector<int> problem; // 0 ok, 1 cannot decompress, 2 invalid pixels, 3 transparent (ok)
    std::vector<uchar *> destination;
    for (size_t first = 0; first < entries.size(); first += TileChunk) {
        const int n = int(std::min<size_t>(TileChunk, entries.size() - first));
        raw.assign(size_t(n), QByteArray());
        problem.assign(size_t(n), 0);
        parallelFor(0, n, 4, [&](int i) {
            const Entry &e = entries[first + size_t(i)];
            const qsizetype rawSize = qsizetype(e.rect.width()) * e.rect.height() * 4;
            QByteArray pixels = qUncompress(reinterpret_cast<const uchar *>(block.data() + e.offset), e.length);
            if (pixels.size() != rawSize) {
                problem[size_t(i)] = 1;
                return;
            }
            uchar *bytes = reinterpret_cast<uchar *>(pixels.data());
            if (!validPremultiplied(bytes, rawSize)) {
                problem[size_t(i)] = 2;
                return;
            }
            if (out) {
                if (std::all_of(bytes, bytes + rawSize, [](uchar b) { return b == 0; })) {
                    problem[size_t(i)] = 3; // all transparent: stays unallocated
                    return;
                }
                swapToFileOrder(bytes, rawSize);
                raw[size_t(i)] = std::move(pixels);
            }
        });
        for (int i = 0; i < n; ++i) {
            if (problem[size_t(i)] == 1)
                return damaged(QStringLiteral("pixel data cannot be decompressed"));
            if (problem[size_t(i)] == 2)
                return damaged(QStringLiteral("invalid pixel values"));
        }
        if (!out)
            continue;
        if (tileSize == out->tileSize()) {
            // Same grid: the tiles are allocated here (one thread: the layer's table is not
            // thread-safe), then filled in parallel (each thread writes its own tiles).
            destination.assign(size_t(n), nullptr);
            for (int i = 0; i < n; ++i) {
                if (problem[size_t(i)] == 3)
                    continue;
                const Entry &e = entries[first + size_t(i)];
                destination[size_t(i)] = out->writableTileData(out->columnOf(e.rect.left()), out->rowOf(e.rect.top()));
                if (!destination[size_t(i)])
                    return outOfMemory();
            }
            parallelFor(0, n, 8, [&](int i) {
                uchar *dst = destination[size_t(i)];
                if (!dst)
                    return;
                const Entry &e = entries[first + size_t(i)];
                const uchar *src = reinterpret_cast<const uchar *>(raw[size_t(i)].constData());
                const qsizetype rowBytes = qsizetype(e.rect.width()) * 4;
                for (int y = 0; y < e.rect.height(); ++y)
                    std::memcpy(dst + qsizetype(y) * out->tileStride(), src + rowBytes * y, size_t(rowBytes));
                raw[size_t(i)] = QByteArray(); // freed as soon as it is in the layer
            });
        } else {
            for (int i = 0; i < n; ++i) {
                if (problem[size_t(i)] == 3)
                    continue;
                const Entry &e = entries[first + size_t(i)];
                const qsizetype rowBytes = qsizetype(e.rect.width()) * 4;
                const QImage tile(reinterpret_cast<const uchar *>(raw[size_t(i)].constData()), e.rect.width(),
                                  e.rect.height(), rowBytes, QImage::Format_ARGB32_Premultiplied);
                if (!out->write(e.rect.topLeft(), tile))
                    return outOfMemory();
                raw[size_t(i)] = QByteArray();
            }
        }
    }
    return {};
}

// --- the shared layered container (versions 2 and 3) ---

struct Layered
{
    quint32 width = 0, height = 0, layerCount = 0, metaSize = 0;
    QByteArrayView payload;
    QJsonObject root;
    QJsonArray layers;
    int tileSize = 0; // version 3
};

// Header, checksum and metadata checks; everything that bounds memory is checked before any pixel
// data is touched. Returns an empty string or the reason.
QString readLayered(QByteArrayView data, quint32 version, Layered *l, bool *tooLargeOut)
{
    l->width = qFromLittleEndian<quint32>(data.data() + 12);
    l->height = qFromLittleEndian<quint32>(data.data() + 16);
    l->layerCount = qFromLittleEndian<quint32>(data.data() + 20);
    l->metaSize = qFromLittleEndian<quint32>(data.data() + 24);
    const quint64 payloadSize = qFromLittleEndian<quint64>(data.data() + 28);
    if (l->width == 0 || l->height == 0)
        return damaged(QStringLiteral("canvas size is zero"));
    if (l->layerCount == 0)
        return damaged(QStringLiteral("no layers"));
    if (!sizeWithinLimits(l->width, l->height, l->layerCount, version < 3)) {
        *tooLargeOut = true;
        return tooLarge(l->width, l->height, l->layerCount, version < 3);
    }
    if (payloadSize != quint64(data.size() - ProjectFile::HeaderSize))
        return damaged(QStringLiteral("unexpected file length"));
    if (l->metaSize == 0 || l->metaSize > quint32(ProjectFile::MaxMetadataBytes) || l->metaSize > payloadSize)
        return damaged(QStringLiteral("invalid metadata length"));
    l->payload = data.sliced(ProjectFile::HeaderSize);
    const QByteArray digest = QCryptographicHash::hash(l->payload, QCryptographicHash::Sha256);
    if (std::memcmp(digest.constData(), data.data() + 36, 32) != 0)
        return damaged(QStringLiteral("checksum mismatch"));

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(l->payload.first(l->metaSize).toByteArray(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return damaged(QStringLiteral("layer information is not valid"));
    l->root = json.object();
    l->layers = l->root.value(QStringLiteral("layers")).toArray();
    if (quint32(l->layers.size()) != l->layerCount)
        return damaged(QStringLiteral("layer count mismatch"));
    if (version >= 3) {
        const QJsonValue tileSize = l->root.value(QStringLiteral("tileSize"));
        const double t = tileSize.toDouble(-1);
        if (!tileSize.isDouble() || t < ProjectFile::MinTileSize || t > ProjectFile::MaxTileSize || t != std::floor(t)
            || (int(t) & (int(t) - 1)) != 0)
            return damaged(QStringLiteral("invalid tile size"));
        l->tileSize = int(t);
    }
    return {};
}

// One layer's properties and the extent of its block in the payload. Empty string or the reason.
QString readLayerInfo(const QJsonObject &o, quint32 version, qint64 offset, qint64 payloadSize, QSet<quint64> &ids,
                      LayerData *layer, qint64 *blockSize, qint64 *tiles)
{
    bool idOk = false;
    layer->id = o.value(QStringLiteral("id")).toString().toULongLong(&idOk);
    layer->name = o.value(QStringLiteral("name")).toString();
    const QJsonValue opacity = o.value(QStringLiteral("opacity"));
    *blockSize = qint64(o.value(QStringLiteral("compressedSize")).toDouble(-1));
    if (!idOk || layer->id == 0 || ids.contains(layer->id))
        return damaged(QStringLiteral("invalid layer id"));
    if (layer->name.size() > DocumentLimits::MaxNameLength || !o.value(QStringLiteral("visible")).isBool()
        || !o.value(QStringLiteral("locked")).isBool() || !opacity.isDouble())
        return damaged(QStringLiteral("invalid layer properties"));
    layer->visible = o.value(QStringLiteral("visible")).toBool();
    layer->locked = o.value(QStringLiteral("locked")).toBool();
    layer->opacity = opacity.toDouble();
    if (!std::isfinite(layer->opacity) || layer->opacity < 0.0 || layer->opacity > 1.0)
        return damaged(QStringLiteral("invalid layer opacity"));
    if (*blockSize < 4 || offset + *blockSize > payloadSize)
        return damaged(QStringLiteral("invalid layer data size"));
    if (version >= 3) {
        const QJsonValue t = o.value(QStringLiteral("tiles"));
        *tiles = qint64(t.toDouble(-1));
        if (!t.isDouble() || *tiles < 0 || double(*tiles) != t.toDouble())
            return damaged(QStringLiteral("invalid tile count"));
    }
    ids.insert(layer->id);
    return {};
}

ProjectFile::LoadResult decodeVersion1(QByteArrayView data)
{
    const quint32 width = qFromLittleEndian<quint32>(data.data() + 12);
    const quint32 height = qFromLittleEndian<quint32>(data.data() + 16);
    const quint32 pixelFormat = qFromLittleEndian<quint32>(data.data() + 20);
    const quint32 compression = qFromLittleEndian<quint32>(data.data() + 24);
    const quint64 payloadSize = qFromLittleEndian<quint64>(data.data() + 28);
    if (width == 0 || height == 0)
        return failure(damaged(QStringLiteral("canvas size is zero")));
    if (!sizeWithinLimits(width, height, 1, true))
        return failure(tooLarge(width, height, 1, true));
    if (pixelFormat != PixelFormatRgba8Premultiplied || compression != CompressionZlib)
        return failure(QStringLiteral("This project uses a pixel format this version of DrawApp does not support."));
    if (payloadSize != quint64(data.size() - ProjectFile::HeaderSize))
        return failure(damaged(QStringLiteral("unexpected file length")));
    const QByteArrayView payload = data.sliced(ProjectFile::HeaderSize);
    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    if (std::memcmp(digest.constData(), data.data() + 36, 32) != 0)
        return failure(damaged(QStringLiteral("checksum mismatch")));
    QString error;
    const QImage image = decompressLayer(payload, int(width), int(height), &error);
    if (image.isNull())
        return failure(error);
    ProjectFile::LoadResult r;
    r.document = DocumentData::fromImage(image);
    if (r.document.layers.front().image.isNull())
        return failure(outOfMemory());
    r.version = 1;
    return r;
}

// Versions 2 and 3.
ProjectFile::LoadResult decodeLayered(QByteArrayView data, quint32 version)
{
    Layered l;
    bool tooBig = false;
    if (const QString error = readLayered(data, version, &l, &tooBig); !error.isEmpty())
        return failure(error);

    ProjectFile::LoadResult result;
    result.version = int(version);
    result.document.size = QSize(int(l.width), int(l.height));
    QSet<quint64> ids;
    qint64 offset = l.metaSize;
    for (const QJsonValue &value : std::as_const(l.layers)) {
        LayerData layer;
        qint64 blockSize = 0, tiles = 0;
        if (const QString error = readLayerInfo(value.toObject(), version, offset, l.payload.size(), ids, &layer,
                                                &blockSize, &tiles);
            !error.isEmpty())
            return failure(error);
        const QByteArrayView block = l.payload.sliced(offset, blockSize);
        if (version >= 3) {
            layer.image = TiledImage(result.document.size);
            if (const QString error = decodeTiles(block, l.tileSize, result.document.size, tiles, &layer.image);
                !error.isEmpty())
                return failure(error);
        } else {
            QString error;
            const QImage dense = decompressLayer(block, int(l.width), int(l.height), &error);
            if (dense.isNull())
                return failure(error);
            layer.image = TiledImage::fromImage(dense);
            if (layer.image.isNull())
                return failure(outOfMemory());
        }
        offset += blockSize;
        result.document.layers.push_back(layer);
    }
    if (offset != qint64(l.payload.size()))
        return failure(damaged(QStringLiteral("unexpected data after the last layer")));

    bool activeOk = false;
    const quint64 active = l.root.value(QStringLiteral("activeLayerId")).toString().toULongLong(&activeOk);
    result.document.activeLayerId = activeOk && ids.contains(active) ? active : result.document.layers.back().id;
    return result;
}

// Header, metadata and blocks, written once into one buffer (no intermediate copies of the
// payload). Each block is released as soon as it is appended.
QByteArray assemble(quint32 version, const DocumentData &document, QJsonObject root, QJsonArray layers,
                    std::vector<QByteArray> &blocks)
{
    root[QStringLiteral("activeLayerId")] = QString::number(document.activeLayerId);
    root[QStringLiteral("layers")] = layers;
    const QByteArray meta = QJsonDocument(root).toJson(QJsonDocument::Compact);
    qint64 payloadSize = meta.size();
    for (const QByteArray &block : blocks)
        payloadSize += block.size();

    QByteArray out;
    out.reserve(ProjectFile::HeaderSize + payloadSize);
    out.append(Magic, sizeof(Magic));
    char header[ProjectFile::HeaderSize - sizeof(Magic)] = {};
    qToLittleEndian(version, header);
    qToLittleEndian(quint32(document.size.width()), header + 4);
    qToLittleEndian(quint32(document.size.height()), header + 8);
    qToLittleEndian(quint32(document.layers.size()), header + 12);
    qToLittleEndian(quint32(meta.size()), header + 16);
    qToLittleEndian(quint64(payloadSize), header + 20);
    out.append(header, sizeof(header)); // digest (offset 36) filled in below
    out.append(meta);
    for (QByteArray &block : blocks) {
        out.append(block);
        block = QByteArray();
    }
    const QByteArray digest = QCryptographicHash::hash(QByteArrayView(out).sliced(ProjectFile::HeaderSize),
                                                       QCryptographicHash::Sha256);
    std::memcpy(out.data() + 36, digest.constData(), 32);
    return out;
}

QJsonObject layerInfo(const LayerData &layer, qint64 blockSize)
{
    QJsonObject o;
    o[QStringLiteral("id")] = QString::number(layer.id);
    o[QStringLiteral("name")] = layer.name;
    o[QStringLiteral("visible")] = layer.visible;
    o[QStringLiteral("opacity")] = layer.opacity;
    o[QStringLiteral("locked")] = layer.locked;
    o[QStringLiteral("compressedSize")] = double(blockSize);
    return o;
}

} // namespace

QByteArray ProjectFile::encode(const DocumentData &document)
{
    const int tileSize = document.layers.empty() ? TiledImage::DefaultTileSize : document.layers.front().image.tileSize();
    QJsonArray layers;
    std::vector<QByteArray> blocks;
    for (const LayerData &layer : document.layers) {
        int tiles = 0;
        blocks.push_back(layer.image.tileSize() == tileSize ? encodeTiles(layer.image, &tiles) : QByteArray());
        if (blocks.back().isEmpty())
            return {}; // layers of different tile sizes: not produced by the app
        QJsonObject o = layerInfo(layer, blocks.back().size());
        o[QStringLiteral("tiles")] = tiles;
        layers.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("tileSize")] = tileSize;
    return assemble(3, document, root, layers, blocks);
}

QByteArray ProjectFile::encodeVersion2(const DocumentData &document)
{
    QJsonArray layers;
    std::vector<QByteArray> blocks;
    for (const LayerData &layer : document.layers) {
        const QImage dense = layer.image.toImage();
        blocks.push_back(dense.isNull() ? QByteArray() : compressLayer(dense));
        if (blocks.back().isEmpty())
            return {};
        layers.append(layerInfo(layer, blocks.back().size()));
    }
    return assemble(2, document, QJsonObject(), layers, blocks);
}

QByteArray ProjectFile::encodeVersion1(const QImage &image)
{
    const QByteArray payload = compressLayer(image);
    if (payload.isEmpty())
        return {};
    QByteArray out(HeaderSize, '\0');
    char *h = out.data();
    std::memcpy(h, Magic, sizeof(Magic));
    qToLittleEndian(quint32(1), h + 8);
    qToLittleEndian(quint32(image.width()), h + 12);
    qToLittleEndian(quint32(image.height()), h + 16);
    qToLittleEndian(PixelFormatRgba8Premultiplied, h + 20);
    qToLittleEndian(CompressionZlib, h + 24);
    qToLittleEndian(quint64(payload.size()), h + 28);
    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    std::memcpy(out.data() + 36, digest.constData(), 32);
    out.append(payload);
    return out;
}

QString ProjectFile::verify(QByteArrayView data)
{
    // The structure checks of decode() for version 3, but tiles are only decompressed and
    // checked, one at a time, so a large project does not need its decoded size in memory.
    if (data.size() < HeaderSize || std::memcmp(data.data(), Magic, sizeof(Magic)) != 0)
        return damaged(QStringLiteral("header is missing"));
    if (qFromLittleEndian<quint32>(data.data() + 8) != CurrentVersion)
        return damaged(QStringLiteral("unexpected format version"));
    Layered l;
    bool tooBig = false;
    if (const QString error = readLayered(data, CurrentVersion, &l, &tooBig); !error.isEmpty())
        return tooBig ? damaged(QStringLiteral("invalid canvas size or layer count")) : error;
    const QSize canvas(int(l.width), int(l.height));
    QSet<quint64> ids;
    qint64 offset = l.metaSize;
    for (const QJsonValue &value : std::as_const(l.layers)) {
        LayerData layer;
        qint64 blockSize = 0, tiles = 0;
        if (const QString error = readLayerInfo(value.toObject(), CurrentVersion, offset, l.payload.size(), ids,
                                                &layer, &blockSize, &tiles);
            !error.isEmpty())
            return error;
        if (const QString error = decodeTiles(l.payload.sliced(offset, blockSize), l.tileSize, canvas, tiles, nullptr);
            !error.isEmpty())
            return error;
        offset += blockSize;
    }
    if (offset != qint64(l.payload.size()))
        return damaged(QStringLiteral("unexpected data after the last layer"));
    return {};
}

ProjectFile::LoadResult ProjectFile::decode(QByteArrayView data)
{
    if (data.size() < 8 || std::memcmp(data.data(), Magic, sizeof(Magic)) != 0)
        return failure(QStringLiteral("This is not a DrawApp project file."));
    if (data.size() < HeaderSize)
        return failure(damaged(QStringLiteral("header is cut short")));
    const quint32 version = qFromLittleEndian<quint32>(data.data() + 8);
    if (version == 0)
        return failure(damaged(QStringLiteral("invalid format version")));
    if (version > CurrentVersion)
        return failure(QStringLiteral("This project was saved by a newer version of DrawApp (format %1). "
                                      "This version can open formats up to %2.").arg(version).arg(CurrentVersion));
    return version == 1 ? decodeVersion1(data) : decodeLayered(data, version);
}

QString ProjectFile::writeFileSafely(const QString &path, const QByteArray &bytes)
{
    const QString name = displayName(path);
    if (isContentUri(path)) // cannot be replaced atomically: SafeSave handles these
        return QStringLiteral("\"%1\" is a storage-provider document; it must be written through SafeSave.").arg(name);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Could not open \"%1\" for writing: %2").arg(name, file.errorString());
    if (file.write(bytes) != bytes.size()) {
        const QString reason = file.errorString();
        file.cancelWriting();
        return QStringLiteral("Could not write \"%1\": %2").arg(name, reason);
    }
    if (!file.commit())
        return QStringLiteral("Could not save \"%1\": %2. Any previous version of the file was left unchanged.")
            .arg(name, file.errorString());
    return {};
}

QString ProjectFile::save(const DocumentData &document, const QString &path)
{
    return writeFileSafely(path, encode(document));
}

QByteArray ProjectFile::readBounded(QIODevice &device, qint64 maxBytes)
{
    constexpr qint64 Chunk = qint64(1) << 20;
    QByteArray data;
    if (!device.isSequential()) {
        const qint64 remaining = device.size() - device.pos();
        if (remaining > 0)
            data = device.read(std::min(remaining, maxBytes + 1)); // exactly the size, not the limit
    }
    // Sequential devices, and files that grew after their size was read: bounded chunks.
    while (data.size() <= maxBytes) {
        const QByteArray part = device.read(std::min(Chunk, maxBytes + 1 - data.size()));
        if (part.isEmpty())
            break;
        data += part;
    }
    return data;
}

ProjectFile::LoadResult ProjectFile::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("Could not open \"%1\": %2").arg(displayName(path), file.errorString()));
    if (!file.isSequential() && file.size() > MaxFileBytes)
        return failure(QStringLiteral("The file is too large to be a DrawApp project this version can open."));
    // The file's bytes are held while decoding: reserved first, so a large file is refused before
    // it is read rather than after.
    const qint64 expected = file.isSequential() ? 0 : file.size();
    if (!MemoryBudget::tryReserve(expected, MemoryBudget::Category::Temporary))
        return failure(QStringLiteral("Not enough memory to read \"%1\" (%2 MiB) within %3.")
                           .arg(displayName(path)).arg(expected / MiB).arg(MemoryBudget::shortageText()));
    struct Release
    {
        qint64 bytes;
        ~Release() { MemoryBudget::release(bytes, MemoryBudget::Category::Temporary); }
    } reserved{expected};
    // Read at most one byte past the limit so an oversized file is rejected without loading it all.
    const QByteArray data = readBounded(file, MaxFileBytes);
    if (file.error() != QFileDevice::NoError)
        return failure(QStringLiteral("Could not read \"%1\": %2").arg(displayName(path), file.errorString()));
    if (data.size() > MaxFileBytes)
        return failure(QStringLiteral("The file is too large to be a DrawApp project this version can open."));
    return decode(data);
}

QString ProjectFile::exportPng(const QImage &image, const QString &path)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "png");
    if (!writer.write(image.convertToFormat(QImage::Format_ARGB32)))
        return QStringLiteral("Could not encode the PNG image: %1").arg(writer.errorString());
    return writeFileSafely(path, bytes);
}

#include "ProjectFile.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
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
#include <cmath>
#include <cstring>

namespace {

constexpr char Magic[8] = {'D', 'R', 'A', 'W', 'A', 'P', 'P', '\n'};
constexpr quint32 PixelFormatRgba8Premultiplied = 1;
constexpr quint32 CompressionZlib = 1;

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

QString tooLarge(quint32 w, quint32 h, quint32 layers)
{
    return QStringLiteral("The canvas is too large to open (%1 × %2 pixels, %3 layer(s)). Limits: %4 pixels per side, "
                          "%5 megapixels per layer, %6 layers, %7 MiB of layer pixels in total.")
        .arg(w).arg(h).arg(layers)
        .arg(DocumentLimits::MaxSide)
        .arg(DocumentLimits::MaxLayerPixels / (1024 * 1024))
        .arg(DocumentLimits::MaxLayers)
        .arg(DocumentLimits::MaxPixelBytes / (1024 * 1024));
}

QByteArray compressLayer(const QImage &image)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const qsizetype rowBytes = qsizetype(rgba.width()) * 4;
    QByteArray raw(rowBytes * rgba.height(), Qt::Uninitialized);
    for (int y = 0; y < rgba.height(); ++y)
        std::memcpy(raw.data() + rowBytes * y, rgba.constScanLine(y), size_t(rowBytes));
    return qCompress(raw);
}

// Decodes one compressed layer block. The declared raw size is checked before qUncompress
// allocates anything.
QImage decompressLayer(const QByteArray &block, int width, int height, QString *error)
{
    const qsizetype rawSize = qsizetype(width) * height * 4;
    if (block.size() < 4 || qFromBigEndian<quint32>(block.constData()) != quint32(rawSize)) {
        *error = damaged(QStringLiteral("pixel data size does not match the canvas size"));
        return {};
    }
    const QByteArray raw = qUncompress(block);
    if (raw.size() != rawSize) {
        *error = damaged(QStringLiteral("pixel data cannot be decompressed"));
        return {};
    }
    QImage rgba(width, height, QImage::Format_RGBA8888_Premultiplied);
    if (rgba.isNull()) {
        *error = QStringLiteral("Not enough memory to open a %1 × %2 canvas.").arg(width).arg(height);
        return {};
    }
    const qsizetype rowBytes = qsizetype(width) * 4;
    for (int y = 0; y < height; ++y) {
        const uchar *src = reinterpret_cast<const uchar *>(raw.constData()) + rowBytes * y;
        for (qsizetype i = 0; i < rowBytes; i += 4) {
            const uchar a = src[i + 3];
            if (src[i] > a || src[i + 1] > a || src[i + 2] > a) {
                *error = damaged(QStringLiteral("invalid pixel values"));
                return {};
            }
        }
        std::memcpy(rgba.scanLine(y), src, size_t(rowBytes));
    }
    QImage image = rgba.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        *error = QStringLiteral("Not enough memory to open a %1 × %2 canvas.").arg(width).arg(height);
    return image;
}

bool sizeWithinLimits(quint32 w, quint32 h, quint32 layers)
{
    if (w > quint32(DocumentLimits::MaxSide) || h > quint32(DocumentLimits::MaxSide))
        return false;
    if (qint64(w) * h > DocumentLimits::MaxLayerPixels || layers > quint32(DocumentLimits::MaxLayers))
        return false;
    return qint64(w) * h * 4 * layers <= DocumentLimits::MaxPixelBytes;
}

ProjectFile::LoadResult decodeVersion1(const QByteArray &data)
{
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.skipRawData(12);
    quint32 width = 0, height = 0, pixelFormat = 0, compression = 0;
    quint64 payloadSize = 0;
    stream >> width >> height >> pixelFormat >> compression >> payloadSize;
    if (width == 0 || height == 0)
        return failure(damaged(QStringLiteral("canvas size is zero")));
    if (!sizeWithinLimits(width, height, 1))
        return failure(tooLarge(width, height, 1));
    if (pixelFormat != PixelFormatRgba8Premultiplied || compression != CompressionZlib)
        return failure(QStringLiteral("This project uses a pixel format this version of DrawApp does not support."));
    if (payloadSize != quint64(data.size() - ProjectFile::HeaderSize))
        return failure(damaged(QStringLiteral("unexpected file length")));
    const QByteArray payload = data.mid(ProjectFile::HeaderSize);
    if (QCryptographicHash::hash(payload, QCryptographicHash::Sha256) != data.mid(36, 32))
        return failure(damaged(QStringLiteral("checksum mismatch")));
    QString error;
    const QImage image = decompressLayer(payload, int(width), int(height), &error);
    if (image.isNull())
        return failure(error);
    ProjectFile::LoadResult r;
    r.document = DocumentData::fromImage(image);
    r.version = 1;
    return r;
}

ProjectFile::LoadResult decodeVersion2(const QByteArray &data)
{
    const quint32 width = qFromLittleEndian<quint32>(data.constData() + 12);
    const quint32 height = qFromLittleEndian<quint32>(data.constData() + 16);
    const quint32 layerCount = qFromLittleEndian<quint32>(data.constData() + 20);
    const quint32 metaSize = qFromLittleEndian<quint32>(data.constData() + 24);
    const quint64 payloadSize = qFromLittleEndian<quint64>(data.constData() + 28);

    // Everything that bounds memory is checked before any pixel data is touched.
    if (width == 0 || height == 0)
        return failure(damaged(QStringLiteral("canvas size is zero")));
    if (layerCount == 0)
        return failure(damaged(QStringLiteral("no layers")));
    if (!sizeWithinLimits(width, height, layerCount))
        return failure(tooLarge(width, height, layerCount));
    if (payloadSize != quint64(data.size() - ProjectFile::HeaderSize))
        return failure(damaged(QStringLiteral("unexpected file length")));
    if (metaSize == 0 || metaSize > quint32(ProjectFile::MaxMetadataBytes) || metaSize > payloadSize)
        return failure(damaged(QStringLiteral("invalid metadata length")));
    const QByteArray payload = data.mid(ProjectFile::HeaderSize);
    if (QCryptographicHash::hash(payload, QCryptographicHash::Sha256) != data.mid(36, 32))
        return failure(damaged(QStringLiteral("checksum mismatch")));

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(payload.left(metaSize), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return failure(damaged(QStringLiteral("layer information is not valid")));
    const QJsonArray layers = json.object().value(QStringLiteral("layers")).toArray();
    if (quint32(layers.size()) != layerCount)
        return failure(damaged(QStringLiteral("layer count mismatch")));

    ProjectFile::LoadResult result;
    result.version = 2;
    result.document.size = QSize(int(width), int(height));
    QSet<quint64> ids;
    qint64 offset = metaSize;
    for (const QJsonValue &value : layers) {
        const QJsonObject o = value.toObject();
        bool idOk = false;
        LayerData layer;
        layer.id = o.value(QStringLiteral("id")).toString().toULongLong(&idOk);
        layer.name = o.value(QStringLiteral("name")).toString();
        const QJsonValue opacity = o.value(QStringLiteral("opacity"));
        const qint64 blockSize = qint64(o.value(QStringLiteral("compressedSize")).toDouble(-1));
        if (!idOk || layer.id == 0 || ids.contains(layer.id))
            return failure(damaged(QStringLiteral("invalid layer id")));
        if (layer.name.size() > DocumentLimits::MaxNameLength || !o.value(QStringLiteral("visible")).isBool()
            || !o.value(QStringLiteral("locked")).isBool() || !opacity.isDouble())
            return failure(damaged(QStringLiteral("invalid layer properties")));
        layer.visible = o.value(QStringLiteral("visible")).toBool();
        layer.locked = o.value(QStringLiteral("locked")).toBool();
        layer.opacity = opacity.toDouble();
        if (!std::isfinite(layer.opacity) || layer.opacity < 0.0 || layer.opacity > 1.0)
            return failure(damaged(QStringLiteral("invalid layer opacity")));
        if (blockSize < 4 || offset + blockSize > qint64(payload.size()))
            return failure(damaged(QStringLiteral("invalid layer data size")));
        ids.insert(layer.id);

        QString error;
        layer.image = decompressLayer(payload.mid(offset, blockSize), int(width), int(height), &error);
        if (layer.image.isNull())
            return failure(error);
        offset += blockSize;
        result.document.layers.push_back(layer);
    }
    if (offset != qint64(payload.size()))
        return failure(damaged(QStringLiteral("unexpected data after the last layer")));

    bool activeOk = false;
    const quint64 active = json.object().value(QStringLiteral("activeLayerId")).toString().toULongLong(&activeOk);
    result.document.activeLayerId = activeOk && ids.contains(active) ? active : result.document.layers.back().id;
    return result;
}

} // namespace

QByteArray ProjectFile::encode(const DocumentData &document)
{
    QJsonArray layers;
    QList<QByteArray> blocks;
    for (const LayerData &layer : document.layers) {
        blocks.append(compressLayer(layer.image));
        QJsonObject o;
        o[QStringLiteral("id")] = QString::number(layer.id);
        o[QStringLiteral("name")] = layer.name;
        o[QStringLiteral("visible")] = layer.visible;
        o[QStringLiteral("opacity")] = layer.opacity;
        o[QStringLiteral("locked")] = layer.locked;
        o[QStringLiteral("compressedSize")] = double(blocks.last().size());
        layers.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("activeLayerId")] = QString::number(document.activeLayerId);
    root[QStringLiteral("layers")] = layers;
    QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    const quint32 metaSize = quint32(payload.size());
    for (const QByteArray &block : blocks)
        payload.append(block);

    QByteArray out;
    out.reserve(HeaderSize + payload.size());
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData(Magic, sizeof(Magic));
    stream << CurrentVersion << quint32(document.size.width()) << quint32(document.size.height())
           << quint32(document.layers.size()) << metaSize << quint64(payload.size());
    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    stream.writeRawData(digest.constData(), int(digest.size()));
    out.append(payload);
    return out;
}

QByteArray ProjectFile::encodeVersion1(const QImage &image)
{
    const QByteArray payload = compressLayer(image);
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData(Magic, sizeof(Magic));
    stream << quint32(1) << quint32(image.width()) << quint32(image.height()) << PixelFormatRgba8Premultiplied
           << CompressionZlib << quint64(payload.size());
    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    stream.writeRawData(digest.constData(), int(digest.size()));
    out.append(payload);
    return out;
}

QString ProjectFile::verify(const QByteArray &data)
{
    // The structure checks of decode() for version 2, but each layer block is only decompressed
    // and measured, one at a time, so a large project does not need its decoded size in memory.
    if (data.size() < HeaderSize || std::memcmp(data.constData(), Magic, sizeof(Magic)) != 0)
        return damaged(QStringLiteral("header is missing"));
    if (qFromLittleEndian<quint32>(data.constData() + 8) != CurrentVersion)
        return damaged(QStringLiteral("unexpected format version"));
    const quint32 width = qFromLittleEndian<quint32>(data.constData() + 12);
    const quint32 height = qFromLittleEndian<quint32>(data.constData() + 16);
    const quint32 layerCount = qFromLittleEndian<quint32>(data.constData() + 20);
    const quint32 metaSize = qFromLittleEndian<quint32>(data.constData() + 24);
    const quint64 payloadSize = qFromLittleEndian<quint64>(data.constData() + 28);
    if (width == 0 || height == 0 || layerCount == 0 || !sizeWithinLimits(width, height, layerCount))
        return damaged(QStringLiteral("invalid canvas size or layer count"));
    if (payloadSize != quint64(data.size() - HeaderSize) || metaSize == 0 || metaSize > payloadSize)
        return damaged(QStringLiteral("unexpected file length"));
    const QByteArrayView payload(data.constData() + HeaderSize, data.size() - HeaderSize);
    if (QCryptographicHash::hash(payload, QCryptographicHash::Sha256) != data.mid(36, 32))
        return damaged(QStringLiteral("checksum mismatch"));
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(payload.first(metaSize).toByteArray(), &parseError);
    const QJsonArray layers = json.object().value(QStringLiteral("layers")).toArray();
    if (parseError.error != QJsonParseError::NoError || quint32(layers.size()) != layerCount)
        return damaged(QStringLiteral("layer information is not valid"));
    const qsizetype rawSize = qsizetype(width) * height * 4;
    qint64 offset = metaSize;
    for (const QJsonValue &value : layers) {
        const qint64 blockSize = qint64(value.toObject().value(QStringLiteral("compressedSize")).toDouble(-1));
        if (blockSize < 4 || offset + blockSize > qint64(payload.size()))
            return damaged(QStringLiteral("invalid layer data size"));
        const QByteArray block = payload.sliced(offset, blockSize).toByteArray();
        if (qFromBigEndian<quint32>(block.constData()) != quint32(rawSize) || qUncompress(block).size() != rawSize)
            return damaged(QStringLiteral("pixel data cannot be decompressed"));
        offset += blockSize;
    }
    if (offset != qint64(payload.size()))
        return damaged(QStringLiteral("unexpected data after the last layer"));
    return {};
}

ProjectFile::LoadResult ProjectFile::decode(const QByteArray &data)
{
    if (data.size() < 8 || std::memcmp(data.constData(), Magic, sizeof(Magic)) != 0)
        return failure(QStringLiteral("This is not a DrawApp project file."));
    if (data.size() < HeaderSize)
        return failure(damaged(QStringLiteral("header is cut short")));
    const quint32 version = qFromLittleEndian<quint32>(data.constData() + 8);
    if (version == 0)
        return failure(damaged(QStringLiteral("invalid format version")));
    if (version > CurrentVersion)
        return failure(QStringLiteral("This project was saved by a newer version of DrawApp (format %1). "
                                      "This version can open formats up to %2.").arg(version).arg(CurrentVersion));
    return version == 1 ? decodeVersion1(data) : decodeVersion2(data);
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

ProjectFile::LoadResult ProjectFile::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("Could not open \"%1\": %2").arg(displayName(path), file.errorString()));
    if (!file.isSequential() && file.size() > MaxFileBytes)
        return failure(QStringLiteral("The file is too large to be a DrawApp project this version can open."));
    // Read at most one byte past the limit so an oversized file is rejected without loading it all.
    const QByteArray data = file.read(MaxFileBytes + 1);
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

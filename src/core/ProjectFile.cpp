#include "ProjectFile.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QSaveFile>
#include <QUrl>
#include <QtEndian>
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
    return {QImage(), message};
}

QString damaged(const QString &detail)
{
    return QStringLiteral("The file is damaged or incomplete (%1).").arg(detail);
}

} // namespace

QByteArray ProjectFile::encode(const QImage &image)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const qsizetype rowBytes = qsizetype(rgba.width()) * 4;
    QByteArray raw(rowBytes * rgba.height(), Qt::Uninitialized);
    for (int y = 0; y < rgba.height(); ++y)
        std::memcpy(raw.data() + rowBytes * y, rgba.constScanLine(y), size_t(rowBytes));
    const QByteArray payload = qCompress(raw);
    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);

    QByteArray out;
    out.reserve(HeaderSize + payload.size());
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData(Magic, sizeof(Magic));
    stream << CurrentVersion << quint32(rgba.width()) << quint32(rgba.height())
           << PixelFormatRgba8Premultiplied << CompressionZlib << quint64(payload.size());
    stream.writeRawData(digest.constData(), int(digest.size()));
    out.append(payload);
    return out;
}

ProjectFile::LoadResult ProjectFile::decode(const QByteArray &data)
{
    if (data.size() < 8 || std::memcmp(data.constData(), Magic, sizeof(Magic)) != 0)
        return failure(QStringLiteral("This is not a DrawApp project file."));
    if (data.size() < HeaderSize)
        return failure(damaged(QStringLiteral("header is cut short")));

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.skipRawData(sizeof(Magic));
    quint32 version = 0, width = 0, height = 0, pixelFormat = 0, compression = 0;
    quint64 payloadSize = 0;
    stream >> version >> width >> height >> pixelFormat >> compression >> payloadSize;
    QByteArray digest(32, Qt::Uninitialized);
    stream.readRawData(digest.data(), 32);

    if (version == 0)
        return failure(damaged(QStringLiteral("invalid format version")));
    if (version > CurrentVersion)
        return failure(QStringLiteral("This project was saved by a newer version of DrawApp (format %1). "
                                      "This version can open format %2.").arg(version).arg(CurrentVersion));
    if (width == 0 || height == 0)
        return failure(damaged(QStringLiteral("canvas size is zero")));
    if (width > quint32(MaxSide) || height > quint32(MaxSide) || qint64(width) * height > MaxPixels)
        return failure(QStringLiteral("The canvas is too large to open (%1 × %2 pixels). The limit is %3 pixels "
                                      "per side and %4 megapixels in total.")
                           .arg(width).arg(height).arg(MaxSide).arg(MaxPixels / (1024 * 1024)));
    if (pixelFormat != PixelFormatRgba8Premultiplied || compression != CompressionZlib)
        return failure(QStringLiteral("This project uses a pixel format this version of DrawApp does not support."));
    if (payloadSize != quint64(data.size() - HeaderSize))
        return failure(damaged(QStringLiteral("unexpected file length")));

    const QByteArray payload = data.mid(HeaderSize);
    if (QCryptographicHash::hash(payload, QCryptographicHash::Sha256) != digest)
        return failure(damaged(QStringLiteral("checksum mismatch")));

    // qCompress output starts with the uncompressed size; check it before qUncompress allocates.
    const qsizetype rawSize = qsizetype(width) * height * 4;
    if (payload.size() < 4 || qFromBigEndian<quint32>(payload.constData()) != quint32(rawSize))
        return failure(damaged(QStringLiteral("pixel data size does not match the canvas size")));
    const QByteArray raw = qUncompress(payload);
    if (raw.size() != rawSize)
        return failure(damaged(QStringLiteral("pixel data cannot be decompressed")));

    QImage rgba(int(width), int(height), QImage::Format_RGBA8888_Premultiplied);
    if (rgba.isNull())
        return failure(QStringLiteral("Not enough memory to open a %1 × %2 canvas.").arg(width).arg(height));
    const qsizetype rowBytes = qsizetype(width) * 4;
    for (int y = 0; y < int(height); ++y) {
        const uchar *src = reinterpret_cast<const uchar *>(raw.constData()) + rowBytes * y;
        for (qsizetype i = 0; i < rowBytes; i += 4) {
            const uchar a = src[i + 3];
            if (src[i] > a || src[i + 1] > a || src[i + 2] > a)
                return failure(damaged(QStringLiteral("invalid pixel values")));
        }
        std::memcpy(rgba.scanLine(y), src, size_t(rowBytes));
    }
    QImage image = rgba.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return failure(QStringLiteral("Not enough memory to open a %1 × %2 canvas.").arg(width).arg(height));
    return {image, QString()};
}

QString ProjectFile::writeFileSafely(const QString &path, const QByteArray &bytes)
{
    const QString name = displayName(path);
    if (isContentUri(path)) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("Could not open \"%1\" for writing: %2").arg(name, file.errorString());
        if (file.write(bytes) != bytes.size() || !file.flush())
            return QStringLiteral("Could not write \"%1\": %2").arg(name, file.errorString());
        return {};
    }

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

QString ProjectFile::save(const QImage &image, const QString &path)
{
    return writeFileSafely(path, encode(image));
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

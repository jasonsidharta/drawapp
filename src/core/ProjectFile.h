#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

// The .drawapp project format, version 1: one raster canvas.
//
//   offset size  field (integers little-endian)
//   0      8     magic "DRAWAPP\n"
//   8      4     format version (1)
//   12     4     width in pixels
//   16     4     height in pixels
//   20     4     pixel format: 1 = 8-bit RGBA, premultiplied alpha, rows top to bottom
//   24     4     compression: 1 = zlib stream as written by qCompress (4-byte big-endian raw size first)
//   28     8     payload size in bytes
//   36     32    SHA-256 of the payload
//   68     n     payload; the file ends exactly here
namespace ProjectFile {

inline constexpr char Extension[] = "drawapp";
inline constexpr quint32 CurrentVersion = 1;
inline constexpr int HeaderSize = 68;
inline constexpr int MaxSide = 16384;
inline constexpr qint64 MaxPixels = qint64(8192) * 8192;
inline constexpr qint64 MaxFileBytes = HeaderSize + 4 + MaxPixels * 4 + MaxPixels * 4 / 1000 + 1024;

struct LoadResult
{
    QImage image;   // premultiplied ARGB32 on success
    QString error;  // user-facing reason on failure
    bool ok() const { return error.isEmpty(); }
};

QByteArray encode(const QImage &image);
LoadResult decode(const QByteArray &data);

// path is a local file path or, on Android, a content:// URI string.
// These return an empty string on success, otherwise a user-facing message.
QString save(const QImage &image, const QString &path);
LoadResult load(const QString &path);
// Writes the image as PNG with straight (non-premultiplied) alpha at its own resolution.
QString exportPng(const QImage &image, const QString &path);

// Writes bytes through a temporary file that atomically replaces path, so a failed write leaves
// any previous file intact. Content URIs (Android storage access framework) cannot be replaced
// atomically and are written directly.
QString writeFileSafely(const QString &path, const QByteArray &bytes);

} // namespace ProjectFile

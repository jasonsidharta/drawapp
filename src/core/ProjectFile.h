#pragma once

#include "DocumentData.h"

#include <QByteArray>
#include <QImage>
#include <QString>

// The .drawapp project format. Integers are little-endian. Both versions start with a 68-byte header.
//
// Version 2 (written by this app): layered.
//   0  8   magic "DRAWAPP\n"
//   8  4   format version (2)
//   12 4   width            16 4  height
//   20 4   layer count n    24 4  metadata length m
//   28 8   payload size (everything after the header)
//   36 32  SHA-256 of the payload
//   68     payload: metadata (UTF-8 JSON, m bytes), then n layer blocks bottom to top. Metadata:
//          {"activeLayerId":"<id>","layers":[{"id":"<id>","name":..,"visible":..,"opacity":0..1,
//          "locked":..,"compressedSize":<bytes>}, ...]}. Each block is qCompress output (4-byte
//          big-endian raw size, then zlib) of 8-bit RGBA premultiplied rows, top to bottom.
//
// Version 1 (read only, written by milestone 1): one raster.
//   20 4 pixel format (1)   24 4 compression (1)   28 8 payload size   36 32 SHA-256 of payload
//   68   payload: one qCompress block as above.
namespace ProjectFile {

inline constexpr char Extension[] = "drawapp";
inline constexpr quint32 CurrentVersion = 2;
inline constexpr int HeaderSize = 68;
inline constexpr int MaxSide = DocumentLimits::MaxSide;
inline constexpr qint64 MaxPixels = DocumentLimits::MaxLayerPixels;
inline constexpr int MaxMetadataBytes = 1024 * 1024;
inline constexpr qint64 MaxFileBytes = HeaderSize + MaxMetadataBytes + DocumentLimits::MaxPixelBytes
                                       + DocumentLimits::MaxPixelBytes / 1000 + DocumentLimits::MaxLayers * 64;

struct LoadResult
{
    DocumentData document; // on success: at least one layer, all images premultiplied ARGB32
    int version = 0;       // format version that was read
    QString error;         // user-facing reason on failure
    bool ok() const { return error.isEmpty(); }
};

QByteArray encode(const DocumentData &document);
// Writes the version 1 single-raster format (kept to test reading older files).
QByteArray encodeVersion1(const QImage &image);
LoadResult decode(const QByteArray &data);
// Checks bytes produced by encode() (structure, checksum, every layer block decompresses to the
// right size) without keeping the decoded layers. Empty on success, otherwise the reason.
QString verify(const QByteArray &data);

// load() takes a local file path or, on Android, a content:// URI string. The app saves and
// exports through SafeSave (src/core/SafeSave.h); save() and exportPng() here write local files
// only. These return an empty string on success, otherwise a user-facing message.
QString save(const DocumentData &document, const QString &path);
LoadResult load(const QString &path);
// Writes the image as PNG with straight (non-premultiplied) alpha at its own resolution.
QString exportPng(const QImage &image, const QString &path);

// Writes bytes through a temporary file that atomically replaces the local file at path, so a
// failed write leaves any previous file intact. Refuses content URIs (see SafeSave).
QString writeFileSafely(const QString &path, const QByteArray &bytes);

} // namespace ProjectFile

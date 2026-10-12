#pragma once

#include "DocumentData.h"

#include <QByteArray>
#include <QIODevice>
#include <QImage>
#include <QString>

// The .drawapp project format. Integers are little-endian unless stated. Every version starts with
// a 68-byte header.
//
// Version 3 (written since milestone 10): layered and sparse (only painted tiles are stored).
//   0  8   magic "DRAWAPP\n"
//   8  4   format version (3)
//   12 4   width            16 4  height
//   20 4   layer count n    24 4  metadata length m
//   28 8   payload size (everything after the header)
//   36 32  SHA-256 of the payload
//   68     payload: metadata (UTF-8 JSON, m bytes), then n layer blocks bottom to top. Metadata:
//          {"activeLayerId":"<id>","tileSize":64,"layers":[{"id":"<id>","name":..,"visible":..,
//          "opacity":0..1,"locked":..,"compressedSize":<block bytes>,"tiles":<count>}, ...]}.
//          tileSize is a power of two from 16 to 1024. A layer block is
//            4      tile count t
//            t × {  4 column, 4 row, 4 data length d, d bytes data  }
//          in increasing row-major order (no tile twice). Tile (column, row) covers the pixels
//          [column × tileSize, +tileSize) × [row × tileSize, +tileSize) clipped to the canvas;
//          its data is qCompress output (4-byte big-endian raw size, then zlib) of those pixels'
//          rows top to bottom, 4 bytes per pixel in the order B, G, R, A, premultiplied. A tile
//          that is not stored is transparent.
//
// Version 2 (milestones 3–9, still read): as version 3 without "tileSize" and "tiles"; each layer
//   block is one qCompress block of the whole layer as 8-bit RGBA premultiplied rows.
//
// Version 1 (read only, written by milestone 1): one raster.
//   20 4 pixel format (1)   24 4 compression (1)   28 8 payload size   36 32 SHA-256 of payload
//   68   payload: one qCompress block as in version 2.
//
// Memory: versions 1 and 2 hold dense layers, so they keep the milestone 9 limit on their total
// pixel bytes. Version 3 layers are allocated tile by tile against MemoryBudget, so how large a
// version 3 project can be opened depends on the memory limit and on how much is painted.
namespace ProjectFile {

inline constexpr char Extension[] = "drawapp";
inline constexpr quint32 CurrentVersion = 3;
inline constexpr int HeaderSize = 68;
inline constexpr int MaxSide = DocumentLimits::MaxSide;
inline constexpr qint64 MaxPixels = DocumentLimits::MaxLayerPixels;
inline constexpr int MaxMetadataBytes = 1024 * 1024;
inline constexpr int MinTileSize = 16;
inline constexpr int MaxTileSize = 1024;
// The whole file is read before decoding; the memory for it is also reserved against MemoryBudget.
inline constexpr qint64 MaxFileBytes = qint64(2) * 1024 * 1024 * 1024;

struct LoadResult
{
    DocumentData document; // on success: at least one layer, all images premultiplied ARGB32
    int version = 0;       // format version that was read
    QString error;         // user-facing reason on failure
    bool ok() const { return error.isEmpty(); }
};

// Encodes as version 3. Empty if memory for the encoded bytes cannot be had.
QByteArray encode(const DocumentData &document);
// The older formats (kept to test reading older files). Empty on failure.
QByteArray encodeVersion1(const QImage &image);
QByteArray encodeVersion2(const DocumentData &document);
LoadResult decode(QByteArrayView data);
// Checks bytes produced by encode() (structure, checksum, every tile decompresses to the right
// size) without keeping the decoded layers. Empty on success, otherwise the reason.
QString verify(QByteArrayView data);

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
// Reads the rest of device, but at most maxBytes + 1 bytes (so an oversized input can be told
// apart). Allocates about what it reads: QIODevice::read(n) reserves n bytes up front, which for
// the project limit would commit over 512 MiB to open a small file.
QByteArray readBounded(QIODevice &device, qint64 maxBytes);

} // namespace ProjectFile

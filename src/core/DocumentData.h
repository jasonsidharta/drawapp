#pragma once

#include "TiledImage.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <vector>

// Limits shared by the editor and the file formats, so every document the app can create can
// also be reopened.
//
// Since milestone 10 layers are sparse (TiledImage), so the memory a document uses depends on its
// painted area and is bounded by MemoryBudget at allocation time, not by its canvas size. The
// dense limit below remains for the version 1 and 2 project formats, which store dense layers.
namespace DocumentLimits {
inline constexpr int MaxSide = 16384;
inline constexpr qint64 MaxLayerPixels = qint64(MaxSide) * MaxSide;
inline constexpr int MaxLayers = 64;
inline constexpr qint64 MaxDensePixelBytes = qint64(512) * 1024 * 1024; // v1/v2 files, all layers
inline constexpr int MaxNameLength = 256;
} // namespace DocumentLimits

// Plain value types for a layered raster document. Copies share pixel data (TiledImage
// copy-on-write): a copy is cheap, and whoever writes to a shared tile copies it first, so a copy
// handed to another thread never sees later edits.
struct LayerData
{
    quint64 id = 0;      // stable within the document, never reused, saved in project files
    QString name;
    TiledImage image;    // premultiplied ARGB32 tiles, document size
    bool visible = true;
    qreal opacity = 1.0; // 0..1
    bool locked = false; // blocks pixel edits (brush, eraser, clear)
};

struct DocumentData
{
    QSize size;
    std::vector<LayerData> layers; // bottom to top
    quint64 activeLayerId = 0;

    // A one-layer document holding image (used for version 1 projects and in tests).
    static DocumentData fromImage(const QImage &image, const QString &layerName = QStringLiteral("Layer 1"));
    // All visible layers composited with Normal (source-over) blending and their opacity, as one
    // dense image (export, tests). Null if the memory budget refuses it.
    QImage composite() const;
};

// Clears rect in target to transparent, then composites the visible layers over it, bottom to
// top, with source-over and each layer's opacity. target is premultiplied ARGB32 in document
// coordinates (dense) or a tiled image of the document's size.
void compositeLayers(QImage &target, const QRect &rect, const std::vector<LayerData> &layers);
// Tile by tile; tiles under which no visible layer has a tile become (or stay) unallocated.
// False if the memory budget refused a tile (that tile is then left as it was).
bool compositeLayers(TiledImage &target, const QRect &rect, const std::vector<LayerData> &layers);

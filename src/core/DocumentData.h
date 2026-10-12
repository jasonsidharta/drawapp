#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <vector>

// Limits shared by the editor and the file formats, so every document the app can create can
// also be reopened.
namespace DocumentLimits {
inline constexpr int MaxSide = 16384;
inline constexpr qint64 MaxLayerPixels = qint64(8192) * 8192;
inline constexpr int MaxLayers = 64;
inline constexpr qint64 MaxPixelBytes = qint64(512) * 1024 * 1024; // all layers together
inline constexpr int MaxNameLength = 256;
} // namespace DocumentLimits

// Plain value types for a layered raster document. Copies share pixel data (QImage implicit
// sharing): a copy is cheap, and whoever writes to a shared image detaches first, so a copy
// handed to another thread never sees later edits.
struct LayerData
{
    quint64 id = 0;      // stable within the document, never reused, saved in project files
    QString name;
    QImage image;        // premultiplied ARGB32, document size
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
    // All visible layers composited with Normal (source-over) blending and their opacity.
    QImage composite() const;
};

// Clears rect in target to transparent, then composites the visible layers over it, bottom to
// top, with source-over and each layer's opacity. target is premultiplied ARGB32.
void compositeLayers(QImage &target, const QRect &rect, const std::vector<LayerData> &layers);

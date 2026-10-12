#pragma once

#include "BrushSettings.h"

#include <QQuickImageProvider>

// Preset thumbnails drawn by the real StrokeEngine: "image://brushthumb/<preset id>/<revision>".
// BrushLibrary publishes the settings for each id; requests render on QML's image loader thread.
class BrushThumbnails : public QQuickImageProvider
{
public:
    static constexpr int DefaultWidth = 168;
    static constexpr int DefaultHeight = 52;

    BrushThumbnails();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    // Thread-safe: the settings to draw for a preset id from now on.
    static void publish(const QString &presetId, const BrushSettings &settings);
    // The stroke a thumbnail shows, at the given size (also used by tests).
    static QImage render(const BrushSettings &settings, QSize size);
};

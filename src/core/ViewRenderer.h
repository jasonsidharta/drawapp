#pragma once

#include "TiledImage.h"
#include "ViewTransform.h"

#include <QImage>
#include <QRect>

class QPainter;

namespace ViewRenderer {

inline constexpr QRgb WorkspaceColor = 0xff3c3f44;
inline constexpr QRgb CheckerLight = 0xffffffff;
inline constexpr QRgb CheckerDark = 0xffd6d6d6;
inline constexpr int CheckerSize = 12;

// Draws the part of the view inside exposed (view pixels): workspace background, a checkerboard
// where the document is transparent, then the document through the view transform.
// Work is limited to exposed, so a small brush update repaints a small area.
void render(QPainter &painter, const QImage &document, const ViewTransform &view, const QRect &exposed);
// The same for a tiled document: the visible part is assembled into one dense image first, so
// filtering across tile edges is exactly as for one image (no seams).
void render(QPainter &painter, const TiledImage &document, const ViewTransform &view, const QRect &exposed);

} // namespace ViewRenderer

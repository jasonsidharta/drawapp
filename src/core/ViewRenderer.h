#pragma once

#include "TiledImage.h"
#include "ViewPyramid.h"
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
// The view as the canvas draws it (milestone 10): zoomed out, from the pyramid level that is the
// smallest still at least as detailed as the screen (ViewPyramid::levelFor), so the work and the
// memory depend on the view's size rather than the document's. Above 50 % zoom the pixels are
// exactly those of the overload above. If memory for a level is refused, a coarser level is
// tried; if none fits, only the checkerboard is drawn.
void render(QPainter &painter, const TiledImage &document, ViewPyramid &pyramid, const ViewTransform &view,
            const QRect &exposed);

} // namespace ViewRenderer

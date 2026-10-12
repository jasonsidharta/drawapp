#pragma once

#include <QImage>
#include <QPainterPath>
#include <QRect>
#include <QSize>

// How a new shape combines with the existing selection.
//   Replace:   the shape becomes the selection
//   Add:       min(1, old + new)   (a saturating sum, so shapes that share an anti-aliased edge
//                                    join without a half-transparent seam)
//   Subtract:  max(0, old − new)
//   Intersect: min(old, new)
// "No selection" combines as nothing selected (adding to it gives the shape; subtracting from or
// intersecting with it gives an empty selection).
enum class SelectionMode { Replace, Add, Subtract, Intersect };

// Which document pixels edits may change, kept apart from layer pixels and visibility: coverage
// 0..255 per document pixel (QImage::Format_Grayscale8, the document's size). Three states:
//
//   none    no selection: every pixel is editable (the default)
//   active  a mask: 0 = protected, 255 = fully editable, values between are soft edges
//   empty   active with nothing selected: **no** pixel is editable
//
// "Empty" never turns into "none" by itself, so an empty result can never let an edit apply
// everywhere. Only Deselect (or replacing the document) returns to "none".
//
// A value type: copies share the mask until one of them changes (QImage copy-on-write).
class Selection
{
public:
    Selection() = default; // none
    // Every pixel selected.
    static Selection all(QSize size);
    // A selection from a mask (converted to Grayscale8); a null image gives "none".
    static Selection fromMask(const QImage &mask);

    bool isActive() const { return !m_mask.isNull(); }
    bool isEmpty() const { return isActive() && m_bounds.isEmpty(); }
    const QImage &mask() const { return m_mask; }
    // Bounding rectangle of the selected (non-zero) pixels; empty for none and empty.
    QRect bounds() const { return m_bounds; }
    // 0..255; 255 everywhere when there is no selection.
    int coverageAt(int x, int y) const;
    qsizetype byteSize() const { return m_mask.isNull() ? 0 : m_mask.sizeInBytes(); }
    bool operator==(const Selection &other) const;
    bool operator!=(const Selection &other) const { return !(*this == other); }

    // A filled shape as a document-size coverage mask. Anti-aliased edges carry partial coverage;
    // a path that crosses itself is filled with the nonzero winding rule (everything enclosed).
    static QImage rasterize(QSize size, const QPainterPath &path, bool antialias = true);
    // This selection combined with a shape mask from rasterize() (same size).
    Selection combined(const QImage &shape, SelectionMode mode) const;
    // 255 − coverage. Inverting "none" gives "none" (there is nothing to invert).
    Selection inverted() const;
    // The boundary between pixels with coverage >= 128 and the rest, as horizontal and vertical
    // runs along pixel edges, in document pixels. Empty for none and empty.
    QPainterPath outline() const;

private:
    void updateBounds();

    QImage m_mask;
    QRect m_bounds;
};

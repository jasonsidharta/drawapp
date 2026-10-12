#pragma once

#include "Selection.h"
#include "TiledImage.h"

#include <atomic>

#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <vector>

// An interactive affine transform (move, scale, rotate, flip) of one or more layers' pixels: the
// selected part when there is a selection, otherwise each layer's artwork. All layers share one
// geometry over one source rectangle (the selection's bounds, or the union of the layers'
// content), so they stay aligned with each other.
//
// begin() caches, per layer, over the source rectangle only:
//   source = layer × selection
//   base   = layer × (1 − selection), with complementary rounding so source + base = layer
// Outside the rectangle the original layer is read (shared, not copied).
// Every preview is rendered from this cache (never from an earlier preview), into a separate
// image: the document is not touched until the caller applies the result. Colour is resampled
// premultiplied and clamped to alpha, so transparent pixels cannot leak colour into edges.
//
// Geometry: a source point q maps to  center + R(rotation) · S(scaleX, scaleY) · (q − c0),  c0
// being the source rectangle's centre. Negative scales are flips. The pivot (rotation centre) is
// attached to the content.
class TransformSession
{
public:
    enum class Handle {
        None, Move, Rotate, Pivot,
        TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left
    };
    enum class Quality { Fast, Final }; // bilinear while dragging; bicubic (Catmull–Rom) for the result

    // Scale limits: at most 32×, and never smaller than one pixel across.
    static constexpr double MaxScale = 32.0;

    // Starts a transform of layer (premultiplied ARGB32, document size). Returns false, leaving
    // the session inactive, when there is nothing to transform: an empty selection, or no
    // selection and a fully transparent layer.
    bool begin(const QImage &layer, const Selection &selection);
    // Several layers (all the document's size): one part each, in the given order. Returns false,
    // leaving the session inactive, if there is nothing to transform or memory runs out. With no
    // layers it transforms the selection's outline only (it needs a non-empty selection).
    bool begin(const std::vector<QImage> &layers, const Selection &selection);
    // The document's tiled layers (milestone 10): each is kept as a copy-on-write snapshot (no
    // pixels copied), and dense buffers are made only over the source rectangle. Those buffers
    // are reserved against MemoryBudget; begin() fails if the budget refuses them.
    bool begin(const std::vector<TiledImage> &layers, const Selection &selection);
    int partCount() const { return int(m_parts.size()); }
    // Memory held by the session's caches.
    qsizetype byteSize() const;
    void end();
    bool isActive() const { return !m_sourceRect.isEmpty(); }
    // The last begin() failed because memory for its caches could not be had (not because there
    // was nothing to transform).
    bool lastBeginOutOfMemory() const { return m_beginOutOfMemory; }

    QSize documentSize() const { return m_docSize; }
    QRect sourceRect() const { return m_sourceRect; }
    bool hasSelection() const { return m_selection.isActive(); }

    QPointF center() const { return m_center; }
    double scaleX() const { return m_sx; }
    double scaleY() const { return m_sy; }
    double rotation() const { return m_rotation; } // degrees, clockwise on screen
    QPointF pivot() const;                          // document position of the rotation centre
    // Size of the transformed box in pixels (always positive).
    double width() const { return std::abs(m_sx) * m_sourceRect.width(); }
    double height() const { return std::abs(m_sy) * m_sourceRect.height(); }
    bool isIdentity() const;

    // The transformed source rectangle: top-left, top-right, bottom-right, bottom-left.
    QPolygonF quad() const;
    // Pixel-aligned bounds of quad() (not clipped to the canvas).
    QRect targetRect() const;
    // The layer area Apply changes: (source ∪ target) ∩ canvas.
    QRect changedRect() const;

    // --- editing ---
    // Without scaling or rotation the position snaps to whole pixels (no resampling).
    void moveBy(const QPointF &delta);
    void setCenter(const QPointF &center);
    // Rotates around the pivot to an absolute angle.
    void setRotation(double degrees);
    // Scales by dragging a handle to pointer (document coordinates); the opposite corner or edge
    // stays put, or with fromCenter the box's centre. keepAspect scales both axes by the same
    // factor, also from edge handles.
    void dragScaleHandle(Handle handle, const QPointF &pointer, bool keepAspect, bool fromCenter = false);
    // Sets the box size in pixels around the pivot.
    void setSize(double width, double height);
    void setPivot(const QPointF &docPos);
    void flipHorizontal();
    void flipVertical();
    void reset();

    // Which part of the transform box is at pos. tolerance and knobDistance are in document
    // pixels (the caller converts from screen pixels). Inside the box is Move; near the box but
    // not on a handle is Rotate.
    Handle hitTest(const QPointF &pos, double tolerance, double knobDistance) const;
    QPointF handlePosition(Handle handle, double knobDistance) const;

    // --- rendering ---
    // Writes the transformed layer (base with the transformed source over it) into out
    // (document size) within area.
    void render(QImage &out, const QRect &area, Quality quality) const; // part 0
    void render(int part, QImage &out, const QRect &area, Quality quality) const;
    // Into a tiled image of the document's size, band by band (only the area's tiles change).
    // False if the memory budget refused a tile or `cancel` was set (out then partly updated:
    // previews, or a result that is thrown away). rowsDone, if given, counts finished rows.
    // Safe on a worker thread for a session no one modifies meanwhile (see workerCopy()).
    bool render(int part, TiledImage &out, const QRect &area, Quality quality, const std::atomic<bool> *cancel = nullptr,
                std::atomic<qint64> *rowsDone = nullptr) const;
    // A copy for a worker thread: shares every cache (nothing is copied), but does not own the
    // memory reservation, so only this session releases it. Keep this session active until the
    // worker is done with the copy.
    TransformSession workerCopy() const
    {
        TransformSession s = *this;
        s.m_reserved = 0;
        return s;
    }
    // The transformed layer within area, as a new image of area's size (null if out of memory).
    QImage renderRegion(int part, const QRect &area, Quality quality) const;
    // Tests: renderRegion and the tiled render for this part behave as if memory ran out (-1 = off).
    void failRenderRegionForTesting(int part) { m_failRegionPart = part; }
    // The selection after Apply: the source's mask, transformed. "None" when there was none.
    Selection transformedSelection() const;

    QPointF mapToDocument(const QPointF &sourcePoint) const;
    QPointF mapToSource(const QPointF &docPoint) const;

private:
    QPointF localHandle(Handle handle) const; // relative to c0, unscaled
    void keepPivotAt(const QPointF &pivotDoc);
    struct Part
    {
        TiledImage original;          // the layer as it was (a copy-on-write snapshot)
        QImage base;                  // layer − source over the source rectangle
        std::vector<QImage> source;   // source pixels and 2× reductions (premultiplied ARGB32)
    };
    template <typename RowFn>
    void renderRows(const Part &part, const QRect &area, Quality quality, RowFn rowAt) const;
    void sampleRow(const Part &part, int y, int x0, int x1, QRgb *out, Quality quality) const;
    static void readOriginal(const TiledImage &image, int y, int x0, int x1, QRgb *dst);
    void clampScales();
    void snapUnscaledMove();

    QSize m_docSize;
    QRect m_sourceRect;
    Selection m_selection;
    std::vector<Part> m_parts;
    int m_failRegionPart = -1;
    QImage m_sourceMask;           // the selection over m_sourceRect (Grayscale8), when selected
    qint64 m_reserved = 0;         // MemoryBudget bytes held for the dense caches
    bool m_beginOutOfMemory = false;

    QPointF m_center;
    double m_sx = 1, m_sy = 1;
    double m_rotation = 0;
    QPointF m_pivotLocal; // relative to c0, in source pixels
};

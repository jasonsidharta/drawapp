#include "TransformSession.h"

#include <QSemaphore>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace {

constexpr double Pi = 3.14159265358979323846;

inline quint32 mul255(quint32 x, quint32 y)
{
    const quint32 t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

// Rows y0..y1-1 on a pool of our own (so a busy global pool, e.g. a save, cannot stall the UI);
// the calling thread takes part, so the work finishes even if no pool thread starts.
template <typename F>
void parallelRows(int y0, int y1, F &&rowFn)
{
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
        return true;
    }();
    Q_UNUSED(configured);
    const int rows = y1 - y0;
    const int helpers = std::min(pool.maxThreadCount(), rows / 32);
    if (helpers <= 0) {
        for (int y = y0; y < y1; ++y)
            rowFn(y);
        return;
    }
    std::atomic<int> next{y0};
    constexpr int Batch = 8;
    auto work = [&] {
        for (;;) {
            const int start = next.fetch_add(Batch);
            if (start >= y1)
                break;
            for (int y = start; y < std::min(y1, start + Batch); ++y)
                rowFn(y);
        }
    };
    QSemaphore done;
    int started = 0;
    for (int i = 0; i < helpers; ++i) {
        if (pool.tryStart([&] {
                work();
                done.release();
            }))
            ++started;
    }
    work();
    done.acquire(started);
}

// Catmull–Rom: interpolating, so whole-pixel positions reproduce the source exactly (a plain
// move or flip stays sharp). Its slight overshoot is clamped (alpha to 0..255, colour to alpha).
inline double cubicWeight(double x)
{
    x = std::abs(x);
    if (x < 1)
        return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    if (x < 2)
        return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    return 0.0;
}

// Premultiplied 2× box reduction.
QImage halve(const QImage &src)
{
    const int w = std::max(1, src.width() / 2), h = std::max(1, src.height() / 2);
    QImage dst(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        const QRgb *r0 = reinterpret_cast<const QRgb *>(src.constScanLine(std::min(2 * y, src.height() - 1)));
        const QRgb *r1 = reinterpret_cast<const QRgb *>(src.constScanLine(std::min(2 * y + 1, src.height() - 1)));
        QRgb *out = reinterpret_cast<QRgb *>(dst.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int x0 = std::min(2 * x, src.width() - 1), x1 = std::min(2 * x + 1, src.width() - 1);
            const QRgb a = r0[x0], b = r0[x1], c = r1[x0], d = r1[x1];
            out[x] = qRgba((qRed(a) + qRed(b) + qRed(c) + qRed(d) + 2) / 4, (qGreen(a) + qGreen(b) + qGreen(c) + qGreen(d) + 2) / 4,
                           (qBlue(a) + qBlue(b) + qBlue(c) + qBlue(d) + 2) / 4,
                           (qAlpha(a) + qAlpha(b) + qAlpha(c) + qAlpha(d) + 2) / 4);
        }
    }
    return dst;
}

inline QRgb pixelOrClear(const QImage &img, int x, int y)
{
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height())
        return 0;
    return reinterpret_cast<const QRgb *>(img.constScanLine(y))[x];
}

QRgb bilinear(const QImage &img, double x, double y)
{
    // x, y: continuous coordinates with pixel centres at i + 0.5.
    x -= 0.5;
    y -= 0.5;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    const QRgb p00 = pixelOrClear(img, x0, y0), p10 = pixelOrClear(img, x0 + 1, y0);
    const QRgb p01 = pixelOrClear(img, x0, y0 + 1), p11 = pixelOrClear(img, x0 + 1, y0 + 1);
    const double w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
    auto ch = [&](int shift) {
        return int(((p00 >> shift) & 255) * w00 + ((p10 >> shift) & 255) * w10 + ((p01 >> shift) & 255) * w01
                   + ((p11 >> shift) & 255) * w11 + 0.5);
    };
    return qRgba(ch(16), ch(8), ch(0), ch(24));
}

QRgb bicubic(const QImage &img, double x, double y)
{
    x -= 0.5;
    y -= 0.5;
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const double fx = x - xi, fy = y - yi;
    double wx[4], wy[4];
    for (int i = 0; i < 4; ++i) {
        wx[i] = cubicWeight(fx - (i - 1));
        wy[i] = cubicWeight(fy - (i - 1));
    }
    double acc[4] = {0, 0, 0, 0};
    for (int j = 0; j < 4; ++j) {
        double row[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            const QRgb p = pixelOrClear(img, xi + i - 1, yi + j - 1);
            row[0] += qRed(p) * wx[i];
            row[1] += qGreen(p) * wx[i];
            row[2] += qBlue(p) * wx[i];
            row[3] += qAlpha(p) * wx[i];
        }
        for (int c = 0; c < 4; ++c)
            acc[c] += row[c] * wy[j];
    }
    if (acc[3] <= 0)
        return 0;
    // Overshoot past opaque: scale colour with alpha so the colour itself stays true (clamping
    // alpha alone would leave the colour overshot, a light or saturated fringe).
    if (acc[3] > 255) {
        const double k = 255.0 / acc[3];
        for (double &c : acc)
            c *= k;
    }
    const int a = std::clamp(int(acc[3] + 0.5), 0, 255);
    // Premultiplied: colour may not exceed alpha.
    auto ch = [&](double v) { return std::clamp(int(v + 0.5), 0, a); };
    return qRgba(ch(acc[0]), ch(acc[1]), ch(acc[2]), a);
}

inline QRgb over(QRgb src, QRgb dst)
{
    const quint32 keep = 255 - qAlpha(src);
    if (keep == 255)
        return dst;
    return qRgba(int(qRed(src) + mul255(qRed(dst), keep)), int(qGreen(src) + mul255(qGreen(dst), keep)),
                 int(qBlue(src) + mul255(qBlue(dst), keep)), int(qAlpha(src) + mul255(qAlpha(dst), keep)));
}

double sign(double v)
{
    return v < 0 ? -1.0 : 1.0;
}

} // namespace

bool TransformSession::begin(const QImage &layer, const Selection &selection)
{
    return begin(std::vector<QImage>{layer}, selection);
}

bool TransformSession::begin(const std::vector<QImage> &layers, const Selection &selection)
{
    std::vector<TiledImage> tiled;
    for (const QImage &layer : layers) {
        if (layer.isNull() || layer.format() != QImage::Format_ARGB32_Premultiplied)
            return false;
        tiled.push_back(TiledImage::fromImage(layer));
        if (tiled.back().isNull())
            return false;
    }
    if (layers.empty())
        return begin(std::vector<TiledImage>{}, selection);
    return begin(tiled, selection);
}

bool TransformSession::begin(const std::vector<TiledImage> &layers, const Selection &selection)
{
    end();
    m_beginOutOfMemory = false;
    // No layers: the selection's outline alone (Transform Selection).
    if (layers.empty()) {
        if (!selection.isActive() || selection.isEmpty())
            return false;
        m_sourceMask = selection.mask().copy(selection.bounds());
        m_selection = selection;
        m_docSize = selection.mask().size();
        m_sourceRect = selection.bounds();
        reset();
        return true;
    }
    const QSize size = layers.front().size();
    for (const TiledImage &layer : layers)
        if (layer.isNull() || layer.size() != size)
            return false;
    // One source rectangle for every layer, so all of them share one mapping and stay aligned.
    QRect rect;
    if (selection.isActive()) {
        if (selection.isEmpty())
            return false;
        rect = selection.bounds() & QRect(QPoint(0, 0), size);
    } else {
        for (const TiledImage &layer : layers)
            rect |= layer.contentBounds();
    }
    if (rect.isEmpty())
        return false;
    // The dense caches: source per layer plus its reductions (about a third), and the base only
    // with a selection (without one everything in the rectangle is source and the base is
    // transparent: on the Android emulator five 4096² layers plus the merged preview otherwise
    // held 868 of 980 MiB, and Apply had no room left).
    const bool withBase = selection.isActive();
    const qint64 perLayer = qint64(rect.width()) * rect.height() * 4;
    const qint64 wanted = qint64(layers.size()) * (perLayer * (withBase ? 2 : 1) + perLayer / 3 + 64);
    if (!MemoryBudget::tryReserve(wanted, MemoryBudget::Category::Preview)) {
        m_beginOutOfMemory = true;
        return false;
    }
    m_reserved = wanted;
    std::vector<Part> parts;
    parts.reserve(layers.size());
    for (const TiledImage &layer : layers) {
        Part part;
        part.original = layer; // a snapshot: shares the layer's tiles, read only
        if (withBase)
            part.base = QImage(rect.size(), QImage::Format_ARGB32_Premultiplied);
        QImage source(rect.size(), QImage::Format_ARGB32_Premultiplied);
        if ((withBase && part.base.isNull()) || source.isNull()) {
            end(); // out of memory: nothing started
            m_beginOutOfMemory = true;
            return false;
        }
        // source = layer × s, base = layer − source (exactly complementary). Without a selection
        // everything in the rectangle is source.
        std::vector<QRgb> row(size_t(rect.width()));
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            readOriginal(layer, y, rect.left(), rect.right() + 1, row.data());
            const QRgb *in = row.data() - rect.left();
            const uchar *s = selection.isActive() ? selection.mask().constScanLine(y) : nullptr;
            QRgb *src = reinterpret_cast<QRgb *>(source.scanLine(y - rect.top()));
            if (!withBase) {
                std::memcpy(src, in + rect.left(), size_t(rect.width()) * 4);
                continue;
            }
            QRgb *base = reinterpret_cast<QRgb *>(part.base.scanLine(y - rect.top()));
            for (int x = rect.left(); x <= rect.right(); ++x) {
                const QRgb p = in[x];
                const quint32 c = s ? s[x] : 255;
                const QRgb taken = c == 255 ? p
                                            : qRgba(int(mul255(qRed(p), c)), int(mul255(qGreen(p), c)),
                                                    int(mul255(qBlue(p), c)), int(mul255(qAlpha(p), c)));
                src[x - rect.left()] = taken;
                base[x - rect.left()] = qRgba(qRed(p) - qRed(taken), qGreen(p) - qGreen(taken), qBlue(p) - qBlue(taken),
                                              qAlpha(p) - qAlpha(taken));
            }
        }
        part.source.push_back(source);
        while (part.source.back().width() > 1 || part.source.back().height() > 1)
            part.source.push_back(halve(part.source.back()));
        parts.push_back(std::move(part));
    }
    if (selection.isActive()) {
        m_sourceMask = selection.mask().copy(rect);
        m_selection = selection;
    }
    m_parts = std::move(parts);
    m_docSize = size;
    m_sourceRect = rect;
    reset();
    return true;
}

void TransformSession::end()
{
    if (m_reserved > 0) {
        MemoryBudget::release(m_reserved, MemoryBudget::Category::Preview);
        m_reserved = 0;
    }
    m_docSize = QSize();
    m_sourceRect = QRect();
    m_selection = Selection();
    m_parts.clear();
    m_sourceMask = QImage();
}

qsizetype TransformSession::byteSize() const
{
    qsizetype bytes = m_sourceMask.sizeInBytes();
    for (const Part &part : m_parts) {
        bytes += part.base.sizeInBytes();
        for (const QImage &level : part.source)
            bytes += level.sizeInBytes();
    }
    return bytes;
}

void TransformSession::reset()
{
    m_center = QRectF(m_sourceRect).center();
    m_sx = m_sy = 1;
    m_rotation = 0;
    m_pivotLocal = QPointF();
}

QPointF TransformSession::mapToDocument(const QPointF &q) const
{
    const QPointF c0 = QRectF(m_sourceRect).center();
    const double dx = (q.x() - c0.x()) * m_sx, dy = (q.y() - c0.y()) * m_sy;
    const double a = m_rotation * Pi / 180.0, c = std::cos(a), s = std::sin(a);
    return m_center + QPointF(dx * c - dy * s, dx * s + dy * c);
}

QPointF TransformSession::mapToSource(const QPointF &p) const
{
    const QPointF c0 = QRectF(m_sourceRect).center();
    const double a = m_rotation * Pi / 180.0, c = std::cos(a), s = std::sin(a);
    const double dx = p.x() - m_center.x(), dy = p.y() - m_center.y();
    const double rx = dx * c + dy * s, ry = -dx * s + dy * c;
    return c0 + QPointF(rx / m_sx, ry / m_sy);
}

QPointF TransformSession::pivot() const
{
    return mapToDocument(QRectF(m_sourceRect).center() + m_pivotLocal);
}

void TransformSession::keepPivotAt(const QPointF &pivotDoc)
{
    m_center += pivotDoc - pivot();
}

bool TransformSession::isIdentity() const
{
    const QPointF c0 = QRectF(m_sourceRect).center();
    const double r = std::fmod(std::abs(m_rotation), 360.0);
    return std::abs(m_sx - 1) < 1e-9 && std::abs(m_sy - 1) < 1e-9 && (r < 1e-9 || std::abs(r - 360) < 1e-9)
        && std::abs(m_center.x() - c0.x()) < 1e-9 && std::abs(m_center.y() - c0.y()) < 1e-9;
}

QPolygonF TransformSession::quad() const
{
    const QRectF r(m_sourceRect);
    return QPolygonF({mapToDocument(r.topLeft()), mapToDocument(r.topRight()), mapToDocument(r.bottomRight()),
                      mapToDocument(r.bottomLeft())});
}

QRect TransformSession::targetRect() const
{
    if (!isActive())
        return {};
    // Bicubic support reaches two source pixels beyond the edge.
    const int margin = int(std::ceil(2.0 * std::max(std::abs(m_sx), std::abs(m_sy)))) + 1;
    return quad().boundingRect().toAlignedRect().adjusted(-margin, -margin, margin, margin);
}

QRect TransformSession::changedRect() const
{
    if (!isActive())
        return {};
    return (m_sourceRect | targetRect()) & QRect(QPoint(0, 0), m_docSize);
}

void TransformSession::clampScales()
{
    const double minX = 1.0 / std::max(1, m_sourceRect.width()), minY = 1.0 / std::max(1, m_sourceRect.height());
    auto clampOne = [](double s, double lo) {
        if (!std::isfinite(s))
            return 1.0;
        return sign(s) * std::clamp(std::abs(s), lo, MaxScale);
    };
    m_sx = clampOne(m_sx, minX);
    m_sy = clampOne(m_sy, minY);
}

void TransformSession::snapUnscaledMove()
{
    // Without scaling or rotation, pixels land exactly on pixels: moving never resamples.
    if (std::abs(std::abs(m_sx) - 1) > 1e-9 || std::abs(std::abs(m_sy) - 1) > 1e-9 || std::abs(m_rotation) > 1e-9)
        return;
    const QPointF c0 = QRectF(m_sourceRect).center();
    m_center.setX(std::round(m_center.x() - m_sx * c0.x()) + m_sx * c0.x());
    m_center.setY(std::round(m_center.y() - m_sy * c0.y()) + m_sy * c0.y());
}

void TransformSession::moveBy(const QPointF &delta)
{
    if (!std::isfinite(delta.x()) || !std::isfinite(delta.y()))
        return;
    m_center += delta;
    snapUnscaledMove();
}

void TransformSession::setCenter(const QPointF &center)
{
    if (!std::isfinite(center.x()) || !std::isfinite(center.y()))
        return;
    m_center = center;
    snapUnscaledMove();
}

void TransformSession::setRotation(double degrees)
{
    if (!std::isfinite(degrees))
        return;
    const QPointF p = pivot();
    degrees = std::fmod(degrees, 360.0);
    if (degrees > 180)
        degrees -= 360;
    if (degrees <= -180)
        degrees += 360;
    m_rotation = degrees;
    keepPivotAt(p);
}

void TransformSession::setSize(double width, double height)
{
    if (!std::isfinite(width) || !std::isfinite(height) || m_sourceRect.isEmpty())
        return;
    const QPointF p = pivot();
    m_sx = sign(m_sx) * width / m_sourceRect.width();
    m_sy = sign(m_sy) * height / m_sourceRect.height();
    clampScales();
    keepPivotAt(p);
}

void TransformSession::setPivot(const QPointF &docPos)
{
    if (std::isfinite(docPos.x()) && std::isfinite(docPos.y()))
        m_pivotLocal = mapToSource(docPos) - QRectF(m_sourceRect).center();
}

void TransformSession::flipHorizontal()
{
    const QPointF p = pivot();
    m_sx = -m_sx;
    keepPivotAt(p);
}

void TransformSession::flipVertical()
{
    const QPointF p = pivot();
    m_sy = -m_sy;
    keepPivotAt(p);
}

QPointF TransformSession::localHandle(Handle handle) const
{
    const double w = m_sourceRect.width() / 2.0, h = m_sourceRect.height() / 2.0;
    switch (handle) {
    case Handle::TopLeft: return {-w, -h};
    case Handle::Top: return {0, -h};
    case Handle::TopRight: return {w, -h};
    case Handle::Right: return {w, 0};
    case Handle::BottomRight: return {w, h};
    case Handle::Bottom: return {0, h};
    case Handle::BottomLeft: return {-w, h};
    case Handle::Left: return {-w, 0};
    default: return {};
    }
}

void TransformSession::dragScaleHandle(Handle handle, const QPointF &pointer, bool keepAspect, bool fromCenter)
{
    if (handle < Handle::TopLeft || !std::isfinite(pointer.x()) || !std::isfinite(pointer.y()))
        return;
    const QPointF c0 = QRectF(m_sourceRect).center();
    const QPointF lh = localHandle(handle), la = fromCenter ? QPointF() : -lh;
    const QPointF anchorDoc = mapToDocument(c0 + la);
    const double a = m_rotation * Pi / 180.0, c = std::cos(a), s = std::sin(a);
    const QPointF v = pointer - anchorDoc;
    const QPointF q(v.x() * c + v.y() * s, -v.x() * s + v.y() * c); // in the box's frame
    const QPointF d0 = lh - la;                                       // unscaled handle offset
    const bool horizontal = d0.x() != 0, vertical = d0.y() != 0;
    double nsx = m_sx, nsy = m_sy;
    if (keepAspect) {
        // One factor for both axes: the projection of the pointer onto the handle's direction.
        const QPointF d(horizontal ? d0.x() * m_sx : 0, vertical ? d0.y() * m_sy : 0);
        const double len2 = d.x() * d.x() + d.y() * d.y();
        double k = len2 > 0 ? (q.x() * d.x() + q.y() * d.y()) / len2 : 1.0;
        const double minK = std::max(1.0 / (std::abs(m_sx) * m_sourceRect.width()),
                                     1.0 / (std::abs(m_sy) * m_sourceRect.height()));
        k = std::max(k, minK);
        nsx = m_sx * k;
        nsy = m_sy * k;
    } else {
        // Dragging past the opposite side does not flip (use Flip); it stops at one pixel.
        if (horizontal) {
            const double v2 = q.x() / d0.x();
            nsx = (sign(v2) == sign(m_sx)) ? v2 : sign(m_sx) * 1e-9;
        }
        if (vertical) {
            const double v2 = q.y() / d0.y();
            nsy = (sign(v2) == sign(m_sy)) ? v2 : sign(m_sy) * 1e-9;
        }
    }
    m_sx = nsx;
    m_sy = nsy;
    clampScales();
    // Keep the anchor where it was.
    m_center += anchorDoc - mapToDocument(c0 + la);
}

QPointF TransformSession::handlePosition(Handle handle, double knobDistance) const
{
    const QPointF c0 = QRectF(m_sourceRect).center();
    switch (handle) {
    case Handle::Pivot:
        return pivot();
    case Handle::Rotate: {
        // Beyond the top edge, on the side away from the box.
        const QPointF top = mapToDocument(c0 + localHandle(Handle::Top));
        const QPointF dir = top - mapToDocument(c0);
        const double len = std::hypot(dir.x(), dir.y());
        const QPointF unit = len > 1e-9 ? dir / len : QPointF(0, -1);
        return top + unit * knobDistance;
    }
    case Handle::None:
    case Handle::Move:
        return m_center;
    default:
        return mapToDocument(c0 + localHandle(handle));
    }
}

TransformSession::Handle TransformSession::hitTest(const QPointF &pos, double tolerance, double knobDistance) const
{
    if (!isActive())
        return Handle::None;
    auto near = [&](const QPointF &p) { return std::hypot(p.x() - pos.x(), p.y() - pos.y()) <= tolerance; };
    // Corners first: on a small box they win over edges.
    for (Handle h : {Handle::TopLeft, Handle::TopRight, Handle::BottomRight, Handle::BottomLeft})
        if (near(handlePosition(h, knobDistance)))
            return h;
    if (near(handlePosition(Handle::Rotate, knobDistance)))
        return Handle::Rotate;
    for (Handle h : {Handle::Top, Handle::Right, Handle::Bottom, Handle::Left})
        if (near(handlePosition(h, knobDistance)))
            return h;
    // The pivot's own target is small, and smaller on small boxes, so dragging from the middle of
    // a small selection moves it instead of grabbing the pivot.
    const double minSide = std::min(width(), height());
    if (std::hypot(pos.x() - pivot().x(), pos.y() - pivot().y()) <= std::min(tolerance * 0.45, minSide * 0.15))
        return Handle::Pivot;
    if (quad().containsPoint(pos, Qt::OddEvenFill))
        return Handle::Move;
    return Handle::Rotate;
}

void TransformSession::render(QImage &out, const QRect &requested, Quality quality) const
{
    render(0, out, requested, quality);
}

void TransformSession::render(int partIndex, QImage &out, const QRect &requested, Quality quality) const
{
    if (!isActive() || partIndex < 0 || partIndex >= int(m_parts.size()) || out.size() != m_docSize
        || out.format() != QImage::Format_ARGB32_Premultiplied)
        return;
    const QRect area = requested & out.rect();
    if (area.isEmpty())
        return;
    // Rows are written from several threads: detach out here, once, and hand them a plain
    // pointer. QImage::scanLine() on a still-shared image (a preview copy of a document layer)
    // would detach concurrently from every thread.
    uchar *bits = out.bits();
    const qsizetype bpl = out.bytesPerLine();
    renderRows(m_parts[size_t(partIndex)], area, quality,
               [&](int y) { return reinterpret_cast<QRgb *>(bits + y * bpl) + area.left(); });
}

QImage TransformSession::renderRegion(int partIndex, const QRect &requested, Quality quality) const
{
    if (!isActive() || partIndex < 0 || partIndex >= int(m_parts.size()))
        return {};
    const QRect area = requested & QRect(QPoint(0, 0), m_docSize);
    if (area.isEmpty())
        return {};
    if (partIndex == m_failRegionPart)
        return {}; // simulated allocation failure (tests)
    QImage region(area.size(), QImage::Format_ARGB32_Premultiplied);
    if (region.isNull())
        return {}; // out of memory
    uchar *bits = region.bits(); // see render(): no scanLine() from the worker threads
    const qsizetype bpl = region.bytesPerLine();
    renderRows(m_parts[size_t(partIndex)], area, quality,
               [&](int y) { return reinterpret_cast<QRgb *>(bits + (y - area.top()) * bpl); });
    return region;
}

template <typename RowFn>
void TransformSession::renderRows(const Part &part, const QRect &area, Quality quality, RowFn rowAt) const
{
    // Base pixels: inside the source rectangle the remainder (layer − source), outside it the
    // original layer.
    auto baseRow = [&](int y, QRgb *dst, int x0, int x1) {
        readOriginal(part.original, y, x0, x1, dst);
        if (y < m_sourceRect.top() || y > m_sourceRect.bottom())
            return;
        const int b0 = std::max(x0, m_sourceRect.left()), b1 = std::min(x1, m_sourceRect.right() + 1);
        if (b0 >= b1)
            return;
        if (part.base.isNull()) { // no selection: everything in the rectangle moves
            std::memset(dst + (b0 - x0), 0, size_t(b1 - b0) * 4);
            return;
        }
        const QRgb *base = reinterpret_cast<const QRgb *>(part.base.constScanLine(y - m_sourceRect.top()));
        std::memcpy(dst + (b0 - x0), base + (b0 - m_sourceRect.left()), size_t(b1 - b0) * 4);
    };
    if (isIdentity()) {
        // Nothing moved: base + source is exactly the original layer (with a soft selection,
        // "source over base" would not be: two 50 % halves composite to 75 %).
        for (int y = area.top(); y <= area.bottom(); ++y)
            readOriginal(part.original, y, area.left(), area.right() + 1, rowAt(y));
        return;
    }
    const QRect target = targetRect() & area;
    parallelRows(area.top(), area.bottom() + 1, [&](int y) {
        QRgb *row = rowAt(y); // row[0] is x = area.left()
        baseRow(y, row, area.left(), area.right() + 1);
        if (target.isEmpty() || y < target.top() || y > target.bottom())
            return;
        sampleRow(part, y, target.left(), target.right() + 1, row + (target.left() - area.left()), quality);
    });
}

void TransformSession::readOriginal(const TiledImage &image, int y, int x0, int x1, QRgb *dst)
{
    // Tile segment by tile segment; a missing tile is transparent. Safe on worker threads: the
    // image is a snapshot whose tiles nobody writes.
    for (int x = x0; x < x1;) {
        const int end = std::min(x1, (image.columnOf(x) + 1) * image.tileSize());
        const QRgb *src = image.constRow(x, y);
        if (src)
            std::memcpy(dst + (x - x0), src, size_t(end - x) * 4);
        else
            std::memset(dst + (x - x0), 0, size_t(end - x) * 4);
        x = end;
    }
}

bool TransformSession::render(int partIndex, TiledImage &out, const QRect &requested, Quality quality,
                              const std::atomic<bool> *cancel, std::atomic<qint64> *rowsDone) const
{
    if (!isActive() || partIndex < 0 || partIndex >= int(m_parts.size()) || out.size() != m_docSize)
        return false;
    if (partIndex == m_failRegionPart)
        return false; // simulated allocation failure (tests)
    const QRect area = requested & out.rect();
    if (area.isEmpty())
        return true;
    // A band of tile rows at a time (sixteen: enough rows that the row pool uses every thread):
    // rendered in parallel into a dense band, then written. Cancellation is checked between bands.
    const int band = out.tileSize() * 16;
    QImage buffer(area.width(), band, QImage::Format_ARGB32_Premultiplied);
    if (buffer.isNull())
        return false;
    uchar *bits = buffer.bits(); // detached here, on this thread; rows are written through bits
    const qsizetype bpl = buffer.bytesPerLine();
    for (int top = area.top(); top <= area.bottom();) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return false;
        const int bottom = std::min(area.bottom(), (top / band + 1) * band - 1);
        const QRect strip(area.left(), top, area.width(), bottom - top + 1);
        renderRows(m_parts[size_t(partIndex)], strip, quality,
                   [&](int y) { return reinterpret_cast<QRgb *>(bits + (y - top) * bpl); });
        if (!out.write(strip.topLeft(), QImage(bits, strip.width(), strip.height(), bpl, QImage::Format_ARGB32_Premultiplied)))
            return false;
        if (rowsDone)
            rowsDone->fetch_add(strip.height(), std::memory_order_relaxed);
        top = bottom + 1;
    }
    return true;
}

void TransformSession::sampleRow(const Part &part, int y, int x0, int x1, QRgb *out, Quality quality) const
{
    // out[i] is pixel x0 + i and already holds the base; the transformed source goes over it.
    // The reduction level whose scale is in (0.5, 1] for the smaller scale, so large reductions
    // are prefiltered instead of aliasing.
    const double shrink = std::min(std::abs(m_sx), std::abs(m_sy));
    int level = 0;
    if (shrink < 1.0)
        level = std::min(int(part.source.size()) - 1, std::max(0, int(std::floor(std::log2(1.0 / shrink)))));
    const QImage &img = part.source[size_t(level)];
    const double levelScaleX = double(img.width()) / m_sourceRect.width();
    const double levelScaleY = double(img.height()) / m_sourceRect.height();
    const QPointF origin = m_sourceRect.topLeft();
    // Source position is affine in x: step once per pixel.
    QPointF p = mapToSource(QPointF(x0 + 0.5, y + 0.5)) - origin;
    const QPointF step = mapToSource(QPointF(x0 + 1.5, y + 0.5)) - origin - p;
    // A whole-pixel move or flip lands every pixel centre on a source pixel centre: a copy.
    const double fx = p.x() - 0.5, fy = p.y() - 0.5;
    if (std::abs(std::abs(step.x()) - 1) < 1e-9 && std::abs(step.y()) < 1e-9 && std::abs(fx - std::round(fx)) < 1e-6
        && std::abs(fy - std::round(fy)) < 1e-6) {
        const QImage &src = part.source.front();
        const int sy = int(std::round(fy));
        const int dx = step.x() > 0 ? 1 : -1;
        const QRgb *srcRow = (sy >= 0 && sy < src.height()) ? reinterpret_cast<const QRgb *>(src.constScanLine(sy)) : nullptr;
        int sx = int(std::round(fx));
        for (int x = x0; x < x1; ++x, sx += dx) {
            const QRgb s = (srcRow && sx >= 0 && sx < src.width()) ? srcRow[sx] : 0;
            if (qAlpha(s))
                out[x - x0] = over(s, out[x - x0]);
        }
        return;
    }
    for (int x = x0; x < x1; ++x, p += step) {
        const double sx = p.x() * levelScaleX, sy = p.y() * levelScaleY;
        QRgb src = 0;
        if (sx > -2 && sy > -2 && sx < img.width() + 2 && sy < img.height() + 2)
            src = quality == Quality::Final ? bicubic(img, sx, sy) : bilinear(img, sx, sy);
        if (qAlpha(src))
            out[x - x0] = over(src, out[x - x0]);
    }
}

Selection TransformSession::transformedSelection() const
{
    if (!isActive() || !m_selection.isActive())
        return Selection();
    QImage mask(m_docSize, QImage::Format_Grayscale8);
    mask.fill(0);
    const QRect target = targetRect() & mask.rect();
    const QPointF origin = m_sourceRect.topLeft();
    uchar *bits = mask.bits(); // plain pointer for the worker threads (see render())
    const qsizetype bpl = mask.bytesPerLine();
    parallelRows(target.top(), target.bottom() + 1, [&](int y) {
        uchar *row = bits + y * bpl;
        for (int x = target.left(); x <= target.right(); ++x) {
            const QPointF p = mapToSource(QPointF(x + 0.5, y + 0.5)) - origin - QPointF(0.5, 0.5);
            const int xi = int(std::floor(p.x())), yi = int(std::floor(p.y()));
            const double fx = p.x() - xi, fy = p.y() - yi;
            auto at = [&](int ix, int iy) -> double {
                if (ix < 0 || iy < 0 || ix >= m_sourceMask.width() || iy >= m_sourceMask.height())
                    return 0.0;
                return m_sourceMask.constScanLine(iy)[ix];
            };
            const double v = at(xi, yi) * (1 - fx) * (1 - fy) + at(xi + 1, yi) * fx * (1 - fy)
                           + at(xi, yi + 1) * (1 - fx) * fy + at(xi + 1, yi + 1) * fx * fy;
            row[x] = uchar(std::clamp(int(v + 0.5), 0, 255));
        }
    });
    return Selection::fromMask(mask);
}

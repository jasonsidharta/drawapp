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
    end();
    if (layer.isNull() || layer.format() != QImage::Format_ARGB32_Premultiplied)
        return false;
    QRect rect;
    QImage source;
    if (selection.isActive()) {
        if (selection.isEmpty())
            return false;
        rect = selection.bounds() & layer.rect();
        if (rect.isEmpty())
            return false;
        // source = layer × s, base = layer − source: exactly complementary.
        source = QImage(rect.size(), QImage::Format_ARGB32_Premultiplied);
        m_base = layer.copy();
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            const uchar *s = selection.mask().constScanLine(y);
            QRgb *base = reinterpret_cast<QRgb *>(m_base.scanLine(y));
            QRgb *src = reinterpret_cast<QRgb *>(source.scanLine(y - rect.top()));
            for (int x = rect.left(); x <= rect.right(); ++x) {
                const QRgb p = base[x];
                const quint32 c = s[x];
                const QRgb taken = qRgba(int(mul255(qRed(p), c)), int(mul255(qGreen(p), c)), int(mul255(qBlue(p), c)),
                                         int(mul255(qAlpha(p), c)));
                src[x - rect.left()] = taken;
                base[x] = qRgba(qRed(p) - qRed(taken), qGreen(p) - qGreen(taken), qBlue(p) - qBlue(taken),
                                qAlpha(p) - qAlpha(taken));
            }
        }
        m_sourceMask = selection.mask().copy(rect);
        m_selection = selection;
    } else {
        // The layer's content bounds.
        int left = layer.width(), right = -1, top = layer.height(), bottom = -1;
        for (int y = 0; y < layer.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(layer.constScanLine(y));
            for (int x = 0; x < layer.width(); ++x) {
                if (qAlpha(row[x])) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = y;
                }
            }
        }
        if (right < 0)
            return false;
        rect = QRect(QPoint(left, top), QPoint(right, bottom));
        source = layer.copy(rect);
    }
    m_docSize = layer.size();
    m_sourceRect = rect;
    m_source.push_back(source);
    while (m_source.back().width() > 1 || m_source.back().height() > 1)
        m_source.push_back(halve(m_source.back()));
    reset();
    return true;
}

void TransformSession::end()
{
    m_docSize = QSize();
    m_sourceRect = QRect();
    m_selection = Selection();
    m_base = QImage();
    m_source.clear();
    m_sourceMask = QImage();
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

void TransformSession::dragScaleHandle(Handle handle, const QPointF &pointer, bool keepAspect)
{
    if (handle < Handle::TopLeft || !std::isfinite(pointer.x()) || !std::isfinite(pointer.y()))
        return;
    const QPointF c0 = QRectF(m_sourceRect).center();
    const QPointF lh = localHandle(handle), la = -lh;
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
    if (near(pivot()))
        return Handle::Pivot;
    if (quad().containsPoint(pos, Qt::OddEvenFill))
        return Handle::Move;
    return Handle::Rotate;
}

void TransformSession::render(QImage &out, const QRect &requested, Quality quality) const
{
    if (!isActive() || out.size() != m_docSize || out.format() != QImage::Format_ARGB32_Premultiplied)
        return;
    const QRect area = requested & out.rect();
    if (area.isEmpty())
        return;
    if (isIdentity()) {
        // Nothing moved: base + source is exactly the original layer (with a soft selection,
        // "source over base" would not be: two 50 % halves composite to 75 %).
        for (int y = area.top(); y <= area.bottom(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
            const QRgb *base = m_base.isNull() ? nullptr : reinterpret_cast<const QRgb *>(m_base.constScanLine(y));
            const bool inSource = y >= m_sourceRect.top() && y <= m_sourceRect.bottom();
            const QRgb *src = inSource ? reinterpret_cast<const QRgb *>(m_source.front().constScanLine(y - m_sourceRect.top())) : nullptr;
            for (int x = area.left(); x <= area.right(); ++x) {
                const QRgb b = base ? base[x] : 0;
                const QRgb p = (src && x >= m_sourceRect.left() && x <= m_sourceRect.right()) ? src[x - m_sourceRect.left()] : 0;
                row[x] = qRgba(qRed(b) + qRed(p), qGreen(b) + qGreen(p), qBlue(b) + qBlue(p), qAlpha(b) + qAlpha(p));
            }
        }
        return;
    }
    const QRect target = targetRect() & area;
    parallelRows(area.top(), area.bottom() + 1, [&](int y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
        const QRgb *base = m_base.isNull() ? nullptr : reinterpret_cast<const QRgb *>(m_base.constScanLine(y));
        if (y < target.top() || y > target.bottom() || target.isEmpty()) {
            for (int x = area.left(); x <= area.right(); ++x)
                row[x] = base ? base[x] : 0;
            return;
        }
        for (int x = area.left(); x < target.left(); ++x)
            row[x] = base ? base[x] : 0;
        for (int x = target.right() + 1; x <= area.right(); ++x)
            row[x] = base ? base[x] : 0;
        sampleRow(y, target.left(), target.right() + 1, row, base, quality);
    });
}

void TransformSession::sampleRow(int y, int x0, int x1, QRgb *out, const QRgb *base, Quality quality) const
{
    // The reduction level whose scale is in (0.5, 1] for the smaller scale, so large reductions
    // are prefiltered instead of aliasing.
    const double shrink = std::min(std::abs(m_sx), std::abs(m_sy));
    int level = 0;
    if (shrink < 1.0)
        level = std::min(int(m_source.size()) - 1, std::max(0, int(std::floor(std::log2(1.0 / shrink)))));
    const QImage &img = m_source[size_t(level)];
    const double levelScaleX = double(img.width()) / m_sourceRect.width();
    const double levelScaleY = double(img.height()) / m_sourceRect.height();
    const QPointF origin = m_sourceRect.topLeft();
    // Source position is affine in x: step once per pixel.
    QPointF p = mapToSource(QPointF(x0 + 0.5, y + 0.5)) - origin;
    const QPointF step = mapToSource(QPointF(x0 + 1.5, y + 0.5)) - origin - p;
    for (int x = x0; x < x1; ++x, p += step) {
        const double sx = p.x() * levelScaleX, sy = p.y() * levelScaleY;
        QRgb src = 0;
        if (sx > -2 && sy > -2 && sx < img.width() + 2 && sy < img.height() + 2)
            src = quality == Quality::Final ? bicubic(img, sx, sy) : bilinear(img, sx, sy);
        const QRgb under = base ? base[x] : 0;
        out[x] = qAlpha(src) ? over(src, under) : under;
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
    parallelRows(target.top(), target.bottom() + 1, [&](int y) {
        uchar *row = mask.scanLine(y);
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

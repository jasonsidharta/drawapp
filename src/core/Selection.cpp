#include "Selection.h"

#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

namespace {

QImage blankMask(QSize size)
{
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    return mask;
}

} // namespace

Selection Selection::all(QSize size)
{
    Selection s;
    s.m_mask = QImage(size, QImage::Format_Grayscale8);
    s.m_mask.fill(255);
    s.m_bounds = s.m_mask.rect();
    return s;
}

Selection Selection::fromMask(const QImage &mask)
{
    Selection s;
    if (mask.isNull())
        return s;
    s.m_mask = mask.format() == QImage::Format_Grayscale8 ? mask : mask.convertToFormat(QImage::Format_Grayscale8);
    s.updateBounds();
    return s;
}

void Selection::updateBounds()
{
    int left = m_mask.width(), right = -1, top = m_mask.height(), bottom = -1;
    for (int y = 0; y < m_mask.height(); ++y) {
        const uchar *row = m_mask.constScanLine(y);
        int x0 = 0;
        while (x0 < m_mask.width() && row[x0] == 0)
            ++x0;
        if (x0 == m_mask.width())
            continue;
        int x1 = m_mask.width() - 1;
        while (row[x1] == 0)
            --x1;
        left = std::min(left, x0);
        right = std::max(right, x1);
        top = std::min(top, y);
        bottom = y;
    }
    m_bounds = right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

int Selection::coverageAt(int x, int y) const
{
    if (!isActive())
        return 255;
    if (x < 0 || y < 0 || x >= m_mask.width() || y >= m_mask.height())
        return 0;
    return m_mask.constScanLine(y)[x];
}

bool Selection::operator==(const Selection &other) const
{
    if (isActive() != other.isActive())
        return false;
    return !isActive() || m_mask == other.m_mask;
}

QImage Selection::rasterize(QSize size, const QPainterPath &path, bool antialias)
{
    // QPainter fills Alpha8 images directly; the bytes are then the coverage.
    QImage alpha(size, QImage::Format_Alpha8);
    alpha.fill(0);
    if (!path.isEmpty()) {
        QPainterPath filled = path;
        filled.setFillRule(Qt::WindingFill);
        QPainter p(&alpha);
        p.setRenderHint(QPainter::Antialiasing, antialias);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawPath(filled);
    }
    alpha.reinterpretAsFormat(QImage::Format_Grayscale8);
    return alpha;
}

Selection Selection::combined(const QImage &shape, SelectionMode mode) const
{
    const QImage shapeMask =
        shape.format() == QImage::Format_Grayscale8 ? shape : shape.convertToFormat(QImage::Format_Grayscale8);
    if (mode == SelectionMode::Replace || (!isActive() && mode == SelectionMode::Add))
        return fromMask(shapeMask);
    // "None" combines as nothing selected.
    const QImage old = isActive() ? m_mask : blankMask(shapeMask.size());
    QImage out(shapeMask.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < out.height(); ++y) {
        const uchar *a = old.constScanLine(y);
        const uchar *b = shapeMask.constScanLine(y);
        uchar *o = out.scanLine(y);
        for (int x = 0; x < out.width(); ++x) {
            switch (mode) {
            case SelectionMode::Add: o[x] = uchar(std::min(255, a[x] + b[x])); break;
            case SelectionMode::Subtract: o[x] = uchar(std::max(0, a[x] - b[x])); break;
            default: o[x] = std::min(a[x], b[x]); break;
            }
        }
    }
    Selection s;
    s.m_mask = out;
    s.updateBounds();
    return s;
}

Selection Selection::inverted() const
{
    if (!isActive())
        return *this;
    Selection s;
    s.m_mask = QImage(m_mask.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < m_mask.height(); ++y) {
        const uchar *a = m_mask.constScanLine(y);
        uchar *o = s.m_mask.scanLine(y);
        for (int x = 0; x < m_mask.width(); ++x)
            o[x] = uchar(255 - a[x]);
    }
    s.updateBounds();
    return s;
}

QPainterPath Selection::outline() const
{
    QPainterPath path;
    if (!isActive() || m_bounds.isEmpty())
        return path;
    const int w = m_mask.width(), h = m_mask.height();
    auto in = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && m_mask.constScanLine(y)[x] >= 128;
    };
    const QRect r = m_bounds.adjusted(-1, -1, 1, 1);
    // Horizontal edges: between row y − 1 and row y, merged into runs.
    for (int y = r.top() + 1; y <= r.bottom(); ++y) {
        int start = -1;
        for (int x = r.left(); x <= r.right() + 1; ++x) {
            const bool edge = x <= r.right() && in(x, y - 1) != in(x, y);
            if (edge && start < 0)
                start = x;
            if (!edge && start >= 0) {
                path.moveTo(start, y);
                path.lineTo(x, y);
                start = -1;
            }
        }
    }
    // Vertical edges: between column x − 1 and column x.
    for (int x = r.left() + 1; x <= r.right(); ++x) {
        int start = -1;
        for (int y = r.top(); y <= r.bottom() + 1; ++y) {
            const bool edge = y <= r.bottom() && in(x - 1, y) != in(x, y);
            if (edge && start < 0)
                start = y;
            if (!edge && start >= 0) {
                path.moveTo(x, start);
                path.lineTo(x, y);
                start = -1;
            }
        }
    }
    return path;
}

// --- wand, grow, shrink, feather ---

namespace {

inline int channelDistance(QRgb a, QRgb b)
{
    if (qAlpha(a) == 0 && qAlpha(b) == 0)
        return 0; // transparent is transparent, whatever colour is stored
    return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b)),
                     std::abs(qAlpha(a) - qAlpha(b))});
}

// Squared Euclidean distance transform of one line (Felzenszwalb and Huttenlocher).
void distance1d(const std::vector<float> &f, std::vector<float> &d, std::vector<int> &v, std::vector<float> &z, int n)
{
    constexpr float Inf = 1e20f;
    int k = 0;
    v[0] = 0;
    z[0] = -Inf;
    z[1] = Inf;
    for (int q = 1; q < n; ++q) {
        float s = ((f[q] + float(q) * q) - (f[v[k]] + float(v[k]) * v[k])) / (2.0f * q - 2.0f * v[k]);
        while (s <= z[k]) {
            --k;
            s = ((f[q] + float(q) * q) - (f[v[k]] + float(v[k]) * v[k])) / (2.0f * q - 2.0f * v[k]);
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = Inf;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < q)
            ++k;
        d[q] = float(q - v[k]) * float(q - v[k]) + f[v[k]];
    }
}

// Distance in pixels from each pixel of rect r to the nearest pixel where inside() holds
// (pixels outside r count as not inside). Row-major, r.width() per row.
std::vector<float> distanceToInside(const QRect &r, const std::function<bool(int, int)> &inside)
{
    const int w = r.width(), h = r.height();
    constexpr float Inf = 1e20f;
    std::vector<float> grid(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            grid[size_t(y) * w + x] = inside(r.left() + x, r.top() + y) ? 0.0f : Inf;
    const int n = std::max(w, h);
    std::vector<float> f(n), d(n), z(n + 1);
    std::vector<int> v(n);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y)
            f[y] = grid[size_t(y) * w + x];
        distance1d(f, d, v, z, h);
        for (int y = 0; y < h; ++y)
            grid[size_t(y) * w + x] = d[y];
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            f[x] = grid[size_t(y) * w + x];
        distance1d(f, d, v, z, w);
        for (int x = 0; x < w; ++x)
            grid[size_t(y) * w + x] = std::sqrt(d[x]);
    }
    return grid;
}

// One horizontal or vertical box blur of radius r of a Grayscale8 image within rect
// (unselected beyond rect's ends).
void boxBlur(QImage &img, const QRect &rect, int r, bool horizontal)
{
    const int len = horizontal ? rect.width() : rect.height();
    const int lines = horizontal ? rect.height() : rect.width();
    std::vector<int> line(len), out(len);
    const int window = 2 * r + 1;
    for (int l = 0; l < lines; ++l) {
        for (int i = 0; i < len; ++i)
            line[i] = horizontal ? img.constScanLine(rect.top() + l)[rect.left() + i]
                                 : img.constScanLine(rect.top() + i)[rect.left() + l];
        long sum = 0;
        for (int i = 0; i <= r && i < len; ++i)
            sum += line[i];
        for (int i = 0; i < len; ++i) {
            out[i] = int((sum + window / 2) / window);
            const int add = i + r + 1, drop = i - r;
            if (add < len)
                sum += line[add];
            if (drop >= 0)
                sum -= line[drop];
        }
        for (int i = 0; i < len; ++i) {
            if (horizontal)
                img.scanLine(rect.top() + l)[rect.left() + i] = uchar(out[i]);
            else
                img.scanLine(rect.top() + i)[rect.left() + l] = uchar(out[i]);
        }
    }
}

} // namespace

namespace {
// The wand over any pixel source: at(x, y) gives premultiplied ARGB.
template<typename At>
QImage wandMask(QSize size, At at, QPoint seed, int tolerance, bool contiguous, bool antialias);
}

QImage Selection::magicWand(const QImage &image, QPoint seed, int tolerance, bool contiguous, bool antialias)
{
    if (!image.rect().contains(seed) || image.format() != QImage::Format_ARGB32_Premultiplied) {
        QImage mask(image.size(), QImage::Format_Grayscale8);
        mask.fill(0);
        return mask;
    }
    return wandMask(
        image.size(), [&](int x, int y) { return reinterpret_cast<const QRgb *>(image.constScanLine(y))[x]; }, seed,
        tolerance, contiguous, antialias);
}

QImage Selection::magicWand(const TiledImage &image, QPoint seed, int tolerance, bool contiguous, bool antialias)
{
    return wandMask(image.size(), [&](int x, int y) { return image.pixel(x, y); }, seed, tolerance, contiguous, antialias);
}

namespace {
template<typename At>
QImage wandMask(QSize size, At at, QPoint seed, int tolerance, bool contiguous, bool antialias)
{
    QImage mask(size, QImage::Format_Grayscale8);
    if (mask.isNull())
        return mask;
    mask.fill(0);
    if (!QRect(QPoint(0, 0), size).contains(seed))
        return mask;
    tolerance = std::clamp(tolerance, 0, 255);
    const int w = size.width(), h = size.height();
    const QRgb ref = at(seed.x(), seed.y());
    auto matches = [&](int x, int y) { return channelDistance(at(x, y), ref) <= tolerance; };
    if (contiguous) {
        // Scanline flood fill, 4-connected.
        std::vector<QPoint> stack{seed};
        while (!stack.empty()) {
            const QPoint p = stack.back();
            stack.pop_back();
            uchar *row = mask.scanLine(p.y());
            if (row[p.x()] || !matches(p.x(), p.y()))
                continue;
            int x0 = p.x(), x1 = p.x();
            while (x0 > 0 && !row[x0 - 1] && matches(x0 - 1, p.y()))
                --x0;
            while (x1 < w - 1 && !row[x1 + 1] && matches(x1 + 1, p.y()))
                ++x1;
            for (int x = x0; x <= x1; ++x)
                row[x] = 255;
            for (int dy : {-1, 1}) {
                const int y = p.y() + dy;
                if (y < 0 || y >= h)
                    continue;
                const uchar *other = mask.constScanLine(y);
                bool open = false;
                for (int x = x0; x <= x1; ++x) {
                    const bool candidate = !other[x] && matches(x, y);
                    if (candidate && !open)
                        stack.push_back(QPoint(x, y));
                    open = candidate;
                }
            }
        }
    } else {
        for (int y = 0; y < h; ++y) {
            uchar *row = mask.scanLine(y);
            for (int x = 0; x < w; ++x)
                row[x] = matches(x, y) ? 255 : 0;
        }
    }
    if (antialias) {
        // A one-pixel ring of partial coverage for neighbours just beyond the tolerance.
        constexpr int Soft = 16;
        QImage ring = mask.copy();
        for (int y = 0; y < h; ++y) {
            const uchar *up = mask.constScanLine(std::max(0, y - 1)), *row = mask.constScanLine(y),
                        *down = mask.constScanLine(std::min(h - 1, y + 1));
            uchar *out = ring.scanLine(y);
            for (int x = 0; x < w; ++x) {
                if (row[x])
                    continue;
                const bool touches = (x > 0 && row[x - 1]) || (x < w - 1 && row[x + 1]) || up[x] || down[x];
                if (!touches)
                    continue;
                const int over = channelDistance(at(x, y), ref) - tolerance;
                if (over < Soft)
                    out[x] = uchar(255 * (Soft - over) / (Soft + 1));
            }
        }
        mask = ring;
    }
    return mask;
}
} // namespace

Selection Selection::grown(double radius) const
{
    if (!isActive() || !(radius > 0) || !std::isfinite(radius) || m_bounds.isEmpty())
        return *this;
    radius = std::min(radius, 1000.0);
    const int pad = int(std::ceil(radius)) + 2;
    const QRect r = m_bounds.adjusted(-pad, -pad, pad, pad) & m_mask.rect();
    const std::vector<float> dist = distanceToInside(r, [&](int x, int y) { return m_mask.constScanLine(y)[x] >= 128; });
    Selection s;
    s.m_mask = m_mask.copy();
    for (int y = r.top(); y <= r.bottom(); ++y) {
        uchar *row = s.m_mask.scanLine(y);
        for (int x = r.left(); x <= r.right(); ++x) {
            const float d = dist[size_t(y - r.top()) * r.width() + (x - r.left())];
            const int edge = std::clamp(int(std::lround((radius + 0.5 - d) * 255.0)), 0, 255);
            row[x] = uchar(std::max<int>(row[x], edge));
        }
    }
    s.updateBounds();
    return s;
}

Selection Selection::shrunk(double radius) const
{
    if (!isActive() || !(radius > 0) || !std::isfinite(radius))
        return *this;
    // Shrinking the selection is growing the unselected part.
    return inverted().grown(radius).inverted();
}

Selection Selection::feathered(double radius) const
{
    if (!isActive() || !(radius > 0) || !std::isfinite(radius) || m_bounds.isEmpty())
        return *this;
    radius = std::min(radius, 500.0);
    // Three box passes; each pass of radius b has variance b(b + 1) / 3, so three of them give a
    // spread close to a Gaussian of sigma = radius / 2.
    const double sigma = radius / 2.0;
    const int box = std::max(1, int(std::lround((std::sqrt(4.0 * sigma * sigma + 1.0) - 1.0) / 2.0)));
    const int pad = 3 * box + 2;
    const QRect r = m_bounds.adjusted(-pad, -pad, pad, pad) & m_mask.rect();
    Selection s;
    s.m_mask = m_mask.copy();
    for (int pass = 0; pass < 3; ++pass) {
        boxBlur(s.m_mask, r, box, true);
        boxBlur(s.m_mask, r, box, false);
    }
    s.updateBounds();
    return s;
}

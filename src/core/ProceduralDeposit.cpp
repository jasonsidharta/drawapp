#include "ProceduralDeposit.h"

#include <QColor>

#include <algorithm>
#include <cmath>

using namespace BrushRaster;

namespace {

constexpr quint64 Salt = StrokeSalt::Milestone11 + 0x200;

// Distance from p to the segment a-b, and where along it (0..1) the nearest point is.
inline double segmentDistance(double px, double py, double ax, double ay, double bx, double by, double &t)
{
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    t = len2 > 1e-12 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0, 1.0) : 0.0;
    const double cx = ax + t * dx - px, cy = ay + t * dy - py;
    return std::sqrt(cx * cx + cy * cy);
}

double widthScale(const BrushSettings &s, const DabParams &dab)
{
    return dab.diameter / std::max(1e-6, s.size);
}

} // namespace

ProceduralDeposit::ProceduralDeposit(StrokeContext &context)
    : CoverageDeposit(context)
{
    const BrushSettings &st = m_ctx.settings;
    // Per-element colour (variation, grass shading) needs a colour per pixel; erasing has none.
    if (st.blend == BrushBlend::Paint
        && ((st.procColorJitter > 0 && st.stamp != BrushStamp::Pattern) || st.stamp == BrushStamp::Grass))
        m_ctx.colorPerDab = true;
}

double ProceduralDeposit::reach(const BrushSettings &s, const DabParams &dab)
{
    const double radius = dab.diameter / 2.0;
    const double width = s.procWidth * widthScale(s, dab);
    const double spread = radius * std::max(0.0, s.procSpread);
    switch (s.stamp) {
    case BrushStamp::Strands:
    case BrushStamp::Grass:
        return spread + s.procLength * dab.diameter + width + dab.stepFraction * dab.diameter;
    case BrushStamp::Foliage: return spread + s.procLength * dab.diameter + width;
    case BrushStamp::Splatter: return spread + width / 2.0 * (1.0 + s.procStretch) * 3.2 + width;
    case BrushStamp::Pattern:
        return std::max({s.procLength * dab.diameter, dab.stepFraction * dab.diameter, dab.diameter}) + width;
    default: return radius;
    }
}

double ProceduralDeposit::rnd(const DabParams &dab, int element, int stream) const
{
    return m_ctx.random(dab.index * 128 + element, Salt + quint64(stream));
}

double ProceduralDeposit::heading(const DabParams &dab) const
{
    const BrushSettings &st = m_ctx.settings;
    switch (st.procDirection) {
    case ProcDirection::Up: return -Pi / 2.0 + dab.angle;
    case ProcDirection::Angle: return dab.angle;
    case ProcDirection::Path:
    default:
        // Follow direction already turns the dab angle with the stroke.
        return dab.angle + (dab.hasDirection && !st.followDirection ? dab.direction : 0.0);
    }
}

QRgb ProceduralDeposit::elementColor(const DabParams &dab, int element) const
{
    const double j = m_ctx.settings.procColorJitter;
    if (j <= 0 || !m_ctx.colorPerDab)
        return dab.color;
    QColor c = QColor::fromRgb(dab.color);
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    const double dv = (rnd(dab, element, 20) * 2.0 - 1.0) * 0.5 * j;
    const double dh = (rnd(dab, element, 21) * 2.0 - 1.0) * 20.0 / 360.0 * j;
    const double hue = h < 0 ? 0.0 : std::fmod(h + dh + 1.0, 1.0);
    c = QColor::fromHsvF(float(hue), s, float(std::clamp(v + dv, 0.0, 1.0)));
    return qRgb(c.red(), c.green(), c.blue());
}

void ProceduralDeposit::rasterize(const DabParams &dab, const QRect &rect)
{
    switch (m_ctx.settings.stamp) {
    case BrushStamp::Strands: strands(dab, rect, false); break;
    case BrushStamp::Grass: strands(dab, rect, true); break;
    case BrushStamp::Foliage: foliage(dab, rect); break;
    case BrushStamp::Splatter: splatter(dab, rect); break;
    case BrushStamp::Pattern: pattern(dab, rect); break;
    default: CoverageDeposit::rasterize(dab, rect); break;
    }
}

void ProceduralDeposit::strands(const DabParams &dab, const QRect &rect, bool grass)
{
    const BrushSettings &st = m_ctx.settings;
    const double radius = dab.diameter / 2.0;
    const double width = st.procWidth * widthScale(st, dab);
    const double base = heading(dab);
    const double jitter = st.procAngleJitter * Pi / 180.0;
    // Grass roots lie across the stroke (perpendicular to its path), spread a little along it.
    const double path = dab.hasDirection ? dab.direction : 0.0;
    const QPointF across(-std::sin(path), std::cos(path)), along(std::cos(path), std::sin(path));
    // A strand that follows the path needs one: the dab before the stroke has moved draws none.
    if (st.procDirection == ProcDirection::Path && !dab.hasDirection && !st.followDirection)
        return;
    const int n = std::clamp(st.procCount, 1, 64);
    for (int e = 0; e < n; ++e) {
        QPointF root;
        if (grass) {
            const double lateral = (2.0 * rnd(dab, e, 0) - 1.0) * radius * st.procSpread;
            const double ahead = (rnd(dab, e, 1) - 0.5) * dab.stepFraction * dab.diameter;
            root = dab.center + across * lateral + along * ahead;
        } else {
            const double r = radius * st.procSpread * std::sqrt(rnd(dab, e, 0));
            const double a = 2.0 * Pi * rnd(dab, e, 1);
            root = dab.center + QPointF(r * std::cos(a), r * std::sin(a));
        }
        const double h0 = base + (2.0 * rnd(dab, e, 2) - 1.0) * jitter;
        const double length = st.procLength * dab.diameter * (1.0 - st.procLengthJitter * rnd(dab, e, 3));
        if (length <= 0.05)
            continue;
        // Strands curl evenly; blades droop, bending more towards the tip.
        const double turn = grass ? st.procCurl * Pi / 2.0 * (0.5 + rnd(dab, e, 4)) : st.procCurl * Pi * (0.5 + rnd(dab, e, 4));
        const int segments = std::clamp(int(std::ceil(length / 4.0)), 2, 16);
        QPointF points[17];
        points[0] = root;
        // (Explicit bounds: QRectF's union ignores zero-size rectangles.)
        double x0 = root.x(), y0 = root.y(), x1 = root.x(), y1 = root.y();
        for (int i = 0; i < segments; ++i) {
            const double s = (i + 0.5) / segments;
            const double h = h0 + turn * (grass ? s * s : s);
            points[i + 1] = points[i] + QPointF(std::cos(h), std::sin(h)) * (length / segments);
            x0 = std::min(x0, points[i + 1].x());
            y0 = std::min(y0, points[i + 1].y());
            x1 = std::max(x1, points[i + 1].x());
            y1 = std::max(y1, points[i + 1].y());
        }
        const double r0 = width / 2.0;
        if (!begin(QRectF(QPointF(x0 - r0 - 1, y0 - r0 - 1), QPointF(x1 + r0 + 1, y1 + r0 + 1)), rect))
            continue;
        for (int i = 0; i < segments; ++i) {
            const double t0 = double(i) / segments, t1 = double(i + 1) / segments;
            capsule(points[i], points[i + 1], r0 * (1.0 - st.procTaper * t0), r0 * (1.0 - st.procTaper * t1), t0, t1);
        }
        end(dab, elementColor(dab, e), grass);
    }
}

void ProceduralDeposit::foliage(const DabParams &dab, const QRect &rect)
{
    const BrushSettings &st = m_ctx.settings;
    const double radius = dab.diameter / 2.0;
    const double width = st.procWidth * widthScale(st, dab);
    const double base = heading(dab);
    const double jitter = st.procAngleJitter * Pi / 180.0;
    const int n = std::clamp(st.procCount, 1, 64);
    for (int e = 0; e < n; ++e) {
        const double r = radius * st.procSpread * std::sqrt(rnd(dab, e, 0));
        const double a = 2.0 * Pi * rnd(dab, e, 1);
        const QPointF at = dab.center + QPointF(r * std::cos(a), r * std::sin(a));
        const double angle = base + (2.0 * rnd(dab, e, 2) - 1.0) * jitter;
        const double k = 1.0 - st.procLengthJitter * rnd(dab, e, 3);
        const double length = st.procLength * dab.diameter * k, w = width * k;
        if (length <= 0.3)
            continue;
        const QPointF tip = at + QPointF(std::cos(angle), std::sin(angle)) * length;
        QRectF bounds = QRectF(at, tip).normalized();
        if (!begin(bounds.adjusted(-w - 1, -w - 1, w + 1, w + 1), rect))
            continue;
        lens(at, angle, length, w);
        end(dab, elementColor(dab, e));
    }
}

void ProceduralDeposit::splatter(const DabParams &dab, const QRect &rect)
{
    const BrushSettings &st = m_ctx.settings;
    const double radius = dab.diameter / 2.0;
    const double largest = st.procWidth * widthScale(st, dab) / 2.0;
    const double base = heading(dab);
    const double jitter = st.procAngleJitter * Pi / 180.0;
    const int n = std::clamp(st.procCount, 1, 64);
    for (int e = 0; e < n; ++e) {
        // Power-law sizes: few large drops, many small ones.
        const double rd = largest * std::pow(rnd(dab, e, 3), 1.0 + 3.0 * st.procLengthJitter);
        if (rd < 0.15)
            continue;
        const double r = radius * st.procSpread * std::sqrt(rnd(dab, e, 0));
        const double a = 2.0 * Pi * rnd(dab, e, 1);
        const QPointF at = dab.center + QPointF(r * std::cos(a), r * std::sin(a));
        const double dir = base + (2.0 * rnd(dab, e, 2) - 1.0) * jitter;
        const double len = rd * (1.0 + st.procStretch * rnd(dab, e, 5));
        const QPointF unit(std::cos(dir), std::sin(dir));
        // Satellites: large stretched drops throw a few droplets ahead.
        int satellites = 0;
        if (st.procStretch > 0 && rd > 0.5 * largest)
            satellites = 1 + int(rnd(dab, e, 6) * 3.0);
        QRectF bounds(at - QPointF(len + 1, len + 1), at + QPointF(len + 1, len + 1));
        QPointF sat[3];
        double satR[3];
        for (int i = 0; i < satellites; ++i) {
            const double dist = len * (1.6 + 1.4 * rnd(dab, e, 7 + i));
            const double side = (rnd(dab, e, 10 + i) - 0.5) * len * 0.6;
            sat[i] = at + unit * dist + QPointF(-unit.y(), unit.x()) * side;
            satR[i] = rd * (0.15 + 0.2 * rnd(dab, e, 13 + i));
            bounds |= QRectF(sat[i] - QPointF(satR[i] + 1, satR[i] + 1), sat[i] + QPointF(satR[i] + 1, satR[i] + 1));
        }
        if (!begin(bounds, rect))
            continue;
        ellipse(at, len, rd, dir);
        for (int i = 0; i < satellites; ++i)
            ellipse(sat[i], satR[i], satR[i], dir);
        end(dab, elementColor(dab, e));
    }
}

void ProceduralDeposit::pattern(const DabParams &dab, const QRect &rect)
{
    const BrushSettings &st = m_ctx.settings;
    const double h = heading(dab);
    const QPointF u(std::cos(h), std::sin(h)), w(-std::sin(h), std::cos(h));
    auto at = [&](double along, double across) { return dab.center + u * along + w * across; };
    const double lw = std::max(0.15, st.procWidth * widthScale(st, dab) / 2.0); // line radius
    const double half = dab.diameter / 2.0;
    const double motif = st.procLength * dab.diameter;
    const double step = dab.stepFraction * dab.diameter;
    const double reachPx = std::max({motif, step, dab.diameter}) + lw + 1;
    if (!begin(QRectF(dab.center - QPointF(reachPx, reachPx), dab.center + QPointF(reachPx, reachPx)), rect))
        return;
    switch (st.patternShape) {
    case PatternShape::Dots: {
        const double r = std::max(0.5, motif / 4.0);
        ellipse(dab.center, r, r, h);
        break;
    }
    case PatternShape::Dashes: capsule(at(-motif / 2, 0), at(motif / 2, 0), lw, lw); break;
    case PatternShape::Chevrons:
        capsule(at(-motif / 2, -half), at(motif / 2, 0), lw, lw);
        capsule(at(motif / 2, 0), at(-motif / 2, half), lw, lw);
        break;
    case PatternShape::Zigzag: {
        // From this dab to the next: alternate sides so the segments join.
        const double side = (dab.index % 2 == 0) ? half : -half;
        capsule(at(0, side), at(step, -side), lw, lw);
        break;
    }
    case PatternShape::Scallops: {
        const int k = 12;
        QPointF prev = at(0, 0);
        for (int i = 1; i <= k; ++i) {
            const double a = Pi * i / k;
            const QPointF p = at(step / 2.0 * (1.0 - std::cos(a)), -std::sin(a) * std::min(half, step / 2.0));
            capsule(prev, p, lw, lw);
            prev = p;
        }
        break;
    }
    case PatternShape::Stitches:
        capsule(at(-motif / 2, -half * 0.6), at(motif / 2, half * 0.6), lw, lw);
        capsule(at(-motif / 2, half * 0.6), at(motif / 2, -half * 0.6), lw, lw);
        break;
    }
    end(dab, dab.color);
}

bool ProceduralDeposit::begin(const QRectF &bounds, const QRect &rect)
{
    m_box = bounds.toAlignedRect() & rect;
    if (m_box.isEmpty())
        return false;
    const size_t n = size_t(m_box.width()) * size_t(m_box.height());
    m_v.assign(n, 0.0f);
    m_t.assign(n, 0.0f);
    return true;
}

void ProceduralDeposit::capsule(QPointF a, QPointF b, double ra, double rb, double ta, double tb)
{
    const double rmax = std::max(ra, rb);
    const QRect area = QRectF(QPointF(std::min(a.x(), b.x()) - rmax - 1, std::min(a.y(), b.y()) - rmax - 1),
                              QPointF(std::max(a.x(), b.x()) + rmax + 1, std::max(a.y(), b.y()) + rmax + 1))
                           .toAlignedRect()
                     & m_box;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        float *row = m_v.data() + size_t(y - m_box.top()) * size_t(m_box.width()) - m_box.left();
        float *trow = m_t.data() + size_t(y - m_box.top()) * size_t(m_box.width()) - m_box.left();
        for (int x = area.left(); x <= area.right(); ++x) {
            double t;
            const double d = segmentDistance(x + 0.5, y + 0.5, a.x(), a.y(), b.x(), b.y(), t);
            const double r = ra + (rb - ra) * t;
            // Below a pixel's width the line keeps its area: coverage × width instead of thinner.
            const double v = r < 0.5 ? std::clamp(1.0 - d, 0.0, 1.0) * 2.0 * r : std::clamp(r + 0.5 - d, 0.0, 1.0);
            if (v > row[x]) {
                row[x] = float(v);
                trow[x] = float(ta + (tb - ta) * t);
            }
        }
    }
}

void ProceduralDeposit::ellipse(QPointF c, double along, double across, double angle)
{
    along = std::max(along, 1e-3);
    across = std::max(across, 1e-3);
    const double ca = std::cos(angle), sa = std::sin(angle);
    const double r = std::max(along, across);
    const QRect area = QRectF(c - QPointF(r + 1, r + 1), c + QPointF(r + 1, r + 1)).toAlignedRect() & m_box;
    const double small = std::min(along, across);
    const double thin = small < 0.5 ? 2.0 * small : 1.0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        float *row = m_v.data() + size_t(y - m_box.top()) * size_t(m_box.width()) - m_box.left();
        for (int x = area.left(); x <= area.right(); ++x) {
            const double dx = x + 0.5 - c.x(), dy = y + 0.5 - c.y();
            const double u = (dx * ca + dy * sa) / along, w = (-dx * sa + dy * ca) / across;
            const double q = std::sqrt(u * u + w * w);
            // Distance to the rim, estimated along the smaller axis.
            const double v = std::clamp((1.0 - q) * std::max(small, 0.5) + 0.5, 0.0, 1.0) * thin;
            if (v > row[x])
                row[x] = float(v);
        }
    }
}

void ProceduralDeposit::lens(QPointF base, double angle, double length, double width)
{
    const double ca = std::cos(angle), sa = std::sin(angle);
    const double half = width / 2.0;
    const bool vein = width >= 6.0;
    const QPointF tip = base + QPointF(ca, sa) * length;
    const QRect area = QRectF(base, tip).normalized().adjusted(-half - 1, -half - 1, half + 1, half + 1).toAlignedRect() & m_box;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        float *row = m_v.data() + size_t(y - m_box.top()) * size_t(m_box.width()) - m_box.left();
        for (int x = area.left(); x <= area.right(); ++x) {
            const double dx = x + 0.5 - base.x(), dy = y + 0.5 - base.y();
            const double u = dx * ca + dy * sa, w = std::abs(-dx * sa + dy * ca);
            if (u <= -0.5 || u >= length + 0.5)
                continue;
            // Pointed at both ends, widest a little before the middle.
            const double s = std::clamp(u / length, 0.0, 1.0);
            const double hw = half * std::pow(std::sin(Pi * std::pow(s, 0.85)), 0.75);
            double v = std::clamp(hw + 0.5 - w, 0.0, 1.0) * std::clamp(std::min(u, length - u) + 0.5, 0.0, 1.0);
            if (vein && s < 0.85)
                v *= 1.0 - 0.4 * std::clamp(0.8 - w, 0.0, 1.0); // a lighter midrib
            if (v > row[x])
                row[x] = float(v);
        }
    }
}

void ProceduralDeposit::end(const DabParams &dab, QRgb color, bool shade)
{
    const BrushSettings &st = m_ctx.settings;
    const quint32 ceiling = quint32(std::lround(dab.ceiling * 255.0)) * 257u;
    const qint64 index = dab.index + 1;
    const bool texture = m_ctx.textureEachDab;
    const bool moving = texture && st.textureMoving;
    const bool subtract = st.textureMode == TextureMode::Subtract;
    const double texCos = std::cos(dab.angle), texSin = std::sin(dab.angle);
    const QRgb root = qRgb(qRed(color) * 55 / 100, qGreen(color) * 55 / 100, qBlue(color) * 55 / 100);
    qint64 cachedKey = -1;
    CoverageTile *tile = nullptr;
    ColorTile *colors = nullptr;
    for (int y = m_box.top(); y <= m_box.bottom(); ++y) {
        const size_t line = size_t(y - m_box.top()) * size_t(m_box.width());
        for (int x = m_box.left(); x <= m_box.right(); ++x) {
            double v = m_v[line + size_t(x - m_box.left())];
            if (v <= 0.0)
                continue;
            if (texture) {
                const double dx = x + 0.5 - dab.center.x(), dy = y + 0.5 - dab.center.y();
                const double t = moving ? m_ctx.grain(dx * texCos + dy * texSin, -dx * texSin + dy * texCos)
                                        : m_ctx.grain(x + 0.5, y + 0.5);
                const double valley = dab.textureStrength * (1.0 - t);
                v = subtract ? std::max(0.0, v - valley) : v * (1.0 - valley);
                if (v <= 0.0)
                    continue;
            }
            const qint64 key = tileKey(x / Tile, y / Tile);
            if (key != cachedKey) {
                cachedKey = key;
                tile = &coverageTile(x / Tile, y / Tile);
                colors = m_ctx.colorPerDab ? &colorTile(x / Tile, y / Tile) : nullptr;
            }
            const size_t idx = size_t((y % Tile) * Tile + x % Tile);
            quint16 &s = (*tile)[idx];
            const quint32 add = increase(s, v, ceiling, dab.flow, x, y, index);
            if (add == 0)
                continue;
            QRgb c = color;
            if (shade && colors) {
                // Blades are darker at the root.
                const double t = std::clamp(double(m_t[line + size_t(x - m_box.left())]), 0.0, 1.0);
                const quint32 tip = quint32(std::lround(t * 1000.0));
                c = mixColor(root, 1000 - tip, color, tip);
            }
            deposit(s, add, colors ? &(*colors)[idx] : nullptr, c);
        }
    }
}

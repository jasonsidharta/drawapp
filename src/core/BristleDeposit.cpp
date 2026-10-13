#include "BristleDeposit.h"

#include "BrushTips.h"
#include "PigmentMix.h"
#include "TileSnapshot.h"

#include <cmath>

using namespace BrushRaster;

namespace {

inline double smoothstep01(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Distance from p to the segment a-b.
inline double segmentDistance(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 1e-12 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double cx = ax + t * dx - px, cy = ay + t * dy - py;
    return std::sqrt(cx * cx + cy * cy);
}

constexpr double ContactRange = 0.15; // pressure over which a bristle goes from not touching to touching

} // namespace

BristleDeposit::BristleDeposit(StrokeContext &context)
    : CoverageDeposit(context)
{
    // Every bristle has its own colour, and the texture is the paper the dry bristles skip on
    // (applied per bristle, not again at composite time).
    m_ctx.colorPerDab = true;
    if (m_ctx.texture)
        m_ctx.textureEachDab = true;
    const BrushSettings &st = m_ctx.settings;
    const int n = std::clamp(st.bristleCount, 1, 512);
    m_bristles.resize(size_t(n));
    for (int i = 0; i < n; ++i) {
        Bristle &b = m_bristles[size_t(i)];
        const quint64 salt = StrokeSalt::Milestone11 + 0x100;
        // Uniform over the disc (√ for the radius), jittered sunflower order so they never clump.
        const double r = std::sqrt((i + m_ctx.random(i, salt)) / n);
        const double a = i * 2.39996322972865332 + m_ctx.random(i, salt + 1) * 0.6;
        b.u = r * std::cos(a);
        b.w = r * std::sin(a);
        b.threshold = st.bristleLengthJitter * m_ctx.random(i, salt + 2);
        b.reservoir = st.bristleLoad * (0.75 + 0.25 * m_ctx.random(i, salt + 3));
        b.depletion = st.bristleDepletion * (0.5 + m_ctx.random(i, salt + 4));
    }
}

qsizetype BristleDeposit::bufferBytes() const
{
    return CoverageDeposit::bufferBytes() + qsizetype(m_bristles.size() * sizeof(Bristle));
}

QRgb BristleDeposit::pixelBefore(const StrokeSurface &image, const TileSnapshot &before, int x, int y) const
{
    if (const QRgb *row = before.storedRow(x, y))
        return row[0];
    return image.pixel(x, y); // a tile the stroke has not changed yet: as it was
}

void BristleDeposit::pickUp(Bristle &b, const StrokeSurface &image, const TileSnapshot &before, double travelled)
{
    const BrushSettings &st = m_ctx.settings;
    if (st.bristlePickup <= 0 || travelled <= 0)
        return;
    const int x = int(std::floor(b.previous.x())), y = int(std::floor(b.previous.y()));
    if (!image.rect().contains(x, y))
        return;
    const QRgb p = pixelBefore(image, before, x, y);
    const int alpha = qAlpha(p);
    if (alpha == 0)
        return; // nothing to pick up from transparency
    const float under[3] = {float(qRed(p) * 255.0 / alpha), float(qGreen(p) * 255.0 / alpha), float(qBlue(p) * 255.0 / alpha)};
    // Per 10 px travelled the bristle takes `pickup` of the way to the colour under it (× its alpha).
    const double k = (1.0 - std::pow(1.0 - std::clamp(st.bristlePickup, 0.0, 1.0), travelled / 10.0)) * alpha / 255.0;
    if (st.pigmentMixing) {
        float out[3];
        PigmentMix::mixStraight(b.color, under, k, out);
        std::copy(out, out + 3, b.color);
    } else {
        for (int c = 0; c < 3; ++c)
            b.color[c] += float((under[c] - b.color[c]) * k);
    }
}

void BristleDeposit::dab(StrokeSurface &image, const DabParams &dab, const QRect &rect, TileSnapshot &before, QRect &dirty)
{
    Q_UNUSED(rect)
    const BrushSettings &st = m_ctx.settings;
    if (dab.ceiling <= 0 || dab.flow <= 0)
        return;
    if (!m_initialised) {
        for (Bristle &b : m_bristles) {
            b.color[0] = float(qRed(dab.color));
            b.color[1] = float(qGreen(dab.color));
            b.color[2] = float(qBlue(dab.color));
        }
        m_initialised = true;
    }
    const double radius = dab.diameter / 2.0;
    const double spread = 1.0 - std::clamp(st.bristleSpread, 0.0, 1.0) * (1.0 - std::clamp(dab.pressure, 0.0, 1.0));
    const double ca = std::cos(dab.angle), sa = std::sin(dab.angle);
    const double roundness = std::max(0.05, dab.roundness);
    const double width = std::max(0.8, dab.diameter * st.bristleThickness);
    const double half = width / 2.0;
    const double gap = 2.0 * radius / std::sqrt(double(m_bristles.size()));
    // A bristle's path covers each pixel about once (not once per dab, as a tip does), so it lays
    // the full per-pass flow, and overlapping segment ends and crossing bristles take the larger
    // value rather than adding up (no beads at the joints).
    const quint32 ceiling = quint32(std::lround(dab.ceiling * dab.passFlow * 255.0)) * 257u;

    // Where each bristle is now, and the area all segments cover (captured before it changes).
    std::vector<QPointF> now(m_bristles.size());
    QRectF area;
    for (size_t i = 0; i < m_bristles.size(); ++i) {
        const Bristle &b = m_bristles[i];
        const double wobble = (m_ctx.random(dab.index * 1024 + qint64(i), StrokeSalt::Milestone11 + 0x110) - 0.5) * 2.0
                            * st.bristleWobble * gap;
        const double lu = b.u * radius * spread, lw = (b.w * radius * spread + wobble) * roundness;
        now[i] = dab.center + QPointF(lu * ca - lw * sa, lu * sa + lw * ca);
        const QPointF from = b.placed ? b.previous : now[i];
        area |= QRectF(std::min(from.x(), now[i].x()) - half - 1.5, std::min(from.y(), now[i].y()) - half - 1.5,
                       std::abs(from.x() - now[i].x()) + width + 3, std::abs(from.y() - now[i].y()) + width + 3);
    }
    const QRect bounds = area.toAlignedRect() & image.rect();
    if (bounds.isEmpty())
        return;
    image.capture(before, bounds);

    const bool paper = m_ctx.texture != nullptr;
    const double tooth = dab.textureStrength; // the setting × its dynamics
    for (size_t i = 0; i < m_bristles.size(); ++i) {
        Bristle &b = m_bristles[i];
        const QPointF from = b.placed ? b.previous : now[i];
        const QPointF to = now[i];
        const double travelled = b.placed ? std::hypot(to.x() - from.x(), to.y() - from.y()) : 0.0;
        b.previous = to;
        b.placed = true;
        const double contact = smoothstep01(0.5 + (dab.pressure - b.threshold) / ContactRange);
        if (contact <= 0.0 || b.reservoir <= 0.001)
            continue;
        const double load = b.reservoir;
        const QRgb color = qRgb(std::clamp(int(std::lround(b.color[0])), 0, 255), std::clamp(int(std::lround(b.color[1])), 0, 255),
                                std::clamp(int(std::lround(b.color[2])), 0, 255));
        const QRect seg = QRectF(std::min(from.x(), to.x()) - half - 1, std::min(from.y(), to.y()) - half - 1,
                                 std::abs(from.x() - to.x()) + width + 2, std::abs(from.y() - to.y()) + width + 2)
                              .toAlignedRect()
                        & image.rect();
        for (int y = seg.top(); y <= seg.bottom(); ++y) {
            for (int x = seg.left(); x <= seg.right(); ++x) {
                const double d = segmentDistance(x + 0.5, y + 0.5, from.x(), from.y(), to.x(), to.y());
                double v = std::clamp(half + 0.5 - d, 0.0, 1.0);
                if (width < 1.0)
                    v *= width;
                if (v <= 0.0)
                    continue;
                // A full bristle paints; a drying one first skips the valleys of the paper, then
                // fades out (without paper it just fades).
                double paint;
                if (paper) {
                    const double h = 1.0 - tooth * (1.0 - m_ctx.grain(x + 0.5, y + 0.5));
                    paint = smoothstep01(0.5 + (load * 1.3 - (1.0 - h)) / 0.25);
                } else {
                    paint = smoothstep01(load * 3.0);
                }
                v *= contact * paint;
                if (v <= 0.0)
                    continue;
                CoverageTile &tile = coverageTile(x / Tile, y / Tile);
                ColorTile &colors = colorTile(x / Tile, y / Tile);
                const size_t idx = size_t((y % Tile) * Tile + x % Tile);
                quint16 &s = tile[idx];
                const quint32 target = quint32(std::lround(v * ceiling));
                if (target <= s)
                    continue;
                deposit(s, target - s, &colors[idx], color);
            }
        }
        // Paint used, and colour picked up, along the distance painted.
        if (travelled > 0) {
            b.reservoir *= std::pow(1.0 - std::clamp(b.depletion, 0.0, 0.999), travelled * contact / 100.0);
            pickUp(b, image, before, travelled * contact);
        }
    }
    touch(bounds);
    dirty |= bounds;
}

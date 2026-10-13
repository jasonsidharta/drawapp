#include "DryDeposit.h"

#include "BrushTips.h"

#include <cmath>

using namespace BrushRaster;

namespace {

inline double smoothstep01(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

DryDeposit::DryDeposit(StrokeContext &context)
    : CoverageDeposit(context)
{
    if (m_ctx.settings.dryStreaks > 0)
        m_streaks = &BrushTips::streaks();
}

void DryDeposit::rasterize(const DabParams &dab, const QRect &rect)
{
    const BrushSettings &st = m_ctx.settings;
    const DabShape shape(dab, st, m_ctx.tip);
    const quint32 ceiling = quint32(std::lround(dab.ceiling * 255.0)) * 257u;
    const qint64 index = dab.index + 1;
    // Penetration of this dab: setting × pressure response × tilt (side of the lead).
    double depth = st.dryDepth * st.pressureDepth.apply(dab.pressure);
    depth *= 1.0 + (st.dryTiltDepth - 1.0) * dab.tilt;
    const double tooth = st.textureStrength;
    const double softness = std::max(0.01, st.drySoftness);
    const double density = st.tipDensity;
    // Streak frame: along the stroke heading (or the dab angle before the stroke has moved),
    // anchored in document coordinates so the streaks continue from dab to dab.
    const double heading = dab.hasDirection ? dab.direction : dab.angle;
    const double hc = std::cos(heading), hs = std::sin(heading);
    const double streakU = 1.0 / (st.dryStreakScale * st.dryStreakLength);
    const double streakW = 1.0 / st.dryStreakScale;
    const double streaks = m_streaks ? st.dryStreaks : 0.0;
    const double breakup = st.dryEdgeBreakup;
    const double grit = st.dryGrit;
    const bool paper = m_ctx.texture != nullptr;
    for (int row = rect.top() / Tile; row <= rect.bottom() / Tile; ++row) {
        for (int column = rect.left() / Tile; column <= rect.right() / Tile; ++column) {
            const QRect part = rect & QRect(column * Tile, row * Tile, Tile, Tile);
            CoverageTile &tile = coverageTile(column, row);
            ColorTile *colors = m_ctx.colorPerDab ? &colorTile(column, row) : nullptr;
            const int ox = column * Tile;
            for (int y = part.top(); y <= part.bottom(); ++y) {
                const double dy = y + 0.5 - dab.center.y();
                const int lineStart = (y - row * Tile) * Tile;
                quint16 *line = tile.data() + lineStart;
                for (int x = part.left(); x <= part.right(); ++x) {
                    const double v = shape.value(x + 0.5 - dab.center.x(), dy);
                    if (v <= 0.0)
                        continue;
                    if (density < 1.0 && BrushTips::hash01((qint64(y) << 24) ^ x, index, StrokeSalt::Density) >= density)
                        continue;
                    // Paper height (1 = peak), flattened by a weak tooth.
                    double h = paper ? 1.0 - tooth * (1.0 - m_ctx.grain(x + 0.5, y + 0.5)) : 1.0;
                    if (streaks > 0) {
                        const double px = x + 0.5, py = y + 0.5;
                        const double u = (px * hc + py * hs) * streakU, w = (-px * hs + py * hc) * streakW;
                        h = h * (1.0 - streaks) + sampleWrapped(*m_streaks, u, w) * streaks;
                    }
                    double d = depth;
                    if (breakup > 0 && v < 1.0) {
                        // Towards the rim (falling tip value) the penetration drops by a second,
                        // offset sample of the paper, so the edge crumbles.
                        const double noise = paper ? m_ctx.grain(x * 1.7 + 31.5, y * 1.7 + 17.5) : 0.5;
                        d -= breakup * (1.0 - v) * (0.5 + noise);
                    }
                    double f = smoothstep01(0.5 + (d - (1.0 - h)) / softness);
                    if (grit > 0 && BrushTips::hash01((qint64(y) << 24) ^ x, qint64(m_ctx.seed), StrokeSalt::Milestone11 + 1) < grit)
                        f *= 0.2;
                    const double value = v * f;
                    if (value <= 0.0)
                        continue;
                    quint16 &s = line[x - ox];
                    const quint32 add = increase(s, value, ceiling, dab.flow, x, y, index);
                    if (add == 0)
                        continue;
                    deposit(s, add, colors ? &(*colors)[size_t(lineStart + x - ox)] : nullptr, dab.color);
                }
            }
        }
    }
}

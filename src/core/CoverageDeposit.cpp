#include "CoverageDeposit.h"

#include "TileSnapshot.h"

#include <cmath>

using namespace BrushRaster;

void CoverageDeposit::dab(StrokeSurface &image, const DabParams &dab, const QRect &rect, TileSnapshot &before, QRect &dirty)
{
    if (dab.ceiling <= 0 || dab.flow <= 0)
        return;
    // Per dab, not the union: a long diagonal segment must not capture its whole bounding box.
    image.capture(before, rect);
    rasterize(dab, rect);
    touch(rect);
    dirty |= rect;
}

void CoverageDeposit::rasterize(const DabParams &dab, const QRect &rect)
{
    if (m_ctx.settings.stamp == BrushStamp::Spray)
        rasterizeSpray(dab, rect);
    else
        rasterizeTip(dab, rect);
}

void CoverageDeposit::touch(const QRect &rect)
{
    for (int row = rect.top() / Tile; row <= rect.bottom() / Tile; ++row)
        for (int column = rect.left() / Tile; column <= rect.right() / Tile; ++column)
            m_touched[tileKey(column, row)] |= rect & QRect(column * Tile, row * Tile, Tile, Tile);
}

qsizetype CoverageDeposit::bufferBytes() const
{
    return qsizetype(m_coverage.size()) * Tile * Tile * qsizetype(sizeof(quint16))
         + qsizetype(m_colors.size()) * Tile * Tile * qsizetype(sizeof(quint32));
}

CoverageDeposit::CoverageTile &CoverageDeposit::coverageTile(int column, int row)
{
    const auto [it, inserted] = m_coverage.try_emplace(tileKey(column, row));
    if (inserted)
        it->second.assign(size_t(Tile * Tile), 0);
    return it->second;
}

CoverageDeposit::ColorTile &CoverageDeposit::colorTile(int column, int row)
{
    const auto [it, inserted] = m_colors.try_emplace(tileKey(column, row));
    if (inserted)
        it->second.assign(size_t(Tile * Tile), 0);
    return it->second;
}

void CoverageDeposit::rasterizeTip(const DabParams &dab, const QRect &rect)
{
    const BrushSettings &st = m_ctx.settings;
    const DabShape shape(dab, st, m_ctx.tip);
    const quint32 ceiling = quint32(std::lround(dab.ceiling * 255.0)) * 257u;
    const qint64 index = dab.index + 1; // the milestone 6 numbering of the rounding pattern
    const bool hatch = st.stamp == BrushStamp::Hatching;
    const double hatchNx = -std::sin(st.hatchAngle * Pi / 180.0), hatchNy = std::cos(st.hatchAngle * Pi / 180.0);
    const double hatchHalf = st.hatchThickness / 2.0, hatchPeriod = st.hatchSpacing;
    const double density = st.tipDensity;
    const bool texture = m_ctx.textureEachDab;
    const bool moving = texture && st.textureMoving;
    const double texCos = std::cos(dab.angle), texSin = std::sin(dab.angle);
    const bool subtract = st.textureMode == TextureMode::Subtract;
    const bool buildUp = m_ctx.buildUp;
    if (density >= 1.0 && !hatch && !texture && !m_ctx.colorPerDab) {
        // The common case keeps the milestone 6 loop: nothing per pixel beyond the tip and the
        // accumulation rule (the general loop below measured up to ~15 % slower on large dabs).
        for (int row = rect.top() / Tile; row <= rect.bottom() / Tile; ++row) {
            for (int column = rect.left() / Tile; column <= rect.right() / Tile; ++column) {
                const QRect part = rect & QRect(column * Tile, row * Tile, Tile, Tile);
                CoverageTile &tile = coverageTile(column, row);
                const int ox = column * Tile;
                for (int y = part.top(); y <= part.bottom(); ++y) {
                    const double dy = y + 0.5 - dab.center.y();
                    quint16 *line = tile.data() + (y - row * Tile) * Tile;
                    for (int x = part.left(); x <= part.right(); ++x) {
                        const double v = shape.value(x + 0.5 - dab.center.x(), dy);
                        if (v <= 0.0)
                            continue;
                        quint16 &s = line[x - ox];
                        if (!buildUp) {
                            const quint32 target = quint32(std::lround(v * ceiling));
                            if (target > s)
                                s = quint16(target);
                        } else if (ceiling > s) {
                            const double inc = v * dab.flow * double(ceiling - s);
                            const quint32 step = quint32(inc + threshold(x + int(index * 29), y + int(index * 17)));
                            s = quint16(std::min<quint32>(ceiling, s + step));
                        }
                    }
                }
            }
        }
        return;
    }
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
                    const double dx = x + 0.5 - dab.center.x();
                    double v = shape.value(dx, dy);
                    if (v <= 0.0)
                        continue;
                    if (density < 1.0 && BrushTips::hash01((qint64(y) << 24) ^ x, index, StrokeSalt::Density) >= density)
                        continue;
                    if (hatch) {
                        const double p = (x + 0.5) * hatchNx + (y + 0.5) * hatchNy;
                        const double m = std::abs(p - hatchPeriod * std::floor(p / hatchPeriod + 0.5));
                        double stripe = std::clamp(hatchHalf + 0.5 - m, 0.0, 1.0);
                        if (hatchHalf < 0.5)
                            stripe = std::min(stripe, 2.0 * hatchHalf);
                        v *= stripe;
                        if (v <= 0.0)
                            continue;
                    }
                    if (texture) {
                        const double t = moving ? m_ctx.grain(dx * texCos + dy * texSin, -dx * texSin + dy * texCos)
                                                : m_ctx.grain(x + 0.5, y + 0.5);
                        const double valley = dab.textureStrength * (1.0 - t);
                        v = subtract ? std::max(0.0, v - valley) : v * (1.0 - valley);
                        if (v <= 0.0)
                            continue;
                    }
                    quint16 &s = line[x - ox];
                    const quint32 add = increase(s, v, ceiling, dab.flow, x, y, index);
                    if (add == 0)
                        continue;
                    deposit(s, add, colors ? &(*colors)[size_t(lineStart + x - ox)] : nullptr, dab.color);
                }
            }
        }
    }
}

void CoverageDeposit::rasterizeSpray(const DabParams &dab, const QRect &rect)
{
    const BrushSettings &st = m_ctx.settings;
    const quint32 ceiling = quint32(std::lround(dab.ceiling * 255.0)) * 257u;
    const int particles = std::max(1, int(std::lround(st.sprayDensity)));
    const double radius = dab.diameter / 2.0;
    const bool subtract = st.textureMode == TextureMode::Subtract;
    qint64 cachedKey = -1;
    CoverageTile *tile = nullptr;
    ColorTile *colors = nullptr;
    for (int i = 0; i < particles; ++i) {
        const qint64 key = dab.index * 4096 + i;
        const double a = m_ctx.random(key, StrokeSalt::SprayAngle) * 2.0 * Pi;
        const double r = radius * std::sqrt(m_ctx.random(key, StrokeSalt::SprayRadius));
        const double pr = st.sprayParticleSize * (1.0 - st.sprayParticleJitter * m_ctx.random(key, StrokeSalt::SpraySize)) / 2.0;
        const double cx = dab.center.x() + r * std::cos(a), cy = dab.center.y() + r * std::sin(a);
        const QRect area = QRectF(cx - pr - 1, cy - pr - 1, 2 * pr + 2, 2 * pr + 2).toAlignedRect() & rect;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                double v = std::clamp(pr + 0.5 - std::hypot(x + 0.5 - cx, y + 0.5 - cy), 0.0, 1.0);
                if (pr < 0.5)
                    v *= 2.0 * pr;
                if (v <= 0.0)
                    continue;
                if (m_ctx.textureEachDab) {
                    const double valley = dab.textureStrength * (1.0 - m_ctx.grain(x + 0.5, y + 0.5));
                    v = subtract ? std::max(0.0, v - valley) : v * (1.0 - valley);
                    if (v <= 0.0)
                        continue;
                }
                const qint64 tk = tileKey(x / Tile, y / Tile);
                if (tk != cachedKey) {
                    cachedKey = tk;
                    tile = &coverageTile(x / Tile, y / Tile);
                    if (m_ctx.colorPerDab)
                        colors = &colorTile(x / Tile, y / Tile);
                }
                const size_t idx = size_t((y % Tile) * Tile + x % Tile);
                quint16 &s = (*tile)[idx];
                const quint32 add = increase(s, v, ceiling, dab.flow, x, y, key);
                if (add == 0)
                    continue;
                deposit(s, add, colors ? &(*colors)[idx] : nullptr, dab.color);
            }
        }
    }
}

void CoverageDeposit::flush(StrokeSurface &image, const TileSnapshot &before)
{
    // Each pixel is recomputed once per call, however many dabs covered it.
    for (const auto &[key, area] : m_touched)
        composite(image, area, before);
    m_touched.clear();
}

void CoverageDeposit::composite(StrokeSurface &image, const QRect &rect, const TileSnapshot &before)
{
    const int column = rect.left() / Tile, row = rect.top() / Tile;
    Q_ASSERT(before.storedRow(rect.left(), rect.top())); // dab() captured every tile it touches
    if (!before.storedRow(rect.left(), rect.top()) || m_ctx.allocationFailed)
        return;
    const int x0 = rect.left();
    const auto found = m_coverage.find(tileKey(column, row));
    if (found == m_coverage.end())
        return; // a spray dab may touch a tile without any particle landing in it
    const CoverageTile &tile = found->second;
    const ColorTile *colors = nullptr;
    if (m_ctx.colorPerDab) {
        const auto it = m_colors.find(tileKey(column, row));
        colors = it == m_colors.end() ? nullptr : &it->second;
    }
    const int ox = column * Tile, oy = row * Tile;
    const BrushSettings &st = m_ctx.settings;
    const bool erase = st.blend == BrushBlend::Erase;
    const PaintMode mode = st.paintMode;
    const bool strokeTexture = m_ctx.texture && !m_ctx.textureEachDab;
    const bool exact = !strokeTexture && m_ctx.clip.isNull();
    const double strength = st.textureStrength;
    const double opacity = st.opacity;
    const bool subtract = st.textureMode == TextureMode::Subtract;
    // The usual grain (unscaled, unrotated, unadjusted) is read straight from its row.
    const bool directGrain = strokeTexture && m_ctx.textureNearest && !st.textureInvert && st.textureContrast == 1.0
                          && st.textureBrightness == 0.0;
    const QImage *grainImage = strokeTexture ? &m_ctx.texture->image : nullptr;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        QRgb *out = image.writableRow(x0, y); // indexed from x0
        if (!out) {
            m_ctx.allocationFailed = true;
            return;
        }
        const QRgb *src = before.storedRow(x0, y);
        const quint16 *cov = tile.data() + (y - oy) * Tile;
        const uchar *grainRow = nullptr;
        int grainWidth = 0;
        if (directGrain) {
            const int gh = grainImage->height(), gy = y + m_ctx.textureOffset.y();
            grainRow = grainImage->constScanLine(((gy % gh) + gh) % gh);
            grainWidth = grainImage->width();
        }
        const quint32 *col = colors ? colors->data() + (y - oy) * Tile : nullptr;
        const uchar *clip = m_ctx.clip.isNull() ? nullptr : m_ctx.clip.constScanLine(y);
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const QRgb p = src[x - x0];
            const quint32 coverage = cov[x - ox];
            if (coverage == 0) {
                out[x - x0] = p;
                continue;
            }
            double k; // 8-bit units
            if (exact) {
                k = coverage / 257.0; // a whole level for a coverage that is one
            } else {
                double s = coverage / double(FullCoverage);
                if (strokeTexture) {
                    double t;
                    if (grainRow) {
                        const int gx = x + m_ctx.textureOffset.x();
                        t = grainRow[((gx % grainWidth) + grainWidth) % grainWidth] / 255.0;
                    } else {
                        t = m_ctx.grain(x + 0.5, y + 0.5);
                    }
                    const double valley = strength * (1.0 - t);
                    // Subtract works on the coverage relative to the brush opacity, so a light
                    // mark breaks up into grain while a heavy one fills the tooth.
                    s = subtract ? opacity * std::max(0.0, s / opacity - valley) : s * (1.0 - valley);
                }
                if (clip)
                    s *= clip[x] / 255.0;
                k = s * 255.0;
            }
            const quint32 a = quint32(std::clamp(int(k + threshold(x, y)), 0, 255));
            if (a == 0) {
                out[x - x0] = p;
                continue;
            }
            const quint32 keep = 255 - a;
            if (erase) {
                out[x - x0] = qRgba(int(mul255(qRed(p), keep)), int(mul255(qGreen(p), keep)), int(mul255(qBlue(p), keep)),
                                   int(mul255(qAlpha(p), keep)));
                continue;
            }
            const QRgb c = col ? col[x - ox] : m_ctx.color;
            const quint32 sr = mul255(qRed(c), a), sg = mul255(qGreen(c), a), sb = mul255(qBlue(c), a); // premultiplied source
            const quint32 pr = qRed(p), pg = qGreen(p), pb = qBlue(p), pa = qAlpha(p);
            quint32 r, g, b, alpha;
            switch (mode) {
            case PaintMode::Multiply:
                // k·cs·(1 − αb) + cb'·(1 − k) + k·cs·cb'
                r = mul255(sr, 255 - pa) + mul255(pr, keep) + mul255(sr, pr);
                g = mul255(sg, 255 - pa) + mul255(pg, keep) + mul255(sg, pg);
                b = mul255(sb, 255 - pa) + mul255(pb, keep) + mul255(sb, pb);
                alpha = a + mul255(pa, keep);
                break;
            case PaintMode::Screen:
                // k·cs + cb' − k·cs·cb'
                r = sr + pr - mul255(sr, pr);
                g = sg + pg - mul255(sg, pg);
                b = sb + pb - mul255(sb, pb);
                alpha = a + mul255(pa, keep);
                break;
            case PaintMode::Behind:
                // The stroke goes under existing paint: cb' + k·cs·(1 − αb).
                r = pr + mul255(sr, 255 - pa);
                g = pg + mul255(sg, 255 - pa);
                b = pb + mul255(sb, 255 - pa);
                alpha = pa + mul255(a, 255 - pa);
                break;
            case PaintMode::PreserveAlpha:
                // Colour only where there is paint; alpha unchanged.
                r = mul255(pr, keep) + mul255(sr, pa);
                g = mul255(pg, keep) + mul255(sg, pa);
                b = mul255(pb, keep) + mul255(sb, pa);
                alpha = pa;
                break;
            case PaintMode::Normal:
            default:
                // Source-over with a premultiplied source (colour × a, alpha a).
                r = sr + mul255(pr, keep);
                g = sg + mul255(pg, keep);
                b = sb + mul255(pb, keep);
                alpha = a + mul255(pa, keep);
                break;
            }
            alpha = std::min<quint32>(alpha, 255);
            out[x - x0] = qRgba(int(std::min(r, alpha)), int(std::min(g, alpha)), int(std::min(b, alpha)), int(alpha));
        }
    }
}

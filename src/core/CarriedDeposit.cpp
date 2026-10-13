#include "CarriedDeposit.h"

#include "TileSnapshot.h"

#include <cmath>
#include <cstring>

using namespace BrushRaster;

qsizetype CarriedDeposit::bufferBytes() const
{
    return qsizetype(m_patch.size() * sizeof(float)) + qsizetype(m_work.size()) * Tile * Tile * 4
         + (m_region.isNull() ? 0 : m_region.sizeInBytes());
}

void CarriedDeposit::dab(StrokeSurface &image, const DabParams &dab, const QRect &rect, TileSnapshot &before, QRect &dirty)
{
    // Smudge: the first dab only loads the patch; after that each dab changes pixels. Wet
    // paint deposits from the first dab on.
    if (m_patchLoaded || m_ctx.settings.blend == BrushBlend::WetPaint)
        image.capture(before, rect);
    carry(image, dab, rect, before);
    dirty |= rect;
}

QRgb CarriedDeposit::workPixel(const StrokeSurface &image, int x, int y) const
{
    const auto it = m_work.find(tileKey(x / Tile, y / Tile));
    if (it == m_work.end())
        return image.pixel(x, y);
    return reinterpret_cast<const QRgb *>(it->second.constScanLine(y % Tile))[x % Tile];
}

void CarriedDeposit::loadPatch(const StrokeSurface &image, const DabParams &dab)
{
    const int n = m_patchSize;
    const BrushSettings &st = m_ctx.settings;
    if (st.blend == BrushBlend::WetPaint) {
        // Charged with brush paint (thinned by the dilution), as much as the load says.
        const double alpha = (1.0 - st.wetDilution) * st.wetLoad;
        const float c[4] = {float(qRed(dab.color) * alpha), float(qGreen(dab.color) * alpha),
                            float(qBlue(dab.color) * alpha), float(255.0 * alpha)};
        for (int i = 0; i < n * n; ++i)
            std::memcpy(&m_patch[size_t(i) * 4], c, sizeof(c));
        return;
    }
    // Smudge: pick up what is under the first dab; beyond the layer edge, the nearest edge pixel.
    const int ox = int(std::floor(dab.center.x())) - n / 2, oy = int(std::floor(dab.center.y())) - n / 2;
    const int w = image.rect().width(), h = image.rect().height();
    double sum[4] = {0, 0, 0, 0};
    for (int j = 0; j < n; ++j) {
        const int py = std::clamp(oy + j, 0, h - 1);
        for (int i = 0; i < n; ++i) {
            const QRgb p = workPixel(image, std::clamp(ox + i, 0, w - 1), py);
            float *c = &m_patch[(size_t(j) * n + i) * 4];
            c[0] = float(qRed(p));
            c[1] = float(qGreen(p));
            c[2] = float(qBlue(p));
            c[3] = float(qAlpha(p));
            for (int k = 0; k < 4; ++k)
                sum[k] += c[k];
        }
    }
    if (st.smudgeMode == SmudgeMode::Dulling) {
        const float mean[4] = {float(sum[0] / (n * n)), float(sum[1] / (n * n)), float(sum[2] / (n * n)),
                               float(sum[3] / (n * n))};
        for (int i = 0; i < n * n; ++i)
            std::memcpy(&m_patch[size_t(i) * 4], mean, sizeof(mean));
    }
}

void CarriedDeposit::carry(StrokeSurface &image, const DabParams &dab, const QRect &rect, const TileSnapshot &before)
{
    const BrushSettings &st = m_ctx.settings;
    if (m_patch.empty()) {
        // Large enough for the biggest dab this stroke can make (every other size factor is ≤ 1).
        const double largest = st.size * std::max(1.0, st.tiltSize);
        m_patchSize = int(std::ceil(largest)) + 4;
        m_patch.assign(size_t(m_patchSize) * m_patchSize * 4, 0.0f);
    }
    if (!m_patchLoaded) {
        loadPatch(image, dab);
        m_patchLoaded = true;
        if (st.blend == BrushBlend::Smudge)
            return;
    }

    // The dab works on a copy of its area; without a selection the copy is written back as is.
    // With a selection: the copy is unclipped (so the carried colour is not weakened dab after
    // dab at soft edges), and  before + (work − before) × selection  is written once. The brush
    // may pick up colour from outside the selection; it writes only inside.
    // The copy's buffer is kept from dab to dab and only grows (milestone 11: no allocation per dab).
    if (m_region.width() < rect.width() || m_region.height() < rect.height()) {
        m_region = QImage(std::max(m_region.width(), rect.width()), std::max(m_region.height(), rect.height()),
                          QImage::Format_ARGB32_Premultiplied);
        if (m_region.isNull()) {
            m_ctx.allocationFailed = true;
            return;
        }
    }
    QImage &region = m_region;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(region.scanLine(y - rect.top()));
        for (int x = rect.left(); x <= rect.right(); ++x)
            line[x - rect.left()] = workPixel(image, x, y);
    }
    carryInto(region, rect.topLeft(), dab, rect);
    if (m_ctx.clip.isNull()) {
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(region.constScanLine(y - rect.top()));
            for (int x = rect.left(); x <= rect.right();) {
                const int end = std::min(rect.right(), (x / Tile + 1) * Tile - 1);
                QRgb *out = image.writableRow(x, y);
                if (!out) {
                    m_ctx.allocationFailed = true;
                    return;
                }
                std::memcpy(out, line + (x - rect.left()), size_t(end - x + 1) * 4);
                x = end + 1;
            }
        }
        return;
    }
    for (int row = rect.top() / Tile; row <= rect.bottom() / Tile; ++row) {
        for (int column = rect.left() / Tile; column <= rect.right() / Tile; ++column) {
            const QRect tileRect = QRect(column * Tile, row * Tile, Tile, Tile) & image.rect();
            auto [it, inserted] = m_work.try_emplace(tileKey(column, row));
            if (inserted) {
                it->second = QImage(Tile, Tile, QImage::Format_ARGB32_Premultiplied);
                it->second.fill(Qt::transparent);
                for (int y = tileRect.top(); y <= tileRect.bottom(); ++y) {
                    const QRgb *src = image.constRow(tileRect.left(), y);
                    if (src)
                        std::memcpy(it->second.scanLine(y - row * Tile), src, size_t(tileRect.width()) * 4);
                }
            }
            const QRect part = rect & tileRect;
            for (int y = part.top(); y <= part.bottom(); ++y)
                std::memcpy(it->second.scanLine(y - row * Tile) + (part.left() - column * Tile) * 4,
                            region.constScanLine(y - rect.top()) + (part.left() - rect.left()) * 4,
                            size_t(part.width()) * 4);
            if (!before.storedRow(part.left(), part.top()))
                continue;
            for (int y = part.top(); y <= part.bottom(); ++y) {
                QRgb *out = image.writableRow(part.left(), y); // indexed from part.left()
                if (!out) {
                    m_ctx.allocationFailed = true;
                    return;
                }
                const QRgb *b = before.storedRow(part.left(), y);
                const QRgb *wk = reinterpret_cast<const QRgb *>(it->second.constScanLine(y - row * Tile)) + (part.left() - column * Tile);
                const uchar *clip = m_ctx.clip.constScanLine(y);
                for (int x = part.left(); x <= part.right(); ++x) {
                    const QRgb bp = b[x - part.left()], wp = wk[x - part.left()];
                    const int c = clip[x];
                    auto mix = [&](int from, int to) { return from + (to - from) * c / 255; };
                    const int a = std::clamp(mix(qAlpha(bp), qAlpha(wp)), 0, 255);
                    out[x - part.left()] = qRgba(std::min(a, mix(qRed(bp), qRed(wp))), std::min(a, mix(qGreen(bp), qGreen(wp))),
                                                 std::min(a, mix(qBlue(bp), qBlue(wp))), a);
                }
            }
        }
    }
}

void CarriedDeposit::carryInto(QImage &target, const QPoint &origin, const DabParams &dab, const QRect &rect)
{
    const BrushSettings &st = m_ctx.settings;
    const int n = m_patchSize;
    const int ox = int(std::floor(dab.center.x())) - n / 2, oy = int(std::floor(dab.center.y())) - n / 2;
    const DabShape shape(dab, st, m_ctx.tip);
    const bool wet = st.blend == BrushBlend::WetPaint;
    const bool dulling = !wet && st.smudgeMode == SmudgeMode::Dulling;
    const bool keepAlpha = st.smudgePreserveAlpha;
    // Every rate is per pass, like flow: normalised to the spacing so a pixel passed once is
    // affected the same however many dabs cover it.
    const double fraction = m_ctx.passExponent(dab.stepFraction);
    auto perPass = [&](double rate) { return float(1.0 - std::pow(1.0 - std::clamp(rate, 0.0, 1.0), fraction)); };
    const float lay = perPass(dab.strength); // smudge strength, or wet paint attack
    float take, tint;
    float brush[4];
    if (wet) {
        const double remaining = st.wetLoadLength > 0 ? std::max(0.0, 1.0 - dab.distance / st.wetLoadLength) : 1.0;
        tint = perPass(st.wetLoad * remaining); // refill towards the brush paint
        take = perPass(st.wetPull);
        const double dilution = std::clamp(st.wetDilution + st.wetnessJitter * m_ctx.random(dab.index, StrokeSalt::Wet), 0.0, 1.0);
        const float alpha = float(1.0 - dilution);
        brush[0] = qRed(dab.color) * alpha;
        brush[1] = qGreen(dab.color) * alpha;
        brush[2] = qBlue(dab.color) * alpha;
        brush[3] = 255.0f * alpha;
    } else {
        take = perPass(st.smudgePickup);
        tint = perPass(st.smudgeColorRate);
        brush[0] = float(qRed(dab.color));
        brush[1] = float(qGreen(dab.color));
        brush[2] = float(qBlue(dab.color));
        brush[3] = 255.0f;
    }
    double sum[4] = {0, 0, 0, 0};
    double weight = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        const int j = y - oy;
        if (j < 0 || j >= n)
            continue;
        QRgb *line = reinterpret_cast<QRgb *>(target.scanLine(y - origin.y()));
        const double dy = y + 0.5 - dab.center.y();
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const int i = x - ox;
            if (i < 0 || i >= n)
                continue;
            double v = shape.value(x + 0.5 - dab.center.x(), dy);
            if (v <= 0.0)
                continue;
            if (m_ctx.texture) {
                const double valley = dab.textureStrength * (1.0 - m_ctx.grain(x + 0.5, y + 0.5));
                v *= 1.0 - valley;
            }
            if (v <= 0.0)
                continue;
            float *c = &m_patch[(size_t(j) * n + i) * 4];
            const QRgb p = line[x - origin.x()];
            const float d[4] = {float(qRed(p)), float(qGreen(p)), float(qBlue(p)), float(qAlpha(p))};
            const float fv = float(v);
            float r[4];
            if (wet) {
                for (int k = 0; k < 4; ++k)
                    c[k] += (brush[k] - c[k]) * tint * fv;
                // The carried paint composited over the pixel (never lowers its alpha).
                const float a = lay * fv;
                const float keep = 1.0f - a * c[3] / 255.0f;
                for (int k = 0; k < 4; ++k) {
                    r[k] = c[k] * a + d[k] * keep;
                    c[k] += (d[k] - c[k]) * take * fv; // picks up the pixel as it was before this dab
                }
            } else {
                for (int k = 0; k < 4; ++k) {
                    r[k] = d[k] + (c[k] - d[k]) * lay * fv;
                    c[k] += (d[k] - c[k]) * take * fv;
                    if (tint > 0)
                        c[k] += (brush[k] - c[k]) * tint * fv;
                }
                if (dulling) {
                    for (int k = 0; k < 4; ++k)
                        sum[k] += c[k] * v;
                    weight += v;
                }
            }
            if (keepAlpha) {
                // Colour moves, alpha stays: rescale the colour to the pixel's own alpha.
                if (r[3] > 0.5f && d[3] > 0) {
                    const float scale = d[3] / r[3];
                    for (int k = 0; k < 3; ++k)
                        r[k] *= scale;
                } else {
                    for (int k = 0; k < 3; ++k)
                        r[k] = d[k];
                }
                r[3] = d[3];
            }
            const int alpha = std::clamp(int(std::lround(r[3])), 0, 255);
            // Premultiplied: no channel may exceed alpha (transparent pixels carry no colour).
            line[x - origin.x()] = qRgba(std::clamp(int(std::lround(r[0])), 0, alpha),
                                         std::clamp(int(std::lround(r[1])), 0, alpha),
                                         std::clamp(int(std::lround(r[2])), 0, alpha), alpha);
        }
    }
    if (dulling && weight > 0) {
        // Dulling carries one colour: the average of what the brush now holds where it touched.
        const float mean[4] = {float(sum[0] / weight), float(sum[1] / weight), float(sum[2] / weight),
                               float(sum[3] / weight)};
        for (int i = 0; i < m_patchSize * m_patchSize; ++i)
            std::memcpy(&m_patch[size_t(i) * 4], mean, sizeof(mean));
    }
}

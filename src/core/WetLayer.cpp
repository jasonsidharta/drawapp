#include "WetLayer.h"

#include "BrushTips.h"
#include "MemoryBudget.h"
#include "TileSnapshot.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int T = 64;    // tile side
constexpr int N = T * T; // cells per tile

qint64 key(int column, int row)
{
    return (qint64(row) << 32) | quint32(column);
}

} // namespace

struct WetLayer::Tile
{
    // Two generations of water and suspended pigment (the step reads one, writes the other).
    std::vector<float> water[2];
    std::vector<float> suspended[2]; // 4 per cell: r, g, b (premultiplied by amount), amount
    std::vector<float> settled;      // 4 per cell
    std::vector<float> edge;         // per step: share of dry paper around the cell (0 inside)
    std::vector<QRgb> base;          // the layer before this paint arrived
    int current = 0;
    bool dirty = false; // pixels need writing
    QRect rect;         // in document pixels (clipped to the layer)

    static constexpr qsizetype Bytes = qsizetype(N) * (2 * 4 + 2 * 16 + 16 + 4 + 4);

    Tile()
    {
        for (int g = 0; g < 2; ++g) {
            water[g].assign(size_t(N), 0.0f);
            suspended[g].assign(size_t(N) * 4, 0.0f);
        }
        settled.assign(size_t(N) * 4, 0.0f);
        edge.assign(size_t(N), 0.0f);
        base.assign(size_t(N), 0u);
    }
};

WetLayer::WetLayer(QSize size)
    : m_size(size)
{
}

WetLayer::~WetLayer()
{
    MemoryBudget::release(qint64(m_tiles.size()) * Tile::Bytes, MemoryBudget::Category::Temporary);
}

qsizetype WetLayer::bytes() const
{
    return qsizetype(m_tiles.size()) * Tile::Bytes;
}

WetLayer::Tile *WetLayer::tileAt(int column, int row) const
{
    const auto it = m_tiles.find(key(column, row));
    return it == m_tiles.end() ? nullptr : it->second.get();
}

WetLayer::Tile *WetLayer::ensureTile(int column, int row, const StrokeSurface &image)
{
    if (Tile *t = tileAt(column, row))
        return t;
    if (int(m_tiles.size()) >= MaxTiles || !MemoryBudget::tryReserve(Tile::Bytes, MemoryBudget::Category::Temporary)) {
        m_allocationFailed = true;
        return nullptr;
    }
    auto tile = std::make_unique<Tile>();
    tile->rect = QRect(column * T, row * T, T, T) & QRect(QPoint(0, 0), m_size);
    for (int y = tile->rect.top(); y <= tile->rect.bottom(); ++y) {
        const QRgb *src = image.constRow(tile->rect.left(), y);
        QRgb *dst = tile->base.data() + (y - row * T) * T + (tile->rect.left() - column * T);
        if (src)
            std::copy(src, src + tile->rect.width(), dst);
    }
    Tile *raw = tile.get();
    m_tiles.emplace(key(column, row), std::move(tile));
    return raw;
}

double WetLayer::paperHeight(int x, int y) const
{
    if (!m_params.paper || m_params.tooth <= 0)
        return 1.0;
    const QImage &t = m_params.paper->image;
    const double v = t.constScanLine(((y % t.height()) + t.height()) % t.height())[((x % t.width()) + t.width()) % t.width()] / 255.0;
    return 1.0 - m_params.tooth * (1.0 - v);
}

float WetLayer::water(int x, int y) const
{
    const Tile *t = tileAt(x / T, y / T);
    return t ? t->water[t->current][size_t((y % T) * T + x % T)] : 0.0f;
}

float WetLayer::suspended(int x, int y) const
{
    const Tile *t = tileAt(x / T, y / T);
    return t ? t->suspended[t->current][size_t((y % T) * T + x % T) * 4 + 3] : 0.0f;
}

float WetLayer::settled(int x, int y) const
{
    const Tile *t = tileAt(x / T, y / T);
    return t ? t->settled[size_t((y % T) * T + x % T) * 4 + 3] : 0.0f;
}

bool WetLayer::addPaint(const StrokeSurface &image, TileSnapshot &before, const QRect &requested,
                        const std::function<double(int, int)> &footprint, QRgb color, double water, double pigment,
                        double pass)
{
    Q_UNUSED(before) // pixels change at the next render, which captures first
    const QRect rect = requested & QRect(QPoint(0, 0), m_size);
    const float c[3] = {float(qRed(color) / 255.0), float(qGreen(color) / 255.0), float(qBlue(color) / 255.0)};
    const float lift = float(std::clamp(m_params.lifting, 0.0, 1.0));
    const double fill = std::min(1.0, 4.0 * std::max(0.0, pass)); // share of the way to the brush's wetness
    for (int row = rect.top() / T; row <= rect.bottom() / T; ++row) {
        for (int column = rect.left() / T; column <= rect.right() / T; ++column) {
            const QRect part = rect & QRect(column * T, row * T, T, T);
            Tile *tile = nullptr;
            for (int y = part.top(); y <= part.bottom(); ++y) {
                const uchar *clip = m_clip.isNull() ? nullptr : m_clip.constScanLine(y);
                for (int x = part.left(); x <= part.right(); ++x) {
                    double v = footprint(x, y);
                    if (clip)
                        v *= clip[x] / 255.0;
                    if (v <= 0.0)
                        continue;
                    if (!tile) {
                        tile = ensureTile(column, row, image);
                        if (!tile)
                            return false;
                    }
                    const int lx = x - column * T, ly = y - row * T;
                    // The display average reaches one cell across a tile edge: the tile there must
                    // exist to show it, or the edge of the paint would depend on the tile grid.
                    if ((lx == 0 && column > 0 && !ensureTile(column - 1, row, image))
                        || (lx == T - 1 && (column + 1) * T < m_size.width() && !ensureTile(column + 1, row, image))
                        || (ly == 0 && row > 0 && !ensureTile(column, row - 1, image))
                        || (ly == T - 1 && (row + 1) * T < m_size.height() && !ensureTile(column, row + 1, image)))
                        return false;
                    const size_t i = size_t(ly * T + lx);
                    float &w = tile->water[tile->current][i];
                    float *s = &tile->suspended[tile->current][i * 4];
                    float *d = &tile->settled[i * 4];
                    // A loaded brush wets the paper under its whole contact area up to its own
                    // wetness, evenly (water levels out under the hairs; the tip's softness only
                    // anti-aliases the rim): passing again does not pile water up, a wetter brush
                    // in a damp wash adds water.
                    const float contact = float(std::min(1.0, 8.0 * v));
                    const float target = float(water) * contact;
                    const bool wasDry = w < DryWater;
                    if (w < target)
                        w += (target - w) * float(fill);
                    if (wasDry && w >= DryWater)
                        ++m_wetCells;
                    // New water loosens part of the settled pigment back into the wash (lifting).
                    if (d[3] > 0 && water > 0) {
                        const float f = std::min(1.0f, lift * contact * float(pass));
                        for (int k = 0; k < 4; ++k) {
                            s[k] += d[k] * f;
                            d[k] -= d[k] * f;
                        }
                    }
                    // Paint is exchanged with the brush: the wash under it moves towards the brush's
                    // own paint (its colour, PigmentScale × pigment) by the same share. A loaded
                    // brush lays an even wash, another colour displaces the wet wash and feathers
                    // into it at the edges, and a clean damp brush takes colour up.
                    const float share = float(fill) * contact;
                    const float amount = float(PigmentScale * pigment);
                    for (int k = 0; k < 3; ++k)
                        s[k] += (c[k] * amount - s[k]) * share;
                    s[3] += (amount - s[3]) * share;
                    tile->dirty = true;
                }
            }
        }
    }
    return true;
}

QRect WetLayer::step(StrokeSurface &image, TileSnapshot &before, int steps)
{
    const Params &p = m_params;
    const float dw = float(0.08 + 0.6 * std::clamp(p.spread, 0.0, 1.0)); // water diffusion (≤ 0.68: stable)
    const float dp = float(0.02 + 0.2 * std::clamp(p.spread, 0.0, 1.0)); // pigment diffusion
    const double stepsToDry = std::max(1.0, p.dryingSeconds * 1000.0 / StepMs);
    const float evaporation = float(0.6 / stepsToDry); // a typical wash (0.6) dries in time
    const float darkening = float(std::clamp(p.edgeDarkening, 0.0, 1.0));
    const float pull = 1.0f * darkening;      // lower pressure at the edge: water flows outwards
    const float edgeBoost = 3.0f * darkening; // and evaporates there up to 4× faster
    const float settle = 0.004f;
    const float granulation = float(3.0 * std::clamp(p.granulation, 0.0, 1.0));
    for (int s = 0; s < steps; ++s) {
        if (m_wetCells == 0)
            break;
        // Pass 1: how close each wet cell is to the edge of the wet area, the share of dry cells
        // within two cells (12 cells: a rim a few pixels wide, not a one-pixel line).
        for (auto &[k, tilePtr] : m_tiles) {
            Tile &tile = *tilePtr;
            const int column = int(quint32(k & 0xFFFFFFFF)), row = int(k >> 32);
            const Tile *around[3][3];
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    around[dy + 1][dx + 1] = (column + dx < 0 || row + dy < 0) ? nullptr : tileAt(column + dx, row + dy);
            auto waterAt = [&](int gx, int gy) -> float {
                if (gx < 0 || gy < 0 || gx >= m_size.width() || gy >= m_size.height())
                    return 0.0f;
                const Tile *t = around[gy / T - row + 1][gx / T - column + 1];
                return t ? t->water[t->current][size_t((gy % T) * T + gx % T)] : 0.0f;
            };
            const float *W = tile.water[tile.current].data();
            for (int ly = 0; ly < T; ++ly)
                for (int lx = 0; lx < T; ++lx) {
                    const size_t i = size_t(ly * T + lx);
                    if (W[i] < DryWater) {
                        tile.edge[i] = 0.0f;
                        continue;
                    }
                    static const int ring[12][2] = {{-1, 0}, {1, 0},  {0, -1}, {0, 1},  {-2, 0}, {2, 0},
                                                    {0, -2}, {0, 2},  {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
                    const int gx = column * T + lx, gy = row * T + ly;
                    int dry = 0;
                    for (const auto &o : ring)
                        dry += waterAt(gx + o[0], gy + o[1]) < DryWater;
                    tile.edge[i] = float(dry) / 12.0f;
                }
        }
        // Pass 2: flow, evaporation and settling. Every tile reads the previous generation and
        // writes the next, so the result does not depend on tile order and there are no seams.
        qint64 wet = 0;
        for (auto &[k, tilePtr] : m_tiles) {
            Tile &tile = *tilePtr;
            const int column = int(quint32(k & 0xFFFFFFFF)), row = int(k >> 32);
            const int cur = tile.current, nxt = 1 - cur;
            const float *W = tile.water[cur].data();
            const float *S = tile.suspended[cur].data();
            const float *E = tile.edge.data();
            float *W2 = tile.water[nxt].data();
            float *S2 = tile.suspended[nxt].data();
            float *D = tile.settled.data();
            const Tile *left = tileAt(column - 1, row), *right = tileAt(column + 1, row);
            const Tile *up = row > 0 ? tileAt(column, row - 1) : nullptr, *down = tileAt(column, row + 1);
            bool changed = false;
            for (int ly = 0; ly < T; ++ly) {
                for (int lx = 0; lx < T; ++lx) {
                    const size_t i = size_t(ly * T + lx);
                    const float w = W[i];
                    if (w < DryWater && S[i * 4 + 3] == 0.0f) {
                        W2[i] = w;
                        for (int c = 0; c < 4; ++c)
                            S2[i * 4 + c] = S[i * 4 + c];
                        continue;
                    }
                    changed = true;
                    // The four neighbours (previous generation); outside the layer or in a tile that
                    // was never wet: dry paper.
                    float nw[4], ne[4];
                    const float *ns[4];
                    static const float zero[4] = {0, 0, 0, 0};
                    auto neighbour = [&](int n, const Tile *t, size_t j, bool inside) {
                        if (inside) {
                            nw[n] = W[j];
                            ne[n] = E[j];
                            ns[n] = S + j * 4;
                        } else if (t) {
                            nw[n] = t->water[t->current][j];
                            ne[n] = t->edge[j];
                            ns[n] = t->suspended[t->current].data() + j * 4;
                        } else {
                            nw[n] = 0.0f;
                            ne[n] = 0.0f;
                            ns[n] = zero;
                        }
                    };
                    const int gx = column * T + lx, gy = row * T + ly;
                    if (lx > 0)
                        neighbour(0, nullptr, i - 1, true);
                    else
                        neighbour(0, gx > 0 ? left : nullptr, size_t(ly * T + T - 1), false);
                    if (lx < T - 1)
                        neighbour(1, nullptr, i + 1, true);
                    else
                        neighbour(1, gx + 1 < m_size.width() ? right : nullptr, size_t(ly * T), false);
                    if (ly > 0)
                        neighbour(2, nullptr, i - T, true);
                    else
                        neighbour(2, up, size_t((T - 1) * T + lx), false);
                    if (ly < T - 1)
                        neighbour(3, nullptr, i + T, true);
                    else
                        neighbour(3, gy + 1 < m_size.height() ? down : nullptr, size_t(lx), false);

                    float w2 = w;
                    float s2[4] = {S[i * 4], S[i * 4 + 1], S[i * 4 + 2], S[i * 4 + 3]};
                    const bool wetHere = w >= DryWater;
                    if (wetHere) {
                        // Water moves down the pressure difference between wet cells; near the edge
                        // the pressure is lower (edge darkening), so water and the pigment it carries
                        // flow outwards while the edge stays wet. Dry paper takes no water (wet-on-dry
                        // edges stay hard). The flux is limited to a fifth of either cell's water, the
                        // same limit seen from both sides, so water and pigment are conserved.
                        const float pressure = w * (1.0f - pull * E[i]);
                        for (int n = 0; n < 4; ++n) {
                            if (nw[n] < DryWater)
                                continue;
                            const float raw = dw * 0.25f * (pressure - nw[n] * (1.0f - pull * ne[n]));
                            const float flux = std::clamp(raw, -0.2f * nw[n], 0.2f * w); // i → n
                            w2 -= flux;
                            for (int c = 0; c < 4; ++c) {
                                const float carried = flux > 0 ? flux * S[i * 4 + c] / w : flux * ns[n][c] / nw[n];
                                s2[c] -= carried;
                                s2[c] += dp * 0.25f * (ns[n][c] - S[i * 4 + c]);
                            }
                        }
                        // Evaporation takes water, not pigment, and is faster at the edge: what the
                        // outward flow brings there stays there (the dark rim of a dried wash).
                        w2 -= evaporation * (1.0f + edgeBoost * E[i]);
                        // Settling, more in the paper's valleys.
                        const float rate = std::min(1.0f, settle * (1.0f + granulation * float(1.0 - paperHeight(gx, gy))));
                        for (int c = 0; c < 4; ++c) {
                            const float dep = std::max(0.0f, s2[c]) * rate;
                            s2[c] -= dep;
                            D[i * 4 + c] += dep;
                        }
                    }
                    if (w2 < DryWater) {
                        // Dry: everything left settles here.
                        w2 = 0.0f;
                        for (int c = 0; c < 4; ++c) {
                            D[i * 4 + c] += std::max(0.0f, s2[c]);
                            s2[c] = 0.0f;
                        }
                    } else {
                        ++wet;
                    }
                    W2[i] = w2;
                    for (int c = 0; c < 4; ++c)
                        S2[i * 4 + c] = std::max(0.0f, s2[c]);
                }
            }
            if (changed)
                tile.dirty = true;
        }
        for (auto &[k, tilePtr] : m_tiles)
            tilePtr->current = 1 - tilePtr->current;
        m_wetCells = wet;
    }
    return render(image, before);
}

QRect WetLayer::render(StrokeSurface &image, TileSnapshot &before)
{
    // The display average reaches one cell into the neighbouring tiles: they are redrawn too.
    std::vector<qint64> spread;
    for (auto &[k, tilePtr] : m_tiles)
        if (tilePtr->dirty) {
            const int column = int(quint32(k & 0xFFFFFFFF)), row = int(k >> 32);
            for (const auto &o : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}})
                if (column + o.first >= 0 && row + o.second >= 0)
                    spread.push_back(key(column + o.first, row + o.second));
        }
    for (const qint64 k : spread)
        if (auto it = m_tiles.find(k); it != m_tiles.end())
            it->second->dirty = true;
    QRect changed;
    for (auto &[k, tilePtr] : m_tiles)
        if (tilePtr->dirty)
            writeTile(image, before, k, *tilePtr, changed);
    return changed;
}

void WetLayer::writeTile(StrokeSurface &image, TileSnapshot &before, qint64 k, Tile &tile, QRect &changed)
{
    const QRect r = tile.rect;
    if (r.isEmpty())
        return;
    if (!before.storedRow(r.left(), r.top()))
        image.capture(before, r);
    const int column = int(quint32(k & 0xFFFFFFFF)), row = int(k >> 32);
    const float density = float(m_params.density);
    // Pigment as displayed: a 5-point average (the cell twice, its four neighbours once), so the
    // edge of a wash is anti-aliased instead of following the cell grid. The simulation itself is
    // not blurred.
    const Tile *around[3][3];
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            around[dy + 1][dx + 1] = (column + dx < 0 || row + dy < 0) ? nullptr : tileAt(column + dx, row + dy);
    auto pigmentAt = [&](int gx, int gy, float *out) {
        if (gx < 0 || gy < 0 || gx >= m_size.width() || gy >= m_size.height())
            return;
        const Tile *t = around[gy / T - row + 1][gx / T - column + 1];
        if (!t)
            return;
        const size_t j = size_t((gy % T) * T + gx % T) * 4;
        const float *s = t->suspended[t->current].data() + j, *d = t->settled.data() + j;
        for (int c = 0; c < 4; ++c)
            out[c] += s[c] + d[c];
    };
    for (int y = r.top(); y <= r.bottom(); ++y) {
        QRgb *out = image.writableRow(r.left(), y);
        if (!out) {
            m_allocationFailed = true;
            return;
        }
        const uchar *clip = m_clip.isNull() ? nullptr : m_clip.constScanLine(y);
        for (int x = r.left(); x <= r.right(); ++x) {
            const size_t i = size_t((y - row * T) * T + (x - column * T));
            const QRgb b = tile.base[i];
            float sum[4] = {0, 0, 0, 0};
            pigmentAt(x, y, sum);
            pigmentAt(x, y, sum);
            pigmentAt(x - 1, y, sum);
            pigmentAt(x + 1, y, sum);
            pigmentAt(x, y - 1, sum);
            pigmentAt(x, y + 1, sum);
            const float a = sum[3] / 6.0f;
            if (a <= 1e-6f) {
                out[x - r.left()] = b;
                continue;
            }
            // Pigment colour (straight) and glaze opacity.
            float col[3];
            for (int c = 0; c < 3; ++c)
                col[c] = std::clamp(sum[c] / sum[3], 0.0f, 1.0f);
            const float alpha = 1.0f - std::exp(-density * a);
            const float ba = qAlpha(b) / 255.0f;
            const float bc[3] = {qRed(b) / 255.0f, qGreen(b) / 255.0f, qBlue(b) / 255.0f}; // premultiplied
            float o[4];
            // Multiply glaze over paint, plain colour over transparency (the W3C multiply formula).
            for (int c = 0; c < 3; ++c)
                o[c] = bc[c] * (1.0f - alpha + alpha * col[c]) + col[c] * alpha * (1.0f - ba);
            o[3] = ba + alpha * (1.0f - ba);
            if (clip) {
                const float m = clip[x] / 255.0f;
                const float bb[4] = {bc[0], bc[1], bc[2], ba};
                for (int c = 0; c < 4; ++c)
                    o[c] = bb[c] + (o[c] - bb[c]) * m;
            }
            const int oa = std::clamp(int(std::lround(o[3] * 255.0f)), 0, 255);
            out[x - r.left()] = qRgba(std::clamp(int(std::lround(o[0] * 255.0f)), 0, oa), std::clamp(int(std::lround(o[1] * 255.0f)), 0, oa),
                                      std::clamp(int(std::lround(o[2] * 255.0f)), 0, oa), oa);
        }
    }
    tile.dirty = false;
    changed |= r;
}

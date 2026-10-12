#include "BrushTips.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {

constexpr double Pi = 3.14159265358979323846;

quint64 mix64(quint64 x)
{
    // splitmix64 finaliser.
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

double smooth(double t)
{
    return t * t * (3.0 - 2.0 * t);
}

// Value noise on an integer lattice that wraps every `period` cells, so it tiles.
double valueNoise(double x, double y, int period, quint64 salt)
{
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const double fx = smooth(x - x0), fy = smooth(y - y0);
    auto at = [&](int ix, int iy) {
        const int wx = ((ix % period) + period) % period, wy = ((iy % period) + period) % period;
        return BrushTips::hash01(wx, wy, salt);
    };
    const double a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
    const double b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
    return a + (b - a) * fy;
}

// Tileable fractal noise in [0, 1] over a square of `size` pixels with `cells` lattice cells.
double fbm(double x, double y, int size, int cells, int octaves, quint64 salt)
{
    double sum = 0, amp = 1, norm = 0;
    for (int o = 0; o < octaves; ++o) {
        const int period = cells << o;
        sum += amp * valueNoise(x * period / size, y * period / size, period, salt + quint64(o) * 7919u);
        norm += amp;
        amp *= 0.5;
    }
    return sum / norm;
}

QImage gray(int w, int h)
{
    QImage image(w, h, QImage::Format_Grayscale8);
    image.fill(0);
    return image;
}

quint8 toByte(double v)
{
    return quint8(std::clamp(int(std::lround(v * 255.0)), 0, 255));
}

// Paper tooth: soft fibres (stretched noise) over fine grain, with a contrast curve so that
// valleys are distinct. Mean around 0.55.
QImage paperTexture()
{
    const int n = 256;
    QImage image = gray(n, n);
    for (int y = 0; y < n; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < n; ++x) {
            const double fibres = fbm(x * 0.5, y * 2.0, n, 16, 3, 101); // elongated along x
            const double grain = fbm(x, y, n, 128, 2, 202);                // about 2 px
            double v = 0.35 * fibres + 0.65 * grain;
            v = smooth(std::clamp((v - 0.25) / 0.5, 0.0, 1.0));
            row[x] = toByte(v);
        }
    }
    return image;
}

// Canvas weave: alternating over/under threads in 8-pixel cells, each thread with a rounded
// profile and slight irregularity.
QImage canvasTexture()
{
    const int n = 256, cell = 8;
    QImage image = gray(n, n);
    for (int y = 0; y < n; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < n; ++x) {
            const int cx = x / cell, cy = y / cell;
            const bool horizontal = ((cx + cy) & 1) == 0;
            const double u = (horizontal ? (y % cell) : (x % cell)) + 0.5;
            const double profile = std::sin(Pi * u / cell); // thread cross-section
            const double along = (horizontal ? x : y) * 2.0 * Pi / (cell * 2);
            const double wobble = 0.08 * std::sin(along);
            const double noise = fbm(x, y, n, 32, 2, 303);
            row[x] = toByte(std::clamp(0.15 + 0.7 * profile + wobble + 0.25 * (noise - 0.5), 0.0, 1.0));
        }
    }
    return image;
}

// Flat bristle tip: a row of hairs across the tip (along y), each a soft, slightly elongated blob
// of its own width and load. Moving along x with small spacing, the hairs draw parallel streaks.
QImage bristleTip()
{
    const int n = 128;
    QImage image = gray(n, n);
    std::vector<double> acc(size_t(n) * n, 0.0);
    const int hairs = 34;
    for (int i = 0; i < hairs; ++i) {
        const double t = (i + 0.5) / hairs;
        const double cy = n * (0.08 + 0.84 * t) + (BrushTips::hash01(i, 1, 404) - 0.5) * 3.0;
        const double cx = n * 0.5 + (BrushTips::hash01(i, 2, 404) - 0.5) * n * 0.18;
        const double ry = 2.2 + 2.6 * BrushTips::hash01(i, 3, 404);
        const double rx = ry * (1.6 + BrushTips::hash01(i, 4, 404));
        // Hairs near the edges of the brush carry less paint.
        const double edge = std::sin(Pi * t);
        const double load = (0.6 + 0.4 * BrushTips::hash01(i, 5, 404)) * (0.6 + 0.4 * edge);
        for (int y = std::max(0, int(cy - 3 * ry)); y < std::min(n, int(cy + 3 * ry) + 1); ++y)
            for (int x = std::max(0, int(cx - 3 * rx)); x < std::min(n, int(cx + 3 * rx) + 1); ++x) {
                const double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry;
                const double d2 = dx * dx + dy * dy;
                if (d2 < 9.0)
                    acc[size_t(y) * n + x] += load * std::exp(-1.6 * d2);
            }
    }
    for (int y = 0; y < n; ++y) {
        uchar *row = image.scanLine(y);
        for (int x = 0; x < n; ++x)
            row[x] = toByte(1.0 - std::exp(-3.5 * acc[size_t(y) * n + x])); // soft saturation
    }
    return image;
}

std::vector<QImage> mipChain(const QImage &base)
{
    std::vector<QImage> mips{base};
    while (mips.back().width() > 1) {
        const QImage &src = mips.back();
        const int w = std::max(1, src.width() / 2), h = std::max(1, src.height() / 2);
        QImage dst = gray(w, h);
        for (int y = 0; y < h; ++y) {
            const uchar *r0 = src.constScanLine(std::min(2 * y, src.height() - 1));
            const uchar *r1 = src.constScanLine(std::min(2 * y + 1, src.height() - 1));
            uchar *out = dst.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const int x0 = std::min(2 * x, src.width() - 1), x1 = std::min(2 * x + 1, src.width() - 1);
                out[x] = uchar((r0[x0] + r0[x1] + r1[x0] + r1[x1] + 2) / 4);
            }
        }
        mips.push_back(dst);
    }
    return mips;
}

double passLength(const QImage &tip)
{
    double best = 0;
    for (int y = 0; y < tip.height(); ++y) {
        const uchar *row = tip.constScanLine(y);
        double sum = 0;
        for (int x = 0; x < tip.width(); ++x)
            sum += row[x] / 255.0;
        best = std::max(best, sum / tip.width());
    }
    return std::max(0.05, best);
}

struct Library
{
    std::map<QString, BrushTips::Tip> tips;
    std::map<QString, BrushTips::Texture> textures;
};

const Library &library()
{
    // Built once (thread-safe static initialisation), never modified afterwards.
    static const Library lib = [] {
        Library l;
        const QImage bristle = bristleTip();
        l.tips[QStringLiteral("bristle")] = {QStringLiteral("bristle"), QStringLiteral("Flat bristle"), mipChain(bristle),
                                             passLength(bristle)};
        l.textures[QStringLiteral("paper")] = {QStringLiteral("paper"), QStringLiteral("Paper tooth"), paperTexture()};
        l.textures[QStringLiteral("canvas")] = {QStringLiteral("canvas"), QStringLiteral("Canvas weave"), canvasTexture()};
        return l;
    }();
    return lib;
}

} // namespace

double BrushTips::roundPassLength(double hardness)
{
    // The core is flat; the falloff (1 − t²)² integrates to 8/15 over the rim.
    const double h = std::clamp(hardness, 0.0, 1.0);
    return h + (1.0 - h) * 8.0 / 15.0;
}

double BrushTips::hash01(qint64 a, qint64 b, quint64 salt)
{
    const quint64 h = mix64(mix64(quint64(a) * 0x9E3779B97F4A7C15ull ^ salt) ^ quint64(b));
    return double(h >> 11) * (1.0 / 9007199254740992.0);
}

const BrushTips::Tip *BrushTips::tip(const QString &id)
{
    const auto &tips = library().tips;
    const auto it = tips.find(id);
    return it == tips.end() ? nullptr : &it->second;
}

const BrushTips::Texture *BrushTips::texture(const QString &id)
{
    const auto &textures = library().textures;
    const auto it = textures.find(id);
    return it == textures.end() ? nullptr : &it->second;
}

QStringList BrushTips::tipIds()
{
    QStringList ids;
    for (const auto &[id, tip] : library().tips)
        ids << id;
    return ids;
}

QStringList BrushTips::textureIds()
{
    QStringList ids;
    for (const auto &[id, texture] : library().textures)
        ids << id;
    return ids;
}

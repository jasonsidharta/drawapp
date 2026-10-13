#pragma once

// Raster helpers shared by the stroke pipeline and its deposit strategies (StrokeDeposit.h):
// tile keys, 8-bit arithmetic, the dither/rounding thresholds, texture sampling and the dab
// footprint. Moved unchanged from StrokeEngine.cpp in milestone 11 so that every strategy
// computes exactly what the milestone 10 engine computed (tests/golden/m10-engine-reference.json).

#include "BrushSettings.h"
#include "BrushTips.h"

#include <QImage>
#include <QRgb>
#include <QtGlobal>
#include <algorithm>
#include <array>
#include <cmath>

namespace BrushRaster {

constexpr double Pi = 3.14159265358979323846;
constexpr int Tile = 64;
constexpr quint32 FullCoverage = 65535; // = 255 × 257, so whole 8-bit levels are exact

inline qint64 tileKey(int column, int row)
{
    return (qint64(row) << 32) | quint32(column);
}

// x * y / 255, rounded, for 0..255 operands.
inline quint32 mul255(quint32 x, quint32 y)
{
    const quint32 t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

// Fixed thresholds in [0, 1) on a 64 × 64 tile, for dithering and stochastic rounding.
inline const std::array<float, Tile * Tile> &thresholds()
{
    static const std::array<float, Tile * Tile> table = [] {
        std::array<float, Tile * Tile> t{};
        for (int i = 0; i < Tile * Tile; ++i)
            t[size_t(i)] = float(BrushTips::hash01(i % Tile, i / Tile, 0xD17Eu)) * 0.999f;
        return t;
    }();
    return table;
}

inline float threshold(int x, int y)
{
    return thresholds()[size_t((y & (Tile - 1)) * Tile + (x & (Tile - 1)))];
}

// Bilinear sample of a Grayscale8 image (pixel centres at i + 0.5); 0 outside the image.
inline double sampleBounded(const QImage &image, double x, double y)
{
    x -= 0.5;
    y -= 0.5;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    const int w = image.width(), h = image.height();
    const uchar *bits = image.constBits();
    const qsizetype bpl = image.bytesPerLine();
    auto at = [&](int ix, int iy) -> double {
        return (ix < 0 || iy < 0 || ix >= w || iy >= h) ? 0.0 : double(bits[iy * bpl + ix]);
    };
    const double a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
    const double b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
    return (a + (b - a) * fy) / 255.0;
}

// Bilinear sample of a tiling Grayscale8 image.
inline double sampleWrapped(const QImage &image, double x, double y)
{
    x -= 0.5;
    y -= 0.5;
    const int w = image.width(), h = image.height();
    const double fx0 = std::floor(x), fy0 = std::floor(y);
    // Keep the lattice index small so huge offsets cannot overflow int.
    const int x0 = int(std::fmod(fx0, double(w))), y0 = int(std::fmod(fy0, double(h)));
    const double fx = x - fx0, fy = y - fy0;
    const int xa = ((x0 % w) + w) % w, ya = ((y0 % h) + h) % h;
    const int xb = (xa + 1) % w, yb = (ya + 1) % h;
    const uchar *ra = image.constScanLine(ya), *rb = image.constScanLine(yb);
    const double a = ra[xa] + (ra[xb] - ra[xa]) * fx;
    const double b = rb[xa] + (rb[xb] - rb[xa]) * fx;
    return (a + (b - a) * fy) / 255.0;
}

// (c·s + d·add) / (s + add) per channel of two opaque colours.
inline quint32 mixColor(quint32 c, quint32 s, quint32 d, quint32 add)
{
    const quint32 total = s + add;
    auto ch = [&](int shift) {
        return ((((c >> shift) & 0xFF) * s + ((d >> shift) & 0xFF) * add + total / 2) / total) << shift;
    };
    return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

} // namespace BrushRaster

#pragma once

// What one stroke shares between StrokeEngine (the pipeline: input, smoothing, path sensors,
// taper, spacing, dynamics, dab parameters) and its deposit strategy (StrokeDeposit.h: what a dab
// does to the stroke buffers and the layer). Fixed when the stroke starts, except the allocation
// failure flag. research/m11-brush-architecture.md, decision A1.

#include "BrushRaster.h"
#include "BrushSettings.h"
#include "StrokeSurface.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>

namespace BrushTips {
struct Tip;
struct Texture;
}
class WetLayer;

// The parameters of one dab, as derived from the brush settings and the input sample.
struct DabParams
{
    QPointF center;
    double diameter = 1;
    double ceiling = 1;  // 0..1, a whole 8-bit level
    double flow = 1;     // per dab, already normalised to the spacing
    double passFlow = 1; // the flow setting: what one pass deposits (towards the ceiling)
    double angle = 0;    // radians
    double strength = 0; // smudge: strength; wet paint: attack
    double colorRate = 0; // smudge: brush colour mixed into the carried paint (setting × dynamics)
    double roundness = 1;
    bool flipX = false;
    bool flipY = false;
    double stepFraction = 0.15; // step after this dab / diameter
    double textureStrength = 0;
    double distance = 0;  // along the stroke, px (wet paint load)
    QRgb color = 0;       // opaque stroke colour of this dab
    qint64 index = 0;     // render index, for per-dab randomness
    double pressure = 1;  // the dab's pressure (1 without pressure data)
    double tilt = 0;      // pen tilt amount, 0 (upright or no tilt data) .. 1 (full tilt setting)
    double timeMs = -1;   // input event time of the dab (< 0 unknown)
    double direction = 0; // heading of the stroke at the dab, radians (0 before it has moved)
    bool hasDirection = false;
};

// Random streams: one salt per use, so changing one setting never reshuffles another's values.
namespace StrokeSalt {
enum : quint64 {
    Angle = 0xA9u, // milestone 6 value, kept so existing strokes render the same
    Size = 0x5100u,
    StrokeSize,
    Spacing,
    ScatterAcross,
    ScatterAlong,
    Count,
    Opacity,
    Flow,
    Roundness,
    FlipX,
    FlipY,
    Hue,
    Saturation,
    Lightness,
    StrokeHue,
    StrokeSaturation,
    StrokeLightness,
    Density,
    SprayAngle,
    SprayRadius,
    SpraySize,
    Wet,
    TextureX,
    TextureY,
    // Milestone 11 salts start here; the values above must never change.
    Milestone11 = 0x11000u,
};
}

struct StrokeContext
{
    BrushSettings settings;
    quint32 color = 0; // opaque RGB as ARGB32, after the per-stroke colour jitter
    quint64 seed = 0;
    bool buildUp = false;
    bool coverageBlend = true; // Paint or Erase
    bool colorPerDab = false;
    bool textureEachDab = false;
    const BrushTips::Tip *tip = nullptr;
    const BrushTips::Texture *texture = nullptr;
    bool textureNearest = true; // scale 1, no rotation: nearest texel
    QPoint textureOffset;
    double textureCos = 1, textureSin = 0;
    double passLength = 1.0; // in diameters
    QImage clip;             // Grayscale8 selection, or null
    bool allocationFailed = false;
    WetLayer *wet = nullptr; // watercolour: the layer's wet state, kept by the canvas (or null)

    double random(qint64 index, quint64 salt) const { return BrushTips::hash01(qint64(seed), index, salt); }

    // Dabs per pass along the tip's densest line = pass length / step; each dab gets the share
    // of the per-pass amount that makes the pass add up to it.
    double passExponent(double stepFraction) const { return std::min(1.0, stepFraction / passLength); }

    double grain(double x, double y) const;
    QRect dabRect(const DabParams &dab, const StrokeSurface &image) const;
};

// The footprint of one dab: tip value 0..1 at an offset from the dab centre.
struct DabShape
{
    double radius;
    double cosA, sinA;
    double invRoundness;
    double hardness;
    double flipU, flipW;
    bool small;
    const QImage *mip = nullptr; // sampled tips
    double mipScale = 1;         // tip pixels per document pixel

    // Tips smaller than this radius are area-sampled (4 × 4 per pixel) instead of edge-estimated.
    static constexpr double SmallTipRadius = 3.0;

    DabShape(const DabParams &dab, const BrushSettings &s, const BrushTips::Tip *tip);

    // Round tip without edge anti-aliasing: hard disc times the soft falloff.
    double roundPoint(double u, double w) const
    {
        const double d = std::hypot(u, w);
        if (d > radius)
            return 0.0;
        return falloff(d / radius);
    }

    // Smooth falloff from the hard core (hardness) to the rim: (1 − t²)², flat at both ends so
    // neither the core nor the rim shows a crease.
    double falloff(double x) const
    {
        if (hardness >= 1.0 || x <= hardness)
            return 1.0;
        const double t = (x - hardness) / (1.0 - hardness);
        if (t >= 1.0)
            return 0.0;
        const double q = 1.0 - t * t;
        return q * q;
    }

    double value(double dx, double dy) const;
};

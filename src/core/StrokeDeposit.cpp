#include "StrokeDeposit.h"

#include "BristleDeposit.h"
#include "BrushTips.h"
#include "CarriedDeposit.h"
#include "CoverageDeposit.h"
#include "DryDeposit.h"

#include <cmath>

using namespace BrushRaster;

double StrokeContext::grain(double x, double y) const
{
    const BrushSettings &s = settings;
    const QImage &t = texture->image;
    double value;
    if (textureNearest) {
        const int ix = int(std::floor(x)) + textureOffset.x(), iy = int(std::floor(y)) + textureOffset.y();
        value = t.constScanLine(((iy % t.height()) + t.height()) % t.height())[((ix % t.width()) + t.width()) % t.width()]
              / 255.0;
    } else {
        double u = x + textureOffset.x(), w = y + textureOffset.y();
        if (s.textureRotation != 0.0) {
            const double ru = u * textureCos + w * textureSin;
            w = -u * textureSin + w * textureCos;
            u = ru;
        }
        value = sampleWrapped(t, u / s.textureScale, w / s.textureScale);
    }
    if (s.textureInvert)
        value = 1.0 - value;
    if (s.textureContrast != 1.0 || s.textureBrightness != 0.0)
        value = std::clamp((value - 0.5) * s.textureContrast + 0.5 + s.textureBrightness, 0.0, 1.0);
    return value;
}

QRect StrokeContext::dabRect(const DabParams &dab, const StrokeSurface &image) const
{
    // A sampled tip is a square that may be rotated; a round tip never extends past its radius.
    double r = dab.diameter / 2.0 * (tip ? 1.4143 : 1.0) + 1.0;
    if (settings.stamp == BrushStamp::Spray)
        r = dab.diameter / 2.0 + settings.sprayParticleSize / 2.0 + 2.0;
    return QRectF(dab.center.x() - r, dab.center.y() - r, 2 * r, 2 * r).toAlignedRect() & image.rect();
}

DabShape::DabShape(const DabParams &dab, const BrushSettings &s, const BrushTips::Tip *tip)
    : radius(dab.diameter / 2.0)
    , cosA(std::cos(dab.angle))
    , sinA(std::sin(dab.angle))
    , invRoundness(1.0 / std::max(0.05, dab.roundness))
    , hardness(s.hardness)
    , flipU(dab.flipX ? -1.0 : 1.0)
    , flipW(dab.flipY ? -1.0 : 1.0)
    , small(radius < SmallTipRadius)
{
    if (tip && !tip->mips.empty()) {
        // The smallest level still at least as large as the dab, so it is only ever magnified
        // slightly or reduced by its own mip filter.
        size_t level = 0;
        while (level + 1 < tip->mips.size() && tip->mips[level + 1].width() >= dab.diameter)
            ++level;
        mip = &tip->mips[level];
        mipScale = mip->width() / dab.diameter;
    }
}

double DabShape::value(double dx, double dy) const
{
    const double u = (dx * cosA + dy * sinA) * flipU;
    const double w = (-dx * sinA + dy * cosA) * invRoundness * flipW;
    if (mip) {
        const double half = mip->width() / 2.0;
        return sampleBounded(*mip, half + u * mipScale, half + w * mipScale);
    }
    if (small) {
        // Area coverage from a 4 × 4 grid of points inside the pixel.
        double sum = 0;
        for (int sy = 0; sy < 4; ++sy)
            for (int sx = 0; sx < 4; ++sx) {
                const double ox = (sx + 0.5) / 4.0 - 0.5, oy = (sy + 0.5) / 4.0 - 0.5;
                const double su = (dx + ox) * cosA + (dy + oy) * sinA;
                const double sw = (-(dx + ox) * sinA + (dy + oy) * cosA) * invRoundness;
                sum += roundPoint(su, sw);
            }
        return sum / 16.0;
    }
    // The pixel's coverage by the disc, estimated from its distance to the edge.
    const double d = std::hypot(u, w);
    const double edge = std::clamp(radius + 0.5 - d, 0.0, 1.0);
    if (edge <= 0.0)
        return 0.0;
    return std::min(edge, falloff(std::min(1.0, d / radius)));
}

std::unique_ptr<StrokeDeposit> StrokeDeposit::create(StrokeContext &context)
{
    if (context.coverageBlend && context.settings.stamp == BrushStamp::Dry)
        return std::make_unique<DryDeposit>(context);
    if (context.coverageBlend && context.settings.stamp == BrushStamp::Bristle)
        return std::make_unique<BristleDeposit>(context);
    if (context.coverageBlend)
        return std::make_unique<CoverageDeposit>(context);
    return std::make_unique<CarriedDeposit>(context);
}

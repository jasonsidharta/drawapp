#include "PigmentMix.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr double MinReflectance = 1e-4; // K/S is unbounded at R = 0

double toLinear(double v) // sRGB 0..1 → linear
{
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double toSrgb(double v) // linear 0..1 → sRGB
{
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

double ks(double r)
{
    r = std::clamp(r, MinReflectance, 1.0);
    return (1.0 - r) * (1.0 - r) / (2.0 * r);
}

double reflectance(double k)
{
    return 1.0 + k - std::sqrt(k * k + 2.0 * k);
}

// K/S of each 8-bit sRGB level (the inputs are 8-bit pixels or close to them), and linear → sRGB
// on a fine grid, so mixing costs table look-ups and one square root per channel.
struct Tables
{
    std::array<double, 256 * 4 + 1> ksOf{}; // sRGB in quarter steps of 0..255
    std::array<float, 4097> srgbOf{};      // linear 0..1 in 1/4096 steps → sRGB 0..255
    Tables()
    {
        for (size_t i = 0; i < ksOf.size(); ++i)
            ksOf[i] = ks(toLinear(double(i) / (ksOf.size() - 1)));
        for (size_t i = 0; i < srgbOf.size(); ++i)
            srgbOf[i] = float(255.0 * toSrgb(double(i) / (srgbOf.size() - 1)));
    }
};

const Tables &tables()
{
    static const Tables t;
    return t;
}

double ksOf(float srgb255)
{
    const Tables &t = tables();
    const double pos = std::clamp(double(srgb255), 0.0, 255.0) / 255.0 * double(t.ksOf.size() - 1);
    const size_t i = std::min(size_t(pos), t.ksOf.size() - 2);
    return t.ksOf[i] + (t.ksOf[i + 1] - t.ksOf[i]) * (pos - double(i));
}

float srgbOf(double linear)
{
    const Tables &t = tables();
    const double pos = std::clamp(linear, 0.0, 1.0) * double(t.srgbOf.size() - 1);
    const size_t i = std::min(size_t(pos), t.srgbOf.size() - 2);
    return float(t.srgbOf[i] + (t.srgbOf[i + 1] - t.srgbOf[i]) * (pos - double(i)));
}

} // namespace

void PigmentMix::mixStraight(const float a[3], const float b[3], double t, float out[3])
{
    t = std::clamp(t, 0.0, 1.0);
    for (int k = 0; k < 3; ++k) {
        const double mixed = (1.0 - t) * ksOf(a[k]) + t * ksOf(b[k]);
        out[k] = srgbOf(reflectance(mixed));
    }
}

void PigmentMix::mix(float *c, const float *target, float f, bool pigment)
{
    if (!pigment) {
        for (int k = 0; k < 4; ++k)
            c[k] += (target[k] - c[k]) * f;
        return;
    }
    f = std::clamp(f, 0.0f, 1.0f);
    const float alpha = c[3] + (target[3] - c[3]) * f;
    const float wc = c[3] * (1.0f - f), wt = target[3] * f;
    if (wc + wt <= 1e-4f || alpha <= 1e-4f) {
        for (int k = 0; k < 4; ++k)
            c[k] += (target[k] - c[k]) * f;
        return;
    }
    // Straight colours of both (a transparent one gets weight 0 and is not used).
    float a[3] = {0, 0, 0}, b[3] = {0, 0, 0};
    for (int k = 0; k < 3; ++k) {
        a[k] = c[3] > 1e-4f ? std::clamp(c[k] * 255.0f / c[3], 0.0f, 255.0f) : 0.0f;
        b[k] = target[3] > 1e-4f ? std::clamp(target[k] * 255.0f / target[3], 0.0f, 255.0f) : 0.0f;
    }
    float out[3];
    if (wc <= 1e-4f)
        std::copy(b, b + 3, out);
    else if (wt <= 1e-4f)
        std::copy(a, a + 3, out);
    else
        mixStraight(a, b, wt / (wc + wt), out);
    for (int k = 0; k < 3; ++k)
        c[k] = out[k] * alpha / 255.0f;
    c[3] = alpha;
}

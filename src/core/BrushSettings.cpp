#include "BrushSettings.h"

#include <QJsonValue>
#include <algorithm>
#include <cmath>

namespace {

double clampOr(double v, double lo, double hi, double fallback)
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

const char *blendName(BrushBlend b)
{
    switch (b) {
    case BrushBlend::Erase: return "erase";
    case BrushBlend::Smudge: return "smudge";
    default: return "paint";
    }
}

QJsonObject mappingJson(const PressureMapping &m)
{
    return QJsonObject{{QStringLiteral("enabled"), m.enabled}, {QStringLiteral("minimum"), m.minimum},
                       {QStringLiteral("gamma"), m.gamma}};
}

// Small typed readers: a present key of the wrong type is an error; a missing key keeps *out.
struct Reader
{
    const QJsonObject &json;
    QString *error;
    bool ok = true;

    bool fail(const QString &key, const char *expected)
    {
        if (ok && error)
            *error = QStringLiteral("Brush setting \"%1\" must be %2.").arg(key, QLatin1String(expected));
        ok = false;
        return false;
    }
    void number(const char *key, double *out)
    {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        if (!v.isDouble())
            fail(QLatin1String(key), "a number");
        else
            *out = v.toDouble();
    }
    void integer(const char *key, int *out)
    {
        double d = *out;
        number(key, &d);
        if (std::isfinite(d))
            *out = int(std::lround(std::clamp(d, -1e6, 1e6)));
    }
    void boolean(const char *key, bool *out)
    {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        if (!v.isBool())
            fail(QLatin1String(key), "true or false");
        else
            *out = v.toBool();
    }
    void string(const char *key, QString *out)
    {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        if (!v.isString())
            fail(QLatin1String(key), "text");
        else
            *out = v.toString();
    }
    void mapping(const char *key, PressureMapping *out)
    {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        if (!v.isObject()) {
            fail(QLatin1String(key), "an object");
            return;
        }
        Reader inner{v.toObject(), error};
        inner.boolean("enabled", &out->enabled);
        inner.number("minimum", &out->minimum);
        inner.number("gamma", &out->gamma);
        if (!inner.ok)
            ok = false;
    }
    template <typename E>
    void choice(const char *key, E *out, std::initializer_list<std::pair<const char *, E>> names)
    {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isUndefined())
            return;
        for (const auto &[name, value] : names) {
            if (v.toString() == QLatin1String(name)) {
                *out = value;
                return;
            }
        }
        fail(QLatin1String(key), "a known option");
    }
};

} // namespace

double PressureMapping::apply(double pressure) const
{
    if (!enabled)
        return 1.0;
    const double p = std::isfinite(pressure) ? std::clamp(pressure, 0.0, 1.0) : 1.0;
    const double min = std::clamp(minimum, 0.0, 1.0);
    const double g = std::clamp(gamma, 1.0 / 3.0, 3.0);
    return min + (1.0 - min) * std::pow(p, g);
}

bool PressureMapping::operator==(const PressureMapping &o) const
{
    return enabled == o.enabled && minimum == o.minimum && gamma == o.gamma;
}

bool BrushSettings::operator==(const BrushSettings &o) const
{
    return name == o.name && blend == o.blend && tip == o.tip && size == o.size && hardness == o.hardness
        && roundness == o.roundness && angle == o.angle && followDirection == o.followDirection
        && angleJitter == o.angleJitter && spacing == o.spacing && opacity == o.opacity && flow == o.flow
        && accumulation == o.accumulation && pressureSize == o.pressureSize && pressureOpacity == o.pressureOpacity
        && pressureFlow == o.pressureFlow && tiltSize == o.tiltSize && tiltOpacity == o.tiltOpacity
        && texture == o.texture && textureScale == o.textureScale && textureStrength == o.textureStrength
        && textureMode == o.textureMode && airbrush == o.airbrush && airbrushRate == o.airbrushRate
        && smudgeStrength == o.smudgeStrength && smudgePickup == o.smudgePickup && smoothing == o.smoothing;
}

BrushSettings BrushSettings::sanitized() const
{
    BrushSettings s = *this;
    const BrushSettings d;
    s.name = name.trimmed().left(64);
    s.size = clampOr(size, MinSize, MaxSize, d.size);
    s.hardness = clampOr(hardness, 0.0, 1.0, d.hardness);
    s.roundness = clampOr(roundness, 0.05, 1.0, d.roundness);
    s.angle = clampOr(angle, -360.0, 360.0, d.angle);
    s.angleJitter = clampOr(angleJitter, 0.0, 180.0, 0.0);
    s.spacing = clampOr(spacing, 0.02, 2.0, d.spacing);
    s.opacity = clampOr(opacity, 0.01, 1.0, d.opacity);
    s.flow = clampOr(flow, 0.01, 1.0, d.flow);
    for (PressureMapping *m : {&s.pressureSize, &s.pressureOpacity, &s.pressureFlow}) {
        m->minimum = clampOr(m->minimum, 0.0, 1.0, 0.0);
        m->gamma = clampOr(m->gamma, 1.0 / 3.0, 3.0, 1.0);
    }
    s.tiltSize = clampOr(tiltSize, 0.25, 4.0, 1.0);
    s.tiltOpacity = clampOr(tiltOpacity, 0.0, 1.0, 1.0);
    s.textureScale = clampOr(textureScale, 0.1, 8.0, 1.0);
    s.textureStrength = clampOr(textureStrength, 0.0, 1.0, 0.0);
    s.airbrushRate = clampOr(airbrushRate, 1.0, 200.0, d.airbrushRate);
    s.smudgeStrength = clampOr(smudgeStrength, 0.0, 1.0, d.smudgeStrength);
    s.smudgePickup = clampOr(smudgePickup, 0.0, 1.0, d.smudgePickup);
    s.smoothing = std::clamp(smoothing, 0, 100);
    return s;
}

QJsonObject BrushSettings::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("blend")] = QLatin1String(blendName(blend));
    o[QStringLiteral("tip")] = tip;
    o[QStringLiteral("size")] = size;
    o[QStringLiteral("hardness")] = hardness;
    o[QStringLiteral("roundness")] = roundness;
    o[QStringLiteral("angle")] = angle;
    o[QStringLiteral("followDirection")] = followDirection;
    o[QStringLiteral("angleJitter")] = angleJitter;
    o[QStringLiteral("spacing")] = spacing;
    o[QStringLiteral("opacity")] = opacity;
    o[QStringLiteral("flow")] = flow;
    o[QStringLiteral("accumulation")] =
        accumulation == Accumulation::BuildUp ? QStringLiteral("buildUp") : QStringLiteral("uniform");
    o[QStringLiteral("pressureSize")] = mappingJson(pressureSize);
    o[QStringLiteral("pressureOpacity")] = mappingJson(pressureOpacity);
    o[QStringLiteral("pressureFlow")] = mappingJson(pressureFlow);
    o[QStringLiteral("tiltSize")] = tiltSize;
    o[QStringLiteral("tiltOpacity")] = tiltOpacity;
    o[QStringLiteral("texture")] = texture;
    o[QStringLiteral("textureScale")] = textureScale;
    o[QStringLiteral("textureStrength")] = textureStrength;
    o[QStringLiteral("textureMode")] =
        textureMode == TextureMode::Subtract ? QStringLiteral("subtract") : QStringLiteral("multiply");
    o[QStringLiteral("airbrush")] = airbrush;
    o[QStringLiteral("airbrushRate")] = airbrushRate;
    o[QStringLiteral("smudgeStrength")] = smudgeStrength;
    o[QStringLiteral("smudgePickup")] = smudgePickup;
    o[QStringLiteral("smoothing")] = smoothing;
    return o;
}

bool BrushSettings::fromJson(const QJsonObject &json, BrushSettings *out, QString *error)
{
    BrushSettings s;
    Reader r{json, error};
    r.string("name", &s.name);
    r.choice("blend", &s.blend, {{"paint", BrushBlend::Paint}, {"erase", BrushBlend::Erase}, {"smudge", BrushBlend::Smudge}});
    r.string("tip", &s.tip);
    r.number("size", &s.size);
    r.number("hardness", &s.hardness);
    r.number("roundness", &s.roundness);
    r.number("angle", &s.angle);
    r.boolean("followDirection", &s.followDirection);
    r.number("angleJitter", &s.angleJitter);
    r.number("spacing", &s.spacing);
    r.number("opacity", &s.opacity);
    r.number("flow", &s.flow);
    r.choice("accumulation", &s.accumulation, {{"uniform", Accumulation::Uniform}, {"buildUp", Accumulation::BuildUp}});
    r.mapping("pressureSize", &s.pressureSize);
    r.mapping("pressureOpacity", &s.pressureOpacity);
    r.mapping("pressureFlow", &s.pressureFlow);
    r.number("tiltSize", &s.tiltSize);
    r.number("tiltOpacity", &s.tiltOpacity);
    r.string("texture", &s.texture);
    r.number("textureScale", &s.textureScale);
    r.number("textureStrength", &s.textureStrength);
    r.choice("textureMode", &s.textureMode, {{"multiply", TextureMode::Multiply}, {"subtract", TextureMode::Subtract}});
    r.boolean("airbrush", &s.airbrush);
    r.number("airbrushRate", &s.airbrushRate);
    r.number("smudgeStrength", &s.smudgeStrength);
    r.number("smudgePickup", &s.smudgePickup);
    r.integer("smoothing", &s.smoothing);
    if (!r.ok)
        return false;
    *out = s.sanitized();
    return true;
}

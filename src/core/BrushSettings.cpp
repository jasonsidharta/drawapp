#include "BrushSettings.h"

#include "BrushProperties.h"
#include "BrushTips.h"

#include <QJsonValue>
#include <algorithm>
#include <cmath>

namespace {

double clampOr(double v, double lo, double hi, double fallback)
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

bool isDynamicsId(const QString &id)
{
    return id.startsWith(QLatin1String("dynamics."));
}

QString dynamicsKey(const QString &id)
{
    return id.mid(9);
}

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

bool BrushSettings::hasColorDynamics() const
{
    return hueJitter > 0 || saturationJitter > 0 || lightnessJitter > 0
        || dynamicsFor(DynamicTarget::Hue).isActive() || dynamicsFor(DynamicTarget::Saturation).isActive()
        || dynamicsFor(DynamicTarget::Lightness).isActive();
}

bool BrushSettings::operator==(const BrushSettings &o) const
{
    if (name != o.name || extra != o.extra || dynamics != o.dynamics || tipImage != o.tipImage)
        return false;
    for (const BrushProperty &p : BrushProperties::all()) {
        if (!p.implemented || p.type == PropertyType::Dynamics || p.type == PropertyType::Image)
            continue;
        if (p.get(*this) != p.get(o))
            return false;
    }
    return true;
}

BrushSettings BrushSettings::sanitized() const
{
    BrushSettings s = *this;
    const BrushSettings d;
    s.name = name.trimmed().left(64);
    for (const BrushProperty &p : BrushProperties::all()) {
        if (!p.implemented)
            continue;
        switch (p.type) {
        case PropertyType::Number:
            p.set(s, clampOr(p.get(s).toDouble(), p.minimum, p.maximum, p.get(d).toDouble()));
            break;
        case PropertyType::Integer:
            p.set(s, std::clamp(p.get(s).toInt(), int(p.minimum), int(p.maximum)));
            break;
        default:
            break;
        }
    }
    for (PressureMapping *m : {&s.pressureSize, &s.pressureOpacity, &s.pressureFlow, &s.pressureDepth}) {
        m->minimum = clampOr(m->minimum, 0.0, 1.0, 0.0);
        m->gamma = clampOr(m->gamma, 1.0 / 3.0, 3.0, 1.0);
    }
    for (ParameterDynamics &dyn : s.dynamics)
        dyn = dyn.sanitized();
    if (!s.tipImage.isNull()
        && (s.tipImage.format() != QImage::Format_Grayscale8 || s.tipImage.width() != s.tipImage.height()
            || s.tipImage.width() > MaxTipImageSize))
        s.tipImage = BrushProperties::tipFromImage(s.tipImage);
    return s;
}

QJsonObject BrushSettings::toJson() const
{
    QJsonObject o;
    QJsonObject dyn;
    o[QStringLiteral("name")] = name;
    for (const BrushProperty &p : BrushProperties::all()) {
        if (!p.implemented)
            continue;
        if (p.type == PropertyType::Dynamics) {
            DynamicTarget t;
            DynamicTargets::fromId(dynamicsKey(p.id), &t);
            if (dynamicsFor(t) != ParameterDynamics()) // only what differs from "off"
                dyn[dynamicsKey(p.id)] = dynamicsFor(t).toJson();
            continue;
        }
        if (p.type == PropertyType::Image) {
            if (!tipImage.isNull())
                o[p.id] = BrushProperties::encodeImage(tipImage);
            continue;
        }
        const QVariant v = p.get(*this);
        if (p.since >= 11 && v == p.defaultValue())
            continue; // milestone 11 keys only when used (BrushProperty::since)
        switch (p.type) {
        case PropertyType::Number: o[p.id] = v.toDouble(); break;
        case PropertyType::Integer: o[p.id] = v.toInt(); break;
        case PropertyType::Bool: o[p.id] = v.toBool(); break;
        case PropertyType::Pressure: o[p.id] = QJsonObject::fromVariantMap(v.toMap()); break;
        default: o[p.id] = v.toString(); break;
        }
    }
    // Keys from a newer version go back out unchanged.
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        if (it.key() == QLatin1String("dynamics")) {
            const QJsonObject unknown = it.value().toObject();
            for (auto u = unknown.begin(); u != unknown.end(); ++u)
                if (!dyn.contains(u.key()))
                    dyn[u.key()] = u.value();
        } else if (!o.contains(it.key())) {
            o[it.key()] = it.value();
        }
    }
    if (!dyn.isEmpty())
        o[QStringLiteral("dynamics")] = dyn;
    return o;
}

bool BrushSettings::fromJson(const QJsonObject &json, BrushSettings *out, QString *error, QStringList *warnings)
{
    BrushSettings s;
    auto fail = [&](const QString &key, const char *expected) {
        if (error)
            *error = QStringLiteral("Brush setting \"%1\" must be %2.").arg(key, QLatin1String(expected));
        return false;
    };
    auto warn = [&](const QString &message) {
        if (warnings)
            warnings->append(message);
    };

    const QJsonValue nameValue = json.value(QStringLiteral("name"));
    if (!nameValue.isUndefined()) {
        if (!nameValue.isString())
            return fail(QStringLiteral("name"), "text");
        s.name = nameValue.toString();
    }
    const QJsonValue dynValue = json.value(QStringLiteral("dynamics"));
    if (!dynValue.isUndefined() && !dynValue.isObject())
        return fail(QStringLiteral("dynamics"), "an object");
    const QJsonObject dynJson = dynValue.toObject();

    QStringList known{QStringLiteral("name"), QStringLiteral("dynamics")};
    QStringList knownDynamics;
    for (const BrushProperty &p : BrushProperties::all()) {
        if (!p.implemented)
            continue;
        const bool nested = isDynamicsId(p.id);
        (nested ? knownDynamics : known) << (nested ? dynamicsKey(p.id) : p.id);
        const QJsonValue v = nested ? dynJson.value(dynamicsKey(p.id)) : json.value(p.id);
        if (v.isUndefined())
            continue;
        switch (p.type) {
        case PropertyType::Number:
        case PropertyType::Integer:
            if (!v.isDouble())
                return fail(p.id, "a number");
            p.set(s, v.toDouble());
            break;
        case PropertyType::Bool:
            if (!v.isBool())
                return fail(p.id, "true or false");
            p.set(s, v.toBool());
            break;
        case PropertyType::Choice:
            if (!v.isString())
                return fail(p.id, "text");
            if (p.id == QLatin1String("tip") || p.id == QLatin1String("texture")) {
                // Resource ids: an unknown one (newer version, removed asset) is kept and drawn as
                // round tip / no texture, never refused.
                if (!v.toString().isEmpty() && !p.choices.contains(v.toString()))
                    warn(QStringLiteral("Unknown %1 \"%2\": drawn as %3.")
                             .arg(p.id, v.toString(),
                                  p.id == QLatin1String("tip") ? QStringLiteral("a round tip") : QStringLiteral("no texture")));
                p.set(s, v.toString());
            } else {
                if (!p.choices.contains(v.toString()))
                    return fail(p.id, "a known option");
                p.set(s, v.toString());
            }
            break;
        case PropertyType::Text:
            if (!v.isString())
                return fail(p.id, "text");
            p.set(s, v.toString());
            break;
        case PropertyType::Image: {
            if (!v.isString())
                return fail(p.id, "text");
            bool ok = false;
            const QImage image = BrushProperties::decodeImage(v.toString(), &ok);
            if (!ok)
                warn(QStringLiteral("The imported tip image could not be read; the tip shape is used instead."));
            else if (!image.isNull())
                s.tipImage = image.convertToFormat(QImage::Format_Grayscale8); // sanitized() squares and bounds it
            break;
        }
        case PropertyType::Pressure: {
            if (!v.isObject())
                return fail(p.id, "an object");
            const QJsonObject m = v.toObject();
            const QJsonValue en = m.value(QStringLiteral("enabled")), mi = m.value(QStringLiteral("minimum")),
                             ga = m.value(QStringLiteral("gamma"));
            if ((!en.isUndefined() && !en.isBool()) || (!mi.isUndefined() && !mi.isDouble())
                || (!ga.isUndefined() && !ga.isDouble()))
                return fail(p.id, "an object of enabled, minimum and gamma");
            p.set(s, m.toVariantMap());
            break;
        }
        case PropertyType::Dynamics: {
            if (!v.isObject())
                return fail(p.id, "an object");
            ParameterDynamics d;
            QString why;
            if (!ParameterDynamics::fromJson(v.toObject(), &d, &why)) {
                if (error)
                    *error = QStringLiteral("Brush setting \"%1\": %2").arg(p.id, why);
                return false;
            }
            DynamicTarget t;
            DynamicTargets::fromId(dynamicsKey(p.id), &t);
            s.dynamicsFor(t) = d;
            break;
        }
        }
    }
    for (auto it = json.begin(); it != json.end(); ++it)
        if (!known.contains(it.key()))
            s.extra[it.key()] = it.value();
    QJsonObject unknownDynamics;
    for (auto it = dynJson.begin(); it != dynJson.end(); ++it)
        if (!knownDynamics.contains(it.key()))
            unknownDynamics[it.key()] = it.value();
    if (!unknownDynamics.isEmpty())
        s.extra[QStringLiteral("dynamics")] = unknownDynamics;
    *out = s.sanitized();
    return true;
}

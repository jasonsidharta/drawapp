#include "BrushDynamics.h"

#include "BrushTips.h"

#include <QJsonValue>
#include <algorithm>
#include <cmath>

namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr double FullTiltDegrees = 60.0;
constexpr double MinPointGap = 0.005;

struct SensorInfo
{
    const char *id;
    const char *label;
    const char *description;
    double defaultRange; // 0 = no range
    double minRange;
    double maxRange;
    const char *unit;
};

const SensorInfo sensorInfo[SensorCount] = {
    {"pressure", "Pressure", "Pen pressure. Only pens that report pressure; mouse and touch give no reading.", 0, 0, 0, ""},
    {"tiltX", "Tilt X", "Pen tilt left (0) to right (1), −60° to 60°; upright is 0.5.", 0, 0, 0, ""},
    {"tiltY", "Tilt Y", "Pen tilt away (0) to towards you (1), −60° to 60°; upright is 0.5.", 0, 0, 0, ""},
    {"tiltMagnitude", "Tilt amount", "How far the pen leans: upright 0, 60° or more 1.", 0, 0, 0, ""},
    {"tiltDirection", "Tilt direction", "Which way the pen leans (azimuth), 0° to 360°.", 0, 0, 0, ""},
    {"direction", "Stroke direction", "Direction of travel, 0° (right) to 360°, clockwise on screen.", 0, 0, 0, ""},
    {"speed", "Speed", "How fast the stroke moves, up to the range (px per ms). Needs event times.", 2.0, 0.01, 50.0, "px/ms"},
    {"acceleration", "Acceleration", "Speeding up (towards 1) or slowing down (towards 0); 0.5 is steady.", 0.05, 0.0005, 10.0, "px/ms²"},
    {"distance", "Distance", "Length drawn so far, repeating every range pixels.", 200.0, 1.0, 10000.0, "px"},
    {"fade", "Fade", "Length drawn so far, from 0 at the start to 1 after range pixels.", 300.0, 1.0, 10000.0, "px"},
    {"time", "Time", "Time since the stroke began, 0 to 1 over range milliseconds.", 1000.0, 10.0, 60000.0, "ms"},
    {"random", "Random", "A different value for every dab (repeatable for the same stroke).", 0, 0, 0, ""},
    {"strokeRandom", "Random per stroke", "One value for the whole stroke.", 0, 0, 0, ""},
};

const char *const targetIds[DynamicTargetCount] = {"size", "opacity", "flow", "spacing", "rotation", "roundness",
                                                    "scatter", "textureStrength", "hue", "saturation", "lightness",
                                                    "smudgeStrength", "colorRate"};
const char *const targetLabels[DynamicTargetCount] = {"Size", "Opacity", "Flow", "Spacing", "Rotation", "Roundness",
                                                      "Scatter", "Texture strength", "Hue", "Saturation", "Lightness",
                                                      "Smudge strength", "Colour rate"};
const char *const combineIds[] = {"multiply", "add", "max", "min", "difference"};

double clamp01(double v)
{
    return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
}

} // namespace

// --- sensors ---

QString Sensors::id(Sensor s)
{
    return QLatin1String(sensorInfo[int(s)].id);
}

bool Sensors::fromId(const QString &id, Sensor *out)
{
    for (int i = 0; i < SensorCount; ++i)
        if (id == QLatin1String(sensorInfo[i].id)) {
            *out = Sensor(i);
            return true;
        }
    return false;
}

QString Sensors::label(Sensor s)
{
    return QLatin1String(sensorInfo[int(s)].label);
}

QString Sensors::description(Sensor s)
{
    return QString::fromUtf8(sensorInfo[int(s)].description);
}

bool Sensors::usesRange(Sensor s)
{
    return sensorInfo[int(s)].defaultRange > 0;
}

double Sensors::defaultRange(Sensor s)
{
    return sensorInfo[int(s)].defaultRange;
}

double Sensors::maxRange(Sensor s)
{
    return sensorInfo[int(s)].maxRange;
}

QString Sensors::rangeUnit(Sensor s)
{
    return QString::fromUtf8(sensorInfo[int(s)].unit);
}

double SensorReadings::normalized(Sensor s, double range, quint64 salt) const
{
    const double v = raw[size_t(s)];
    switch (s) {
    case Sensor::Pressure:
        return clamp01(v);
    case Sensor::TiltX:
    case Sensor::TiltY:
        return clamp01((v / FullTiltDegrees + 1.0) / 2.0);
    case Sensor::TiltMagnitude:
        return clamp01(v / FullTiltDegrees);
    case Sensor::TiltDirection:
    case Sensor::Direction: {
        if (!std::isfinite(v))
            return 0.0;
        const double turns = v / (2.0 * Pi);
        return clamp01(turns - std::floor(turns));
    }
    case Sensor::Speed:
    case Sensor::Fade:
    case Sensor::Time:
        return clamp01(v / range);
    case Sensor::Acceleration:
        return clamp01(0.5 + v / (2.0 * range));
    case Sensor::Distance: {
        if (!std::isfinite(v) || v <= 0)
            return 0.0;
        const double periods = v / range;
        return clamp01(periods - std::floor(periods));
    }
    case Sensor::Random:
        return BrushTips::hash01(qint64(seed), dabIndex, salt);
    case Sensor::StrokeRandom:
        return BrushTips::hash01(qint64(seed), -1, salt);
    case Sensor::Count:
        break;
    }
    return 0.0;
}

// --- curves ---

SensorCurve::SensorCurve()
    : m_points{{0.0, 0.0}, {1.0, 1.0}}
{
}

SensorCurve::SensorCurve(std::vector<QPointF> points)
    : m_points(std::move(points))
{
    sanitize();
}

SensorCurve SensorCurve::linear()
{
    return SensorCurve();
}

SensorCurve SensorCurve::inverted()
{
    return SensorCurve({{0.0, 1.0}, {1.0, 0.0}});
}

SensorCurve SensorCurve::threshold(double t)
{
    const double at = std::clamp(std::isfinite(t) ? t : 0.5, 0.02, 1.0);
    return SensorCurve({{0.0, 0.0}, {at - 0.02, 0.0}, {at, 1.0}, {1.0, 1.0}});
}

SensorCurve SensorCurve::exponential(double exponent)
{
    const double e = std::clamp(std::isfinite(exponent) ? exponent : 1.0, 0.1, 10.0);
    std::vector<QPointF> p;
    for (int i = 0; i <= 8; ++i) {
        const double x = i / 8.0;
        p.emplace_back(x, std::pow(x, e));
    }
    return SensorCurve(std::move(p));
}

SensorCurve SensorCurve::clamped(double lo, double hi)
{
    double a = clamp01(lo), b = clamp01(hi);
    if (a > b)
        std::swap(a, b);
    std::vector<QPointF> p{{0.0, a}};
    if (a > MinPointGap)
        p.emplace_back(a, a);
    if (b < 1.0 - MinPointGap && b - a > MinPointGap)
        p.emplace_back(b, b);
    p.emplace_back(1.0, b);
    return SensorCurve(std::move(p));
}

void SensorCurve::sanitize()
{
    std::vector<QPointF> p;
    for (const QPointF &q : m_points)
        if (std::isfinite(q.x()) && std::isfinite(q.y()))
            p.emplace_back(std::clamp(q.x(), 0.0, 1.0), std::clamp(q.y(), 0.0, 1.0));
    std::stable_sort(p.begin(), p.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    std::vector<QPointF> merged;
    for (const QPointF &q : p) {
        if (!merged.empty() && q.x() - merged.back().x() < MinPointGap)
            merged.back() = q; // the later point wins
        else
            merged.push_back(q);
    }
    if (merged.size() > size_t(MaxPoints))
        merged.resize(size_t(MaxPoints));
    if (merged.size() < 2)
        merged = {{0.0, 0.0}, {1.0, 1.0}};
    m_points = std::move(merged);
}

double SensorCurve::value(double x) const
{
    const std::vector<QPointF> &p = m_points;
    const size_t n = p.size();
    if (!std::isfinite(x))
        x = 0.0;
    if (x <= p.front().x())
        return p.front().y();
    if (x >= p.back().x())
        return p.back().y();
    size_t i = 0;
    while (i + 2 < n && x > p[i + 1].x())
        ++i;
    // Fritsch–Carlson slopes: secants at the ends, weighted harmonic means inside, 0 at extrema.
    auto secant = [&](size_t k) { return (p[k + 1].y() - p[k].y()) / (p[k + 1].x() - p[k].x()); };
    auto slope = [&](size_t k) -> double {
        if (k == 0)
            return secant(0);
        if (k == n - 1)
            return secant(n - 2);
        const double d0 = secant(k - 1), d1 = secant(k);
        if (d0 * d1 <= 0)
            return 0.0;
        const double h0 = p[k].x() - p[k - 1].x(), h1 = p[k + 1].x() - p[k].x();
        const double w1 = 2 * h1 + h0, w2 = h1 + 2 * h0;
        return (w1 + w2) / (w1 / d0 + w2 / d1);
    };
    const double h = p[i + 1].x() - p[i].x();
    const double t = (x - p[i].x()) / h;
    const double t2 = t * t, t3 = t2 * t;
    const double y = (2 * t3 - 3 * t2 + 1) * p[i].y() + (t3 - 2 * t2 + t) * h * slope(i)
                   + (-2 * t3 + 3 * t2) * p[i + 1].y() + (t3 - t2) * h * slope(i + 1);
    return clamp01(y);
}

void SensorCurve::tabulate(float *table, int size) const
{
    for (int i = 0; i < size; ++i)
        table[i] = float(value(double(i) / (size - 1)));
}

QJsonArray SensorCurve::toJson() const
{
    QJsonArray a;
    for (const QPointF &q : m_points)
        a.append(QJsonArray{q.x(), q.y()});
    return a;
}

bool SensorCurve::fromJson(const QJsonArray &json, SensorCurve *out)
{
    if (json.size() < 2 || json.size() > 64)
        return false;
    std::vector<QPointF> p;
    for (const QJsonValue &v : json) {
        const QJsonArray pair = v.toArray();
        if (!v.isArray() || pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble())
            return false;
        p.emplace_back(pair[0].toDouble(), pair[1].toDouble());
    }
    *out = SensorCurve(std::move(p));
    return true;
}

// --- dynamics ---

double SensorMapping::effectiveRange() const
{
    if (!Sensors::usesRange(sensor))
        return 0.0;
    const SensorInfo &info = sensorInfo[int(sensor)];
    return (std::isfinite(range) && range > 0) ? std::clamp(range, info.minRange, info.maxRange) : info.defaultRange;
}

ParameterDynamics ParameterDynamics::sanitized() const
{
    ParameterDynamics d = *this;
    d.minimum = clamp01(minimum);
    if (d.mappings.size() > size_t(MaxMappings))
        d.mappings.resize(size_t(MaxMappings));
    for (SensorMapping &m : d.mappings)
        m.range = Sensors::usesRange(m.sensor) ? m.effectiveRange() : 0.0;
    return d;
}

QJsonObject ParameterDynamics::toJson() const
{
    QJsonArray maps;
    for (const SensorMapping &m : mappings) {
        QJsonObject o{{QStringLiteral("sensor"), Sensors::id(m.sensor)}, {QStringLiteral("curve"), m.curve.toJson()}};
        if (Sensors::usesRange(m.sensor))
            o[QStringLiteral("range")] = m.effectiveRange();
        maps.append(o);
    }
    return QJsonObject{{QStringLiteral("enabled"), enabled},
                       {QStringLiteral("minimum"), minimum},
                       {QStringLiteral("combine"), QLatin1String(combineIds[int(combine)])},
                       {QStringLiteral("sensors"), maps}};
}

bool ParameterDynamics::fromJson(const QJsonObject &json, ParameterDynamics *out, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    ParameterDynamics d;
    const QJsonValue enabled = json.value(QStringLiteral("enabled"));
    if (!enabled.isUndefined()) {
        if (!enabled.isBool())
            return fail(QStringLiteral("Dynamics \"enabled\" must be true or false."));
        d.enabled = enabled.toBool();
    }
    const QJsonValue minimum = json.value(QStringLiteral("minimum"));
    if (!minimum.isUndefined()) {
        if (!minimum.isDouble())
            return fail(QStringLiteral("Dynamics \"minimum\" must be a number."));
        d.minimum = minimum.toDouble();
    }
    const QJsonValue combine = json.value(QStringLiteral("combine"));
    if (!combine.isUndefined()) {
        bool found = false;
        for (int i = 0; i < 5; ++i)
            if (combine.toString() == QLatin1String(combineIds[i])) {
                d.combine = CombineMode(i);
                found = true;
            }
        if (!found)
            return fail(QStringLiteral("Unknown dynamics combination \"%1\".").arg(combine.toString()));
    }
    const QJsonValue sensors = json.value(QStringLiteral("sensors"));
    if (!sensors.isUndefined()) {
        if (!sensors.isArray())
            return fail(QStringLiteral("Dynamics \"sensors\" must be a list."));
        for (const QJsonValue &v : sensors.toArray()) {
            const QJsonObject o = v.toObject();
            if (!v.isObject())
                return fail(QStringLiteral("A dynamics sensor must be an object."));
            SensorMapping m;
            if (!Sensors::fromId(o.value(QStringLiteral("sensor")).toString(), &m.sensor))
                return fail(QStringLiteral("Unknown sensor \"%1\".").arg(o.value(QStringLiteral("sensor")).toString()));
            const QJsonValue curve = o.value(QStringLiteral("curve"));
            if (!curve.isUndefined() && !SensorCurve::fromJson(curve.toArray(), &m.curve))
                return fail(QStringLiteral("A sensor curve must be a list of 2 to 64 [x, y] points."));
            const QJsonValue range = o.value(QStringLiteral("range"));
            if (!range.isUndefined()) {
                if (!range.isDouble())
                    return fail(QStringLiteral("A sensor range must be a number."));
                m.range = range.toDouble();
            }
            d.mappings.push_back(m);
        }
    }
    *out = d.sanitized();
    return true;
}

QString DynamicTargets::id(DynamicTarget t)
{
    return QLatin1String(targetIds[int(t)]);
}

bool DynamicTargets::fromId(const QString &id, DynamicTarget *out)
{
    for (int i = 0; i < DynamicTargetCount; ++i)
        if (id == QLatin1String(targetIds[i])) {
            *out = DynamicTarget(i);
            return true;
        }
    return false;
}

QString DynamicTargets::label(DynamicTarget t)
{
    return QLatin1String(targetLabels[int(t)]);
}

CompiledDynamics::CompiledDynamics(const ParameterDynamics &dynamics, DynamicTarget target)
    : m_active(dynamics.isActive())
    , m_minimum(clamp01(dynamics.minimum))
    , m_combine(dynamics.combine)
    , m_salt(0xD1A0000ull + quint64(target) * 0x100u)
{
    if (!m_active)
        return;
    const ParameterDynamics d = dynamics.sanitized();
    for (const SensorMapping &m : d.mappings) {
        Mapping c;
        c.sensor = m.sensor;
        c.range = m.effectiveRange();
        m.curve.tabulate(c.table.data(), TableSize);
        m_mappings.push_back(c);
    }
}

bool CompiledDynamics::uses(Sensor sensor) const
{
    return m_active && std::any_of(m_mappings.begin(), m_mappings.end(), [&](const Mapping &m) { return m.sensor == sensor; });
}

bool CompiledDynamics::evaluate(const SensorReadings &r, double *value) const
{
    if (!m_active)
        return false;
    bool any = false;
    double combined = 0;
    for (size_t i = 0; i < m_mappings.size(); ++i) {
        const Mapping &m = m_mappings[i];
        const bool random = m.sensor == Sensor::Random || m.sensor == Sensor::StrokeRandom;
        if (!random && !r.available[size_t(m.sensor)])
            continue;
        const double x = r.normalized(m.sensor, m.range, m_salt + i);
        const double pos = x * (TableSize - 1);
        const int k = std::min(TableSize - 2, int(pos));
        const double v = m.table[size_t(k)] + (m.table[size_t(k) + 1] - m.table[size_t(k)]) * (pos - k);
        if (!any) {
            combined = v;
            any = true;
            continue;
        }
        switch (m_combine) {
        case CombineMode::Multiply: combined *= v; break;
        case CombineMode::Add: combined = std::min(1.0, combined + v); break;
        case CombineMode::Max: combined = std::max(combined, v); break;
        case CombineMode::Min: combined = std::min(combined, v); break;
        case CombineMode::Difference: combined = std::abs(combined - v); break;
        }
    }
    if (!any)
        return false;
    *value = clamp01(m_minimum + (1.0 - m_minimum) * clamp01(combined));
    return true;
}

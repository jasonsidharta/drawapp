#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <array>
#include <vector>

// The sensor and dynamics framework shared by every brush engine (research/milestone9-
// architecture-decisions.md, D3). A brush parameter (size, opacity, hue, ...) can follow any
// number of sensors, each through its own response curve; the readings combine by one rule and
// the result scales the parameter:
//
//   value = minimum + (1 − minimum) × combine(curve_i(sensor_i))      (0..1)
//
// What the value does depends on the parameter (DynamicTarget). Multiplying parameters (size,
// opacity, flow, spacing, roundness, scatter, texture strength, smudge strength) are multiplied
// by it; rotation adds value × 360°; hue, saturation and lightness shift the colour by
// (1 − value) × their amount, so a full reading keeps the chosen colour.
//
// Missing data is never invented: a sensor the input device does not report (pressure from a
// mouse, tilt from a pen without tilt, speed without event times) is left out of the
// combination, and a parameter none of whose sensors has data keeps its base value.

enum class Sensor {
    Pressure,      // pen pressure 0..1
    TiltX,         // −60°..60° → 0..1 (upright 0.5)
    TiltY,
    TiltMagnitude, // 0° (upright) .. 60° or more → 0..1
    TiltDirection, // pen azimuth, 0..360° → 0..1
    Direction,     // stroke direction, 0..360° → 0..1 (0 = to the right, clockwise on screen)
    Speed,         // px/ms, 0..range → 0..1
    Acceleration,  // px/ms², −range..range → 0..1 (0.5 = steady speed)
    Distance,      // stroke length in px, repeating every `range` px → 0..1
    Fade,          // stroke length in px, 0..range → 0..1, then stays 1
    Time,          // ms since the stroke began, 0..range → 0..1
    Random,        // a new value for every dab
    StrokeRandom,  // one value for the whole stroke
    Count
};

constexpr int SensorCount = int(Sensor::Count);

namespace Sensors {
QString id(Sensor sensor);                   // stable serialisation id ("pressure", "tiltX", ...)
bool fromId(const QString &id, Sensor *out); // false for an unknown id
QString label(Sensor sensor);
QString description(Sensor sensor);
// Sensors that need a range (speed, acceleration, distance, fade, time), its default and unit.
bool usesRange(Sensor sensor);
double defaultRange(Sensor sensor);
double maxRange(Sensor sensor);
QString rangeUnit(Sensor sensor);
}

// A response curve from a sensor reading (x) to a value (y), both 0..1: 2 to MaxPoints points
// with ascending x, joined by a monotone cubic (Fritsch–Carlson), so the curve never overshoots
// its points. Evaluated through a 256-entry table at render time (as Krita's floatTransfer(256)).
class SensorCurve
{
public:
    static constexpr int MaxPoints = 16;

    SensorCurve(); // linear: (0, 0) – (1, 1)
    explicit SensorCurve(std::vector<QPointF> points);

    static SensorCurve linear();
    static SensorCurve inverted();
    // 0 below t, 1 from t on (a steep step of width 0.02).
    static SensorCurve threshold(double t);
    // y = x^exponent, sampled at 9 points (exponent < 1 soft, > 1 firm).
    static SensorCurve exponential(double exponent);
    // y = x limited to lo..hi.
    static SensorCurve clamped(double lo, double hi);

    const std::vector<QPointF> &points() const { return m_points; }
    double value(double x) const;
    // Fills table[i] = value(i / (size − 1)).
    void tabulate(float *table, int size) const;

    QJsonArray toJson() const;
    // Accepts [[x, y], ...]; returns false (and leaves *out) for anything else. Points are
    // sanitised: clamped to 0..1, sorted, nearly equal x merged, at most MaxPoints kept.
    static bool fromJson(const QJsonArray &json, SensorCurve *out);

    bool operator==(const SensorCurve &other) const { return m_points == other.m_points; }
    bool operator!=(const SensorCurve &other) const { return !(*this == other); }

private:
    void sanitize();
    std::vector<QPointF> m_points;
};

// How the readings of several sensors combine (Krita's five curve modes).
enum class CombineMode {
    Multiply,   // product (all sensors must be high)
    Add,        // sum, limited to 1
    Max,
    Min,
    Difference, // |…|v1 − v2| − v3| …
};

struct SensorMapping
{
    Sensor sensor = Sensor::Pressure;
    SensorCurve curve;
    double range = 0; // for sensors that use one; 0 = the sensor's default

    double effectiveRange() const;
    bool operator==(const SensorMapping &o) const
    {
        return sensor == o.sensor && curve == o.curve && range == o.range;
    }
    bool operator!=(const SensorMapping &o) const { return !(*this == o); }
};

struct ParameterDynamics
{
    static constexpr int MaxMappings = 4;

    bool enabled = false;
    double minimum = 0.0;
    CombineMode combine = CombineMode::Multiply;
    std::vector<SensorMapping> mappings;

    bool isActive() const { return enabled && !mappings.empty(); }
    ParameterDynamics sanitized() const;
    QJsonObject toJson() const;
    // false with *error for a malformed object (wrong types, unknown sensor or combine names).
    static bool fromJson(const QJsonObject &json, ParameterDynamics *out, QString *error);

    bool operator==(const ParameterDynamics &o) const
    {
        return enabled == o.enabled && minimum == o.minimum && combine == o.combine && mappings == o.mappings;
    }
    bool operator!=(const ParameterDynamics &o) const { return !(*this == o); }
};

// The parameters that can follow sensors.
enum class DynamicTarget {
    Size,
    Opacity,
    Flow,
    Spacing,
    Rotation,
    Roundness,
    Scatter,
    TextureStrength,
    Hue,
    Saturation,
    Lightness,
    SmudgeStrength,
    Count
};

constexpr int DynamicTargetCount = int(DynamicTarget::Count);
using DynamicsSet = std::array<ParameterDynamics, DynamicTargetCount>;

namespace DynamicTargets {
QString id(DynamicTarget target); // "size", "opacity", ...
bool fromId(const QString &id, DynamicTarget *out);
QString label(DynamicTarget target);
}

// The sensor readings for one dab, in physical units; availability says which the device gave.
struct SensorReadings
{
    std::array<double, SensorCount> raw{};
    std::array<bool, SensorCount> available{};
    quint64 seed = 0;   // the stroke's random seed
    qint64 dabIndex = 0;

    void set(Sensor s, double value)
    {
        raw[size_t(s)] = value;
        available[size_t(s)] = true;
    }
    // The reading normalised to 0..1 (range for the sensors that use one); salt separates the
    // random streams of different parameters and mappings.
    double normalized(Sensor s, double range, quint64 salt) const;
};

// A ParameterDynamics prepared for evaluation per dab: curves as 256-entry tables.
class CompiledDynamics
{
public:
    CompiledDynamics() = default;
    CompiledDynamics(const ParameterDynamics &dynamics, DynamicTarget target);

    bool isActive() const { return m_active; }
    // True when the parameter follows a sensor of that kind.
    bool uses(Sensor sensor) const;
    // The value 0..1, or false when the dynamics are off or no sensor has data (the parameter
    // then keeps its base value).
    bool evaluate(const SensorReadings &readings, double *value) const;

private:
    static constexpr int TableSize = 256;
    struct Mapping
    {
        Sensor sensor;
        double range;
        std::array<float, TableSize> table;
    };
    bool m_active = false;
    double m_minimum = 0;
    CombineMode m_combine = CombineMode::Multiply;
    quint64 m_salt = 0;
    std::vector<Mapping> m_mappings;
};

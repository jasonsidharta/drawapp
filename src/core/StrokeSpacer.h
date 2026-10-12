#pragma once

#include <QPointF>
#include <functional>
#include <vector>

// One input sample in document coordinates, independent of the device that produced it.
struct StrokeSample
{
    QPointF pos;        // document pixels
    qreal pressure = 1; // 0..1, already passed through effectivePressure()
    qreal tiltX = 0;    // degrees, −90..90 (QTabletEvent::xTilt); meaningful only with hasTilt
    qreal tiltY = 0;
    bool hasTilt = false;
    double timeMs = -1; // event time in milliseconds; < 0 when unknown
    // False when the device reports no pressure (mouse, touch, pens without pressure): pressure is
    // then 1 and pressure-driven dynamics are left out rather than fed an invented value.
    bool hasPressure = true;

    // Filled in by StrokeEngine from the (smoothed) path, for the dynamics sensors:
    double distance = 0;      // stroke length up to here, px
    double speed = -1;        // px/ms, smoothed; < 0 when unknown (no event times yet)
    double acceleration = 0;  // px/ms², smoothed
    double direction = 0;     // travel direction, radians; meaningful only with hasDirection
    bool hasDirection = false;

    // Linear interpolation of every field (tilt only when both ends report it).
    static StrokeSample lerp(const StrokeSample &a, const StrokeSample &b, qreal t);
};

struct Dab
{
    QPointF center;      // document pixels
    qreal diameter;      // document pixels
    StrokeSample sample; // the input interpolated at this dab (pressure, tilt, time)
    qreal direction = 0; // travel direction at this dab, radians (0 = +x)
    qreal step = 0;      // distance to the next dab, px (0 when not placed by distance)
};

// How pen pressure maps to dab size: size = brush size × (minFraction + (1 − minFraction) × p^gamma).
// Input without pressure arrives as 1.0 (effectivePressure), so it always draws at full size.
struct PressureResponse
{
    static constexpr qreal MinGamma = 1.0 / 3.0;
    static constexpr qreal MaxGamma = 3.0;

    qreal minFraction = 0.1; // size at the lightest pressure, as a fraction of the brush size
    qreal gamma = 1.0;       // < 1: light pressure already gives a large size; > 1: needs firmer pressure

    // Firmness −1 (soft) .. 0 (linear) .. 1 (firm), as shown in the brush settings.
    static qreal gammaForFirmness(qreal firmness);
    qreal sizeFactor(qreal pressure) const;
};

// Turns a sequence of input samples into evenly spaced dabs so strokes stay continuous
// however sparse the input events are. The distance travelled since the last dab carries
// across samples, so splitting a segment into many small moves yields the same dabs.
class StrokeSpacer
{
public:
    // Spacing between dab centers as a fraction of the last dab's diameter.
    static constexpr qreal DefaultSpacing = 0.15;
    // Diameter at zero pressure as a fraction of the brush size (the default response).
    static constexpr qreal MinPressureFraction = 0.1;
    static constexpr qreal MinDiameter = 1.0;
    static constexpr qreal MinStep = 0.5;

    // The dab diameter for an input sample (clamped to MinDiameter by the spacer).
    using DiameterFunction = std::function<qreal(const StrokeSample &)>;
    // Optional: a factor on the spacing after a dab with this sample and diameter (spacing
    // dynamics and jitter). Called once per dab, after the diameter.
    using SpacingFunction = std::function<qreal(const StrokeSample &, qreal diameter)>;

    explicit StrokeSpacer(qreal brushSize, qreal spacing = DefaultSpacing, PressureResponse response = {});
    StrokeSpacer(DiameterFunction diameter, qreal spacing);
    void setSpacingFunction(SpacingFunction f) { m_spacingFactor = std::move(f); }

    qreal diameterFor(qreal pressure) const;
    qreal diameterFor(const StrokeSample &sample) const;
    // Fraction of the diameter between successive dabs, at least MinStep pixels.
    qreal stepFraction(qreal diameter) const;
    // The last sample reached, and the diameter of the last dab.
    const StrokeSample &last() const { return m_last; }
    qreal lastDiameter() const { return m_lastDabDiameter; }
    // A dab placed at last() by other means (elapsed time): distance spacing restarts from it.
    void restartDistance() { m_sinceLastDab = 0; }

    // Starts a stroke and emits its first dab.
    void begin(const StrokeSample &sample, std::vector<Dab> &out);
    // Continues the stroke to sample, emitting zero or more dabs.
    void moveTo(const StrokeSample &sample, std::vector<Dab> &out);

private:
    qreal stepAfter(const StrokeSample &sample, qreal diameter) const;

    DiameterFunction m_diameter;
    SpacingFunction m_spacingFactor;
    qreal m_spacing;
    qreal m_step = 1; // distance from the last dab to the next one
    StrokeSample m_last;
    qreal m_lastDabDiameter = 1;
    qreal m_sinceLastDab = 0;
};

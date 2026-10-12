#pragma once

#include <QPointF>
#include <vector>

struct StrokeSample
{
    QPointF pos;       // document pixels
    qreal pressure = 1; // 0..1, already passed through effectivePressure()
};

struct Dab
{
    QPointF center; // document pixels
    qreal diameter; // document pixels
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

    explicit StrokeSpacer(qreal brushSize, qreal spacing = DefaultSpacing, PressureResponse response = {});

    qreal diameterFor(qreal pressure) const;

    // Starts a stroke and emits its first dab.
    void begin(const StrokeSample &sample, std::vector<Dab> &out);
    // Continues the stroke to sample, emitting zero or more dabs.
    void moveTo(const StrokeSample &sample, std::vector<Dab> &out);

private:
    qreal m_brushSize;
    qreal m_spacing;
    PressureResponse m_response;
    StrokeSample m_last;
    qreal m_lastDabDiameter = 1;
    qreal m_sinceLastDab = 0;
};

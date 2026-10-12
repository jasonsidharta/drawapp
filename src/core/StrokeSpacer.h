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

// Turns a sequence of input samples into evenly spaced dabs so strokes stay continuous
// however sparse the input events are. The distance travelled since the last dab carries
// across samples, so splitting a segment into many small moves yields the same dabs.
class StrokeSpacer
{
public:
    // Spacing between dab centers as a fraction of the last dab's diameter.
    static constexpr qreal DefaultSpacing = 0.15;
    // Diameter at zero pressure as a fraction of the brush size.
    static constexpr qreal MinPressureFraction = 0.1;
    static constexpr qreal MinDiameter = 1.0;
    static constexpr qreal MinStep = 0.5;

    explicit StrokeSpacer(qreal brushSize, qreal spacing = DefaultSpacing);

    qreal diameterFor(qreal pressure) const;

    // Starts a stroke and emits its first dab.
    void begin(const StrokeSample &sample, std::vector<Dab> &out);
    // Continues the stroke to sample, emitting zero or more dabs.
    void moveTo(const StrokeSample &sample, std::vector<Dab> &out);

private:
    qreal m_brushSize;
    qreal m_spacing;
    StrokeSample m_last;
    qreal m_lastDabDiameter = 1;
    qreal m_sinceLastDab = 0;
};

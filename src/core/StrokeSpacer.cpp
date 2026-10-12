#include "StrokeSpacer.h"

#include <algorithm>
#include <cmath>

qreal PressureResponse::gammaForFirmness(qreal firmness)
{
    return std::pow(MaxGamma, std::clamp(firmness, -1.0, 1.0));
}

qreal PressureResponse::sizeFactor(qreal pressure) const
{
    const qreal p = std::clamp(pressure, 0.0, 1.0);
    const qreal min = std::clamp(minFraction, 0.0, 1.0);
    const qreal g = std::clamp(gamma, MinGamma, MaxGamma);
    return min + (1.0 - min) * std::pow(p, g);
}

StrokeSpacer::StrokeSpacer(qreal brushSize, qreal spacing, PressureResponse response)
    : m_brushSize(std::max(MinDiameter, brushSize))
    , m_spacing(std::max(0.01, spacing))
    , m_response(response)
{
}

qreal StrokeSpacer::diameterFor(qreal pressure) const
{
    return std::max(MinDiameter, m_brushSize * m_response.sizeFactor(pressure));
}

void StrokeSpacer::begin(const StrokeSample &sample, std::vector<Dab> &out)
{
    m_last = sample;
    m_lastDabDiameter = diameterFor(sample.pressure);
    m_sinceLastDab = 0;
    out.push_back({sample.pos, m_lastDabDiameter});
}

void StrokeSpacer::moveTo(const StrokeSample &sample, std::vector<Dab> &out)
{
    const QPointF delta = sample.pos - m_last.pos;
    const qreal length = std::hypot(delta.x(), delta.y());
    if (!(length > 0.0)) {
        m_last.pressure = sample.pressure;
        return;
    }

    // Invariant: m_sinceLastDab < step, because the step only changes when a dab resets it.
    qreal travelled = 0;
    for (;;) {
        const qreal step = std::max(MinStep, m_spacing * m_lastDabDiameter);
        const qreal next = travelled + (step - m_sinceLastDab);
        if (next > length) {
            m_sinceLastDab += length - travelled;
            break;
        }
        travelled = next;
        const qreal t = travelled / length;
        const qreal pressure = m_last.pressure + (sample.pressure - m_last.pressure) * t;
        m_lastDabDiameter = diameterFor(pressure);
        m_sinceLastDab = 0;
        out.push_back({m_last.pos + delta * t, m_lastDabDiameter});
    }
    m_last = sample;
}

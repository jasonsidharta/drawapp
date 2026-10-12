#include "StrokeSpacer.h"

#include <algorithm>
#include <cmath>

StrokeSample StrokeSample::lerp(const StrokeSample &a, const StrokeSample &b, qreal t)
{
    StrokeSample s;
    s.pos = a.pos + (b.pos - a.pos) * t;
    s.pressure = a.pressure + (b.pressure - a.pressure) * t;
    s.hasTilt = a.hasTilt || b.hasTilt;
    if (a.hasTilt && b.hasTilt) {
        s.tiltX = a.tiltX + (b.tiltX - a.tiltX) * t;
        s.tiltY = a.tiltY + (b.tiltY - a.tiltY) * t;
    } else if (s.hasTilt) {
        // Only one end reports tilt: hold that value rather than ramping from a fake zero.
        const StrokeSample &known = a.hasTilt ? a : b;
        s.tiltX = known.tiltX;
        s.tiltY = known.tiltY;
    }
    s.timeMs = (a.timeMs >= 0 && b.timeMs >= 0) ? a.timeMs + (b.timeMs - a.timeMs) * t : b.timeMs;
    return s;
}

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
    : StrokeSpacer(
          [size = std::max(MinDiameter, brushSize), response](const StrokeSample &s) {
              return size * response.sizeFactor(s.pressure);
          },
          spacing)
{
}

StrokeSpacer::StrokeSpacer(DiameterFunction diameter, qreal spacing)
    : m_diameter(std::move(diameter))
    , m_spacing(std::max(0.01, spacing))
{
}

qreal StrokeSpacer::diameterFor(qreal pressure) const
{
    StrokeSample s;
    s.pressure = pressure;
    return diameterFor(s);
}

qreal StrokeSpacer::diameterFor(const StrokeSample &sample) const
{
    const qreal d = m_diameter(sample);
    return std::isfinite(d) ? std::max(MinDiameter, d) : MinDiameter;
}

qreal StrokeSpacer::stepFraction(qreal diameter) const
{
    return std::max(MinStep, m_spacing * diameter) / std::max(MinDiameter, diameter);
}

void StrokeSpacer::begin(const StrokeSample &sample, std::vector<Dab> &out)
{
    m_last = sample;
    m_lastDabDiameter = diameterFor(sample);
    m_sinceLastDab = 0;
    out.push_back({sample.pos, m_lastDabDiameter, sample, 0});
}

void StrokeSpacer::moveTo(const StrokeSample &sample, std::vector<Dab> &out)
{
    const QPointF delta = sample.pos - m_last.pos;
    const qreal length = std::hypot(delta.x(), delta.y());
    if (!(length > 0.0)) {
        // No travel: take the new pressure, tilt and time, keep the position.
        const QPointF pos = m_last.pos;
        m_last = sample;
        m_last.pos = pos;
        return;
    }
    const qreal direction = std::atan2(delta.y(), delta.x());

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
        const StrokeSample at = StrokeSample::lerp(m_last, sample, travelled / length);
        m_lastDabDiameter = diameterFor(at);
        m_sinceLastDab = 0;
        out.push_back({at.pos, m_lastDabDiameter, at, direction});
    }
    m_last = sample;
}

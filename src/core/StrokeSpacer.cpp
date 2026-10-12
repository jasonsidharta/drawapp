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
    s.hasPressure = a.hasPressure && b.hasPressure;
    s.distance = a.distance + (b.distance - a.distance) * t;
    s.speed = (a.speed >= 0 && b.speed >= 0) ? a.speed + (b.speed - a.speed) * t : b.speed;
    s.acceleration = a.acceleration + (b.acceleration - a.acceleration) * t;
    s.hasDirection = b.hasDirection || a.hasDirection;
    s.direction = b.hasDirection ? b.direction : a.direction; // angles are not interpolated (wrap-around)
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

qreal StrokeSpacer::stepAfter(const StrokeSample &sample, qreal diameter) const
{
    qreal factor = 1.0;
    if (m_spacingFactor) {
        factor = m_spacingFactor(sample, diameter);
        if (!std::isfinite(factor) || factor <= 0)
            factor = 1.0;
    }
    return std::max(MinStep, m_spacing * factor * diameter);
}

qreal StrokeSpacer::stepFraction(qreal diameter) const
{
    return std::max(MinStep, m_spacing * diameter) / std::max(MinDiameter, diameter);
}

void StrokeSpacer::begin(const StrokeSample &sample, std::vector<Dab> &out)
{
    m_last = sample;
    m_lastDabDiameter = diameterFor(sample);
    m_step = stepAfter(sample, m_lastDabDiameter);
    m_sinceLastDab = 0;
    out.push_back({sample.pos, m_lastDabDiameter, sample, 0, m_step});
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

    // Invariant: m_sinceLastDab < m_step, because the step only changes when a dab resets it.
    qreal travelled = 0;
    for (;;) {
        const qreal next = travelled + (m_step - m_sinceLastDab);
        if (next > length) {
            m_sinceLastDab += length - travelled;
            break;
        }
        travelled = next;
        const StrokeSample at = StrokeSample::lerp(m_last, sample, travelled / length);
        m_lastDabDiameter = diameterFor(at);
        m_step = stepAfter(at, m_lastDabDiameter);
        m_sinceLastDab = 0;
        out.push_back({at.pos, m_lastDabDiameter, at, direction, m_step});
    }
    m_last = sample;
}

#include "StrokeSmoother.h"

#include <algorithm>
#include <cmath>

StrokeSmoother::StrokeSmoother(qreal radius)
    : m_radius(std::max(0.0, radius))
{
}

void StrokeSmoother::begin(const StrokeSample &sample, std::vector<StrokeSample> &out)
{
    m_pointer = sample;
    m_brush = sample;
    out.push_back(sample);
}

void StrokeSmoother::moveTo(const StrokeSample &sample, std::vector<StrokeSample> &out)
{
    if (m_radius <= 0.0) {
        m_pointer = m_brush = sample;
        out.push_back(sample);
        return;
    }
    const QPointF delta = sample.pos - m_pointer.pos;
    const qreal length = std::hypot(delta.x(), delta.y());
    const int steps = std::max(1, int(std::ceil(length / StepLength)));
    bool moved = false;
    for (int i = 1; i <= steps; ++i) {
        const qreal t = qreal(i) / steps;
        const QPointF pointer = m_pointer.pos + delta * t;
        const qreal pressure = m_pointer.pressure + (sample.pressure - m_pointer.pressure) * t;
        const QPointF toPointer = pointer - m_brush.pos;
        const qreal distance = std::hypot(toPointer.x(), toPointer.y());
        // The brush carries the pressure of the pointer that pulls it.
        m_brush.pressure = pressure;
        if (distance > m_radius) {
            m_brush.pos = pointer - toPointer * (m_radius / distance);
            moved = true;
            // Emitting every step keeps curves round; the spacer turns them into evenly spaced dabs.
            out.push_back(m_brush);
        }
    }
    if (!moved && sample.pressure != m_pointer.pressure)
        out.push_back(m_brush); // pressure-only change while within the string
    m_pointer = sample;
}

void StrokeSmoother::finish(std::vector<StrokeSample> &out)
{
    if (m_brush.pos == m_pointer.pos)
        return;
    // Straight to where the pen was lifted, with the last pressure: no missing tail and no delay.
    m_brush = m_pointer;
    out.push_back(m_brush);
}

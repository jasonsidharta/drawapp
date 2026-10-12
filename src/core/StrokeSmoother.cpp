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
        const StrokeSample pointer = StrokeSample::lerp(m_pointer, sample, qreal(i) / steps);
        const QPointF toPointer = pointer.pos - m_brush.pos;
        const qreal distance = std::hypot(toPointer.x(), toPointer.y());
        // The brush carries the pressure, tilt and time of the pointer that pulls it.
        const QPointF brushPos = m_brush.pos;
        m_brush = pointer;
        m_brush.pos = brushPos;
        if (distance > m_radius) {
            m_brush.pos = pointer.pos - toPointer * (m_radius / distance);
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

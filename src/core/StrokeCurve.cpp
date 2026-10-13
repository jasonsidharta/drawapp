#include "StrokeCurve.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr double MinSegment = 1e-3; // px: closer points are the same point

double distance(const QPointF &a, const QPointF &b)
{
    return std::hypot(b.x() - a.x(), b.y() - a.y());
}

} // namespace

StrokeCurve::StrokeCurve(bool enabled, double cornerDegrees)
    : m_enabled(enabled)
    , m_cosCorner(std::cos(std::clamp(cornerDegrees, 1.0, 180.0) * Pi / 180.0))
{
}

bool StrokeCurve::isCorner(const StrokeSample &before, const StrokeSample &at, const StrokeSample &after) const
{
    const QPointF a = at.pos - before.pos, b = after.pos - at.pos;
    const double la = std::hypot(a.x(), a.y()), lb = std::hypot(b.x(), b.y());
    if (la < MinSegment || lb < MinSegment)
        return false;
    // The turn angle is the angle between the incoming and outgoing directions.
    const double cosTurn = (a.x() * b.x() + a.y() * b.y()) / (la * lb);
    return cosTurn < m_cosCorner;
}

void StrokeCurve::add(const StrokeSample &sample, std::vector<StrokeSample> &out)
{
    if (!m_enabled) {
        out.push_back(sample);
        return;
    }
    if (m_count > 0 && distance(m_points.back().pos, sample.pos) < MinSegment) {
        // Same place: keep the newest pressure, tilt and time for the point (no segment yet).
        const QPointF pos = m_points.back().pos;
        m_points.back() = sample;
        m_points.back().pos = pos;
        if (m_count == 1)
            out.push_back(m_points.back()); // the first point is drawn at once
        return;
    }
    m_points.push_back(sample);
    ++m_count;
    if (m_points.size() > 4)
        m_points.erase(m_points.begin());
    if (m_count == 1) {
        out.push_back(sample); // the stroke starts at once
        return;
    }
    if (m_count == 2)
        return; // segment 0→1 waits for point 2 (its end tangent)
    // Points available: [.., p(n-3), p(n-2), p(n-1), p(n)]: emit p(n-2) → p(n-1).
    const size_t n = m_points.size();
    const StrokeSample &p1 = m_points[n - 3], &p2 = m_points[n - 2], &p3 = m_points[n - 1];
    const StrokeSample *p0 = n >= 4 ? &m_points[n - 4] : nullptr;
    emitSegment(p0, p1, p2, &p3, out);
}

void StrokeCurve::finish(std::vector<StrokeSample> &out)
{
    if (!m_enabled || m_count < 2)
        return;
    const size_t n = m_points.size();
    const StrokeSample &p1 = m_points[n - 2], &p2 = m_points[n - 1];
    const StrokeSample *p0 = n >= 3 ? &m_points[n - 3] : nullptr;
    emitSegment(p0, p1, p2, nullptr, out);
    m_count = 0;
    m_points.clear();
}

void StrokeCurve::emitSegment(const StrokeSample *p0, const StrokeSample &p1, const StrokeSample &p2, const StrokeSample *p3,
                              std::vector<StrokeSample> &out) const
{
    // Missing or cornered neighbours are reflected through the segment's end, which makes the
    // tangent there follow the chord (a straight start or end).
    const QPointF b = p1.pos, c = p2.pos;
    const QPointF a = (p0 && !isCorner(*p0, p1, p2)) ? p0->pos : 2.0 * b - c;
    const QPointF d = (p3 && !isCorner(p1, p2, *p3)) ? p3->pos : 2.0 * c - b;
    const double chord = distance(b, c);
    // Centripetal parameterisation: knot spacing = sqrt(distance).
    auto knot = [](const QPointF &u, const QPointF &v) { return std::max(1e-4, std::sqrt(distance(u, v))); };
    const double t0 = 0, t1 = t0 + knot(a, b), t2 = t1 + knot(b, c), t3 = t2 + knot(c, d);
    const int steps = std::clamp(int(std::ceil(chord / Subdivision)), 1, MaxSubdivisions);
    for (int i = 1; i <= steps; ++i) {
        const double u = double(i) / steps;
        StrokeSample s = StrokeSample::lerp(p1, p2, u);
        if (i < steps) {
            const double t = t1 + (t2 - t1) * u;
            const QPointF a1 = (t1 - t) / (t1 - t0) * a + (t - t0) / (t1 - t0) * b;
            const QPointF a2 = (t2 - t) / (t2 - t1) * b + (t - t1) / (t2 - t1) * c;
            const QPointF a3 = (t3 - t) / (t3 - t2) * c + (t - t2) / (t3 - t2) * d;
            const QPointF b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2;
            const QPointF b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3;
            s.pos = (t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2;
        } else {
            s.pos = c; // exactly through the input point
        }
        out.push_back(s);
    }
}

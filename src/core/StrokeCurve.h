#pragma once

#include "StrokeSpacer.h"

#include <vector>

// Curve interpolation between input samples (milestone 11 ink; research/m11-brush-architecture.md,
// A4). Without it the path between two input events is a straight line, so fast curves, which a
// pen reports with few samples, show polygon corners.
//
// Each segment P1→P2 is a centripetal Catmull–Rom curve (alpha 0.5, which never forms cusps or
// loops between unevenly spaced points) through its neighbours P0 and P3, subdivided about every
// pixel. Pressure, tilt and time are interpolated linearly along the segment, so they never
// overshoot.
//
// Corners: where the input turns by more than `cornerDegrees` at a point, the curve does not
// round it: the tangent there follows the segment's own chord, so the corner stays sharp.
//
// Latency: a segment needs the next point, so the line trails the pen by exactly one input
// sample until the next sample or pen-up (finish()).
class StrokeCurve
{
public:
    static constexpr double Subdivision = 1.0; // px between interpolated points (at most)
    static constexpr int MaxSubdivisions = 256;

    // disabled: samples pass through unchanged.
    StrokeCurve(bool enabled, double cornerDegrees);

    bool enabled() const { return m_enabled; }
    // Appends the samples that are final now.
    void add(const StrokeSample &sample, std::vector<StrokeSample> &out);
    // Pen up: the last segment.
    void finish(std::vector<StrokeSample> &out);

private:
    void emitSegment(const StrokeSample *p0, const StrokeSample &p1, const StrokeSample &p2, const StrokeSample *p3,
                     std::vector<StrokeSample> &out) const;
    bool isCorner(const StrokeSample &before, const StrokeSample &at, const StrokeSample &after) const;

    bool m_enabled;
    double m_cosCorner; // cos of the corner angle: a turn with a smaller cosine is a corner
    std::vector<StrokeSample> m_points; // the last up to 4 distinct points
    int m_count = 0;                    // distinct points seen
};

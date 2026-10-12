#pragma once

#include "StrokeSpacer.h"

#include <vector>

// Distance-based stroke smoothing ("pull string"). The brush point follows the pointer on a
// string of length radius: it moves only when the pointer is farther away than that, and then
// straight towards it. Jitter smaller than the radius disappears and curves become rounder.
//
// Input segments are walked in steps of at most StepLength document pixels, so the result depends
// on the path, not on how many events describe it or how fast they arrive. The lag is a distance,
// not a time: when the pointer stops, the brush stops too. finish() draws the remaining straight
// tail to the last pointer position, so a stroke always ends where the pen was lifted.
class StrokeSmoother
{
public:
    static constexpr qreal StepLength = 0.5;

    // radius in document pixels; 0 passes samples through unchanged.
    explicit StrokeSmoother(qreal radius);

    qreal radius() const { return m_radius; }
    // Each call appends the brush positions it produced to out.
    void begin(const StrokeSample &sample, std::vector<StrokeSample> &out);
    void moveTo(const StrokeSample &sample, std::vector<StrokeSample> &out);
    void finish(std::vector<StrokeSample> &out);

private:
    qreal m_radius;
    StrokeSample m_pointer; // last raw sample
    StrokeSample m_brush;   // smoothed position
};

#pragma once

#include "CoverageDeposit.h"

// Dry media: graphite, charcoal, chalk, pastel (research/m11-brush-architecture.md, A3).
//
// The paper is a height field h in document space (the brush's texture, "paper" when none is set;
// the texture strength is the tooth: h' = 1 − tooth × (1 − h)). Each dab has a penetration depth d
// from the dry-depth setting, pressure (pressure → penetration) and tilt (side shading reaches
// only the peaks). A pixel of the dab deposits
//     tip × smoothstep(0.5 + (d − (1 − h')) / softness)
// into the stroke's coverage (the coverage engine's accumulation, opacity ceiling and composite),
// so light pressure catches the peaks of the tooth and heavy pressure fills the valleys; within
// a stroke build-up darkens the peaks first, and later strokes meet the same tooth.
//
// Charcoal and pastel add:
// - streaks: h' mixed with a fine grain field sampled in a frame turned to the stroke heading and
//   stretched along it, anchored in document space so streaks continue from dab to dab;
// - edge breakup: towards the dab's rim the penetration is lowered by a second paper sample, so
//   the rim crumbles instead of fading evenly;
// - grit: per-stroke seeded pixels that take only a fifth of the deposit.
class DryDeposit : public CoverageDeposit
{
public:
    explicit DryDeposit(StrokeContext &context);

protected:
    void rasterize(const DabParams &dab, const QRect &rect) override;

private:
    const QImage *m_streaks = nullptr;
};

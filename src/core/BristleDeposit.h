#pragma once

#include "CoverageDeposit.h"

#include <vector>

// Bristle brushes (research/m11-brush-architecture.md, A5): a 2D contact approximation, not a
// physical bristle simulation.
//
// The footprint is the dab's own shape (size, roundness, angle): a round brush is a disc, a flat
// brush a thin ellipse turned across the stroke. `bristleCount` bristles sit at seeded places in
// it, each with a length (stiffness), a paint reservoir, a depletion rate and a colour. For every
// dab the pipeline places, each bristle draws an anti-aliased capsule from where it was at the
// previous dab to where it is now, so a fast stroke leaves continuous streaks, not dots. The
// capsules go into the coverage engine's buffers with a colour per pixel, and are composited by it
// once per pixel per call.
//
// - Splay: pressure spreads the bristles from a tight tip (spread setting) to the full footprint.
// - Contact: a bristle touches once pressure passes its threshold (longer bristles first),
//   rising softly over a pressure range, so light pressure paints with a few long bristles.
// - Reservoir: each bristle's paint runs down with the distance it paints (its own rate); a dry
//   bristle first skips the valleys of the paper texture (dry brush), then stops.
// - Pickup: each bristle's colour moves towards the colour of the layer under it as it was
//   before the stroke (never the stroke's own fresh paint), so crossing existing paint drags
//   multicoloured streaks; pigment mixing applies.
//
// Deterministic: the bristles and every jitter come from the stroke seed.
class BristleDeposit : public CoverageDeposit
{
public:
    explicit BristleDeposit(StrokeContext &context);

    void dab(StrokeSurface &image, const DabParams &dab, const QRect &rect, TileSnapshot &before, QRect &dirty) override;
    // Bristle state (positions, reservoirs, colours) depends on every dab before it, so the tail
    // cannot be redrawn on its own: the live end taper falls back to the hold-back.
    bool supportsRebuild() const override { return false; }
    qsizetype bufferBytes() const override;

private:
    struct Bristle
    {
        double u = 0, w = 0;     // place in the unit disc (u along the dab angle)
        double threshold = 0;    // pressure at which it starts to touch
        double reservoir = 1;    // paint left, 0..1
        double depletion = 0;    // share lost per 100 px painted
        float color[3] = {0, 0, 0}; // straight sRGB 0..255
        QPointF previous;
        bool placed = false;
    };

    QRgb pixelBefore(const StrokeSurface &image, const TileSnapshot &before, int x, int y) const;
    void pickUp(Bristle &b, const StrokeSurface &image, const TileSnapshot &before, double travelled);

    std::vector<Bristle> m_bristles;
    bool m_initialised = false;
};

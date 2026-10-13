#pragma once

#include "CoverageDeposit.h"

#include <vector>

// Procedural stamps (research/m11-brush-architecture.md, A8): Strands (hair, fur), Grass,
// Foliage, Splatter and Pattern. Each dab generates its elements from the stroke seed and its own
// index, so a dab always draws the same geometry (the live end taper can redraw it, and tiles and
// dense images give the same pixels).
//
// Elements are built from three anti-aliased primitives: tapered capsules (polylines for strands,
// blades and pattern lines), ellipses (drops, dots) and leaf lenses. One element is rasterised
// into a scratch buffer first (the largest coverage of its parts per pixel, so joints and overlaps
// within it never darken), then deposited once per pixel into the coverage engine's buffers with
// the accumulation rule, the per-dab texture and its colour. Compositing is the coverage
// engine's.
//
// - Strands: roots spread over a disc, each strand follows the direction with its own turn,
//   length, constant curl and taper.
// - Grass: roots across the stroke, blades grow in the element direction (up by default), droop
//   more towards the tip and shade from darker roots to lighter tips.
// - Foliage: leaf lenses (pointed at both ends, with a lighter midrib when wide enough) scattered
//   over a disc, turned around the direction.
// - Splatter: drops with power-law sizes (few large, many small), stretched along their
//   direction, large ones with satellite droplets ahead.
// - Pattern: one motif per dab in the dab's frame (along, across the stroke); zigzag and
//   scallops are as long as the spacing so they join into a continuous line.
class ProceduralDeposit : public CoverageDeposit
{
public:
    explicit ProceduralDeposit(StrokeContext &context);

    // How far an element can reach from the dab centre (StrokeContext::dabRect).
    static double reach(const BrushSettings &settings, const DabParams &dab);

protected:
    void rasterize(const DabParams &dab, const QRect &rect) override;

private:
    double heading(const DabParams &dab) const;
    QRgb elementColor(const DabParams &dab, int element) const;
    double rnd(const DabParams &dab, int element, int stream) const;

    void strands(const DabParams &dab, const QRect &rect, bool grass);
    void foliage(const DabParams &dab, const QRect &rect);
    void splatter(const DabParams &dab, const QRect &rect);
    void pattern(const DabParams &dab, const QRect &rect);

    // One element: begin (its bounds), add primitives, end (deposit).
    bool begin(const QRectF &bounds, const QRect &rect);
    void capsule(QPointF a, QPointF b, double ra, double rb, double ta = 0, double tb = 0);
    void ellipse(QPointF c, double along, double across, double angle);
    void lens(QPointF base, double angle, double length, double width);
    // `shade`: grass, darker towards the root (the parts' t).
    void end(const DabParams &dab, QRgb color, bool shade = false);

    QRect m_box;              // the element's pixels inside the rectangle being drawn
    std::vector<float> m_v;   // coverage per pixel of m_box
    std::vector<float> m_t;   // along-the-element fraction at that coverage (shading)
};

#pragma once

#include "StrokeDeposit.h"
#include "WetLayer.h"

#include <memory>

// Watercolour (blend Watercolor; WetLayer.h has the model). Each dab adds water and pigment under
// its footprint; after the dabs of every engine call the simulation advances by the simulation
// time the input events covered (WetLayer::StepMs per step, at most MaxStepsPerCall), and the
// changed tiles are written.
//
// The wet state belongs to the layer, not the stroke: the canvas passes its WetLayer
// (StrokeEngine::setWetLayer), keeps it between strokes (wet-on-wet) and lets it dry on a timer.
// Without one (previews, thumbnails, the brush sheet, tests) the stroke has its own, and pen-up
// lets it dry completely, so a rendered stroke is the finished, dry result.
class WatercolorDeposit : public StrokeDeposit
{
public:
    static constexpr int MaxStepsPerCall = 6;
    static constexpr int MaxDryingSteps = 4000;          // own wet layer: drying at pen-up is bounded
    static constexpr qint64 MaxDryingWork = 30'000'000; // cell-steps (about 0.5 s), then it settles

    explicit WatercolorDeposit(StrokeContext &context);

    void dab(StrokeSurface &image, const DabParams &dab, const QRect &rect, TileSnapshot &before, QRect &dirty) override;
    QRect flush(StrokeSurface &image, TileSnapshot &before) override;
    QRect finishStroke(StrokeSurface &image, TileSnapshot &before) override;
    qsizetype bufferBytes() const override;

private:
    WetLayer *wet(const StrokeSurface &image);

    std::unique_ptr<WetLayer> m_own;
    WetLayer *m_wet = nullptr;
    double m_simTimeMs = -1; // simulation time already stepped (input time base)
    double m_latestMs = -1;  // newest dab time of this call
    bool m_paintedThisCall = false;
};

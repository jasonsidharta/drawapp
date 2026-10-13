#include "WatercolorDeposit.h"

#include "BrushTips.h"
#include "TileSnapshot.h"

#include <QElapsedTimer>

#include <cmath>

WatercolorDeposit::WatercolorDeposit(StrokeContext &context)
    : StrokeDeposit(context)
{
}

WetLayer *WatercolorDeposit::wet(const StrokeSurface &image)
{
    if (m_wet)
        return m_wet;
    if (m_ctx.wet && m_ctx.wet->size() == image.rect().size()) {
        m_wet = m_ctx.wet;
    } else {
        m_own = std::make_unique<WetLayer>(image.rect().size());
        m_wet = m_own.get();
    }
    // The paint on the paper behaves as the brush that last painted it says.
    const BrushSettings &st = m_ctx.settings;
    WetLayer::Params p;
    p.spread = st.wcSpread;
    p.edgeDarkening = st.wcEdgeDarkening;
    p.granulation = st.wcGranulation;
    p.dryingSeconds = st.wcDryingTime;
    p.lifting = st.wcLifting;
    p.paper = BrushTips::texture(st.texture.isEmpty() ? QStringLiteral("rough") : st.texture);
    p.tooth = st.texture.isEmpty() ? 0.6 : st.textureStrength;
    m_wet->setParams(p);
    m_wet->setClip(m_ctx.clip);
    return m_wet;
}

void WatercolorDeposit::dab(StrokeSurface &image, const DabParams &dab, const QRect &rect, TileSnapshot &before, QRect &dirty)
{
    WetLayer *layer = wet(image);
    const BrushSettings &st = m_ctx.settings;
    // The brush wets the paper towards its own wetness and exchanges paint with the wash; the
    // share per dab is the flow normalised to the spacing, and opacity (and pressure on it) sets
    // the brush's pigment concentration.
    const double fraction = m_ctx.passExponent(dab.stepFraction);
    const double pass = 1.0 - std::pow(1.0 - std::clamp(dab.passFlow, 0.0, 1.0), fraction);
    const double water = st.wcWater;
    const double pigment = st.wcPigment * dab.ceiling;
    const DabShape shape(dab, st, m_ctx.tip);
    const auto footprint = [&](int x, int y) { return shape.value(x + 0.5 - dab.center.x(), y + 0.5 - dab.center.y()); };
    if (!layer->addPaint(image, before, rect, footprint, dab.color, water, pigment, pass)) {
        m_ctx.allocationFailed = true;
        return;
    }
    if (dab.timeMs >= 0) {
        if (m_simTimeMs < 0)
            m_simTimeMs = dab.timeMs;
        m_latestMs = std::max(m_latestMs, dab.timeMs);
    }
    m_paintedThisCall = true;
    dirty |= rect;
}

QRect WatercolorDeposit::flush(StrokeSurface &image, TileSnapshot &before)
{
    if (!m_wet || m_ctx.allocationFailed)
        return {};
    int steps = 0;
    if (m_latestMs >= 0 && m_simTimeMs >= 0) {
        steps = int(std::floor((m_latestMs - m_simTimeMs) / WetLayer::StepMs));
        steps = std::clamp(steps, 0, MaxStepsPerCall);
        m_simTimeMs += steps * WetLayer::StepMs;
        if (m_latestMs - m_simTimeMs > MaxStepsPerCall * WetLayer::StepMs)
            m_simTimeMs = m_latestMs; // a long stall is not caught up all at once
    } else if (m_paintedThisCall) {
        steps = 1; // no event times: one step per input event
    }
    m_paintedThisCall = false;
    if (steps > 0 && m_wet->stepBudgetMs() > 0) {
        // On the canvas: steps until the event's time budget is spent; the rest is not caught up.
        QElapsedTimer timer;
        timer.start();
        for (int done = 0; done < steps; ++done) {
            m_wet->advance(1);
            if (timer.nsecsElapsed() / 1e6 >= m_wet->stepBudgetMs())
                break;
        }
    } else if (steps > 0) {
        m_wet->advance(steps);
    }
    const QRect changed = m_wet->render(image, before);
    if (m_wet->allocationFailed())
        m_ctx.allocationFailed = true;
    return changed;
}

QRect WatercolorDeposit::finishStroke(StrokeSurface &image, TileSnapshot &before)
{
    if (!m_own || m_ctx.allocationFailed)
        return {};
    // A stroke with its own wet layer (previews, thumbnails, tests) is finished dry, with bounded
    // work: at most MaxDryingSteps steps and MaxDryingWork cell-steps, then whatever is still
    // wet settles where it is.
    qint64 work = 0;
    for (int done = 0; done < MaxDryingSteps && m_own->isWet() && work < MaxDryingWork; done += 50) {
        work += m_own->wetCells() * 50;
        m_own->advance(50);
    }
    if (m_own->isWet())
        m_own->settleAll();
    return m_own->render(image, before);
}

qsizetype WatercolorDeposit::bufferBytes() const
{
    return m_own ? m_own->bytes() : 0;
}

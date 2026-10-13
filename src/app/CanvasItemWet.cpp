// Watercolour on the canvas (milestone 11, research/m11-brush-architecture.md A7): the paint stays
// wet after the pen lifts and dries on a timer, and the next watercolour stroke on the same layer
// paints into it (wet-on-wet).
//
// The wet state (WetLayer) belongs to one layer. The undo record of the latest wet stroke stays
// open while its paint dries, so the drying is part of that step; the next watercolour stroke
// commits it and keeps the paint wet. Everything else that edits the document (another tool,
// another layer, a selection, a transform, undo or redo) first ends the wet session: the record is
// committed and the paint stays exactly as it looks, dry from then on. Files do not store wet
// paint; saving says so and the paint keeps drying on the canvas.

#include "CanvasItem.h"

#include "MemoryBudget.h"

#include <cmath>

namespace {
constexpr int DryingTickMs = 50;         // 20 updates a second while paint dries (it changes slowly)
constexpr double MaxCatchUpMs = 8 * 16.0; // a stalled timer is not caught up all at once
constexpr double TickBudgetMs = 8.0;      // stepping per drying tick; beyond it the paint dries slower
constexpr double EventBudgetMs = 4.0;     // stepping per stroke event (WetLayer::setStepBudgetMs)
}

void CanvasItem::prepareWetStroke(bool watercolour)
{
    if (!watercolour || (m_wetLayer && (m_wetLayerId != m_strokeLayerId || m_wetLayer->size() != m_document.size()))) {
        finishWetPaint();
        if (!watercolour)
            return;
    }
    // The previous wet stroke becomes its own undo step; its paint stays wet for this one.
    m_wetTimer.stop();
    commitWetPaint();
    if (!m_wetLayer) {
        m_wetLayer = std::make_unique<WetLayer>(m_document.size());
        m_wetLayer->setStepBudgetMs(EventBudgetMs);
        m_wetLayerId = m_strokeLayerId;
    }
    m_engine->setWetLayer(m_wetLayer.get());
}

bool CanvasItem::keepStrokeWet()
{
    if (!m_wetLayer || m_wetLayerId != m_strokeLayerId)
        return false;
    if (!m_wetLayer->isWet()) {
        m_wetLayer.reset(); // dried during the stroke: an ordinary undo step
        m_wetLayerId = 0;
        return false;
    }
    m_wetBefore = std::move(m_strokeBefore);
    m_strokeBefore = TileSnapshot();
    m_wetLabel = QStringLiteral("Watercolour stroke");
    m_wetPendingMs = 0;
    m_wetTimer.setInterval(DryingTickMs);
    m_wetClock.start();
    m_wetTimer.start();
    emit historyChanged();
    return true;
}

void CanvasItem::commitWetPaint()
{
    if (m_wetBefore.isEmpty())
        return;
    TileSnapshot before = std::move(m_wetBefore);
    m_wetBefore = TileSnapshot();
    commitPixels(m_wetLayerId, std::move(before), m_wetLabel);
    // Drying after this point (when the session continues) is a step of its own.
    m_wetLabel = QStringLiteral("Watercolour drying");
    emit historyChanged();
}

void CanvasItem::finishWetPaint()
{
    if (!m_wetLayer && m_wetBefore.isEmpty())
        return;
    m_wetTimer.stop();
    commitWetPaint();
    // The pixels already show all the pigment (suspended and settled): nothing changes on screen.
    m_wetLayer.reset();
    m_wetLayerId = 0;
    m_wetBefore = TileSnapshot();
    emit historyChanged();
}

void CanvasItem::dryWetPaintTick()
{
    const double ms = double(m_wetClock.nsecsElapsed()) / 1e6;
    m_wetClock.start();
    advanceWetPaint(std::min(ms, MaxCatchUpMs), TickBudgetMs);
}

void CanvasItem::advanceWetPaint(double ms, double budgetMs)
{
    if (!m_wetLayer || m_gesture == Gesture::Stroke) // a stroke steps the simulation itself
        return;
    const int index = m_document.indexOf(m_wetLayerId);
    if (index < 0) {
        finishWetPaint();
        return;
    }
    m_wetPendingMs += std::max(0.0, ms);
    const int steps = int(std::floor(m_wetPendingMs / WetLayer::StepMs));
    if (steps <= 0)
        return;
    m_wetPendingMs -= steps * WetLayer::StepMs;
    if (budgetMs > 0) {
        // The timer: as many steps as fit in the budget; time that does not fit is dropped.
        QElapsedTimer timer;
        timer.start();
        for (int done = 0; done < steps; ++done) {
            m_wetLayer->advance(1);
            if (timer.nsecsElapsed() / 1e6 >= budgetMs)
                break;
        }
    } else {
        m_wetLayer->advance(steps);
    }
    TiledSurface layer(m_document.pixels(index));
    const QRect changed = m_wetLayer->render(layer, m_wetBefore);
    strokePixelsChanged(changed, 0);
    if (m_wetLayer->allocationFailed()) {
        finishWetPaint();
        emit notice(QStringLiteral("Not enough memory to keep the watercolour wet (%1); it stays as it is now.")
                        .arg(MemoryBudget::shortageText()));
        return;
    }
    if (!m_wetLayer->isWet())
        finishWetPaint();
}

void CanvasItem::noteWetPaintSaved()
{
    if (!m_wetLayer)
        return;
    commitWetPaint();
    emit notice(QStringLiteral("The watercolour is still wet: it is saved as it looks now (files do not store wet paint). "
                               "It keeps drying here; save again when it is dry to keep the dry result."));
}

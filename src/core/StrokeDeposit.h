#pragma once

// What a dab does: the part of a stroke that differs between brush engines
// (research/m11-brush-architecture.md, A1). StrokeEngine runs the shared pipeline (input filters,
// smoothing, path sensors, taper, spacing, dynamics, DabParams, dab counting) and hands each placed
// dab to one deposit strategy, chosen from the settings' blend and stamp when the stroke starts.
//
// Contract for every strategy:
// - capture into `before` (the stroke's undo snapshot) every tile it is about to change, before
//   changing it, so cancelling a stroke restores the layer exactly;
// - change only pixels inside the dab's rectangle (or the segment's), add what it changed to
//   `dirty`, and respect the selection (StrokeContext::clip) exactly once per pixel;
// - set StrokeContext::allocationFailed and stop when a tile cannot be had;
// - be deterministic: all randomness from StrokeContext::random (seed, dab index, salt).

#include "StrokeContext.h"

#include <memory>

class TileSnapshot;

class StrokeDeposit
{
public:
    explicit StrokeDeposit(StrokeContext &context) : m_ctx(context) {}
    virtual ~StrokeDeposit() = default;
    StrokeDeposit(const StrokeDeposit &) = delete;
    StrokeDeposit &operator=(const StrokeDeposit &) = delete;

    // One dab at `rect` (StrokeContext::dabRect, clipped to the image, never empty).
    virtual void dab(StrokeSurface &image, const DabParams &dab, const QRect &rect, TileSnapshot &before, QRect &dirty) = 0;
    // After the dabs of one engine call (begin, moveTo, advanceTime, finish): bring the layer up to
    // date with what the dabs recorded, once per pixel.
    virtual void flush(StrokeSurface &image, const TileSnapshot &before) { Q_UNUSED(image) Q_UNUSED(before) }

    // Memory held by the stroke's own buffers (not the undo snapshot).
    virtual qsizetype bufferBytes() const = 0;
    virtual int coverageTileCount() const { return 0; }

    // The strategy for these settings (StrokeContext already filled in).
    static std::unique_ptr<StrokeDeposit> create(StrokeContext &context);

protected:
    StrokeContext &m_ctx;
};

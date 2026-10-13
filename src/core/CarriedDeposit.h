#pragma once

#include "StrokeDeposit.h"

#include <unordered_map>
#include <vector>

// Smudge and the wet-paint prototype (milestones 6 and 9): a carried patch of paint
// (premultiplied RGBA as floats 0..255, centred on the dab, large enough for the biggest dab).
//
// Smudge: the patch is laid down with strength × tip and then takes on the pixels it passed with
// the pickup rate (both per pass, normalised to the spacing like flow). Dulling replaces the
// patch by its average after each pickup. Wet paint is the same patch, refilled with the brush
// colour while the load lasts, composited over the pixels (attack) and picking up canvas paint
// (pull). Under a selection both work on an unclipped copy and write
// before + (work − before) × selection once per pixel.
class CarriedDeposit : public StrokeDeposit
{
public:
    explicit CarriedDeposit(StrokeContext &context) : StrokeDeposit(context) {}

    void dab(StrokeSurface &image, const DabParams &dab, const QRect &rect, TileSnapshot &before, QRect &dirty) override;
    qsizetype bufferBytes() const override;

private:
    void carry(StrokeSurface &image, const DabParams &dab, const QRect &rect, const TileSnapshot &before);
    void carryInto(QImage &target, const QPoint &origin, const DabParams &dab, const QRect &rect);
    void loadPatch(const StrokeSurface &image, const DabParams &dab);
    // Mix mode: the alpha-weighted average of the area under the dab (centre-weighted pickup).
    bool sample(const QImage &area, const QPoint &origin, const DabParams &dab, const QRect &rect, float out[4]) const;
    void mixInto(QImage &target, const QPoint &origin, const DabParams &dab, const QRect &rect);
    QRgb workPixel(const StrokeSurface &image, int x, int y) const;

    std::vector<float> m_patch;
    int m_patchSize = 0;
    bool m_patchLoaded = false;
    // Under a selection: the unclipped result, per 64 × 64 tile touched.
    std::unordered_map<qint64, QImage> m_work;
    // The dab's area, reused from dab to dab (grown when needed) instead of a new image per dab.
    QImage m_region;
    // Mix mode: the one carried colour (premultiplied RGBA floats 0..255).
    float m_mixColor[4] = {0, 0, 0, 0};
    bool m_mixLoaded = false;
};

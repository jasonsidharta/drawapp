#pragma once

#include "StrokeDeposit.h"

#include <unordered_map>
#include <vector>

// Paint and Erase with the tip, spray and hatching stamps (milestones 6 and 9).
//
// Coverage. Each pixel holds the stroke's coverage S, 0..1, in a sparse map of 64 × 64 tiles
// (16-bit). A dab with tip value v, opacity ceiling C and per-dab flow f gives:
//   Uniform: S = max(S, v·C)            overlaps and self-crossings never darken
//   BuildUp: S = S + v·f·(C − S)        approaches C, never passes it
// With colour dynamics (per-dab colour) the coverage tiles also keep a colour per pixel, mixed
// by coverage: c ← (c·S_old + c_dab·ΔS) / S_new.
//
// After each engine call the changed pixels are recomputed from the layer as it was before the
// stroke (the undo snapshot holds it), so the result never depends on how the input was split:
//   k = S × texture × selection, dithered to 8 bits (exact levels stay exact)
//   Paint: before composited with (colour, k) by the blend mode;  Erase: before × (1 − k)
//
// Subclasses (milestone 11 engines that also accumulate coverage) override how a dab is
// rasterised and reuse the buffers and the composite.
class CoverageDeposit : public StrokeDeposit
{
public:
    using CoverageTile = std::vector<quint16>;
    using ColorTile = std::vector<quint32>;

    explicit CoverageDeposit(StrokeContext &context) : StrokeDeposit(context) {}

    void dab(StrokeSurface &image, const DabParams &dab, const QRect &rect, TileSnapshot &before, QRect &dirty) override;
    void flush(StrokeSurface &image, const TileSnapshot &before) override;
    qsizetype bufferBytes() const override;
    int coverageTileCount() const override { return int(m_coverage.size()); }

protected:
    // Writes the dab into the coverage (and colour) tiles; the default is the tip, spray or
    // hatching stamp of milestones 6 and 9.
    virtual void rasterize(const DabParams &dab, const QRect &rect);
    // Marks `rect` for recompositing at the next flush.
    void touch(const QRect &rect);
    CoverageTile &coverageTile(int column, int row);
    ColorTile &colorTile(int column, int row);
    // The 16-bit increase of coverage `s` for a dab value `v` (0..1, after any masking) at pixel
    // (x, y) by the accumulation rule (the same arithmetic as the milestone 6 loops).
    quint32 increase(quint32 s, double v, quint32 ceiling, double flow, int x, int y, qint64 index) const
    {
        if (!m_ctx.buildUp) {
            const quint32 target = quint32(std::lround(v * ceiling));
            return target > s ? target - s : 0;
        }
        if (ceiling <= s)
            return 0;
        const double inc = v * flow * double(ceiling - s);
        const quint32 step = quint32(inc + BrushRaster::threshold(x + int(index * 29), y + int(index * 17)));
        return std::min<quint32>(ceiling - s, step);
    }
    // Adds `add` to pixel (x, y)'s coverage, mixing `color` into the colour tile when there is one.
    static void deposit(quint16 &s, quint32 add, quint32 *colorCell, QRgb color)
    {
        if (colorCell)
            *colorCell = s == 0 ? color : BrushRaster::mixColor(*colorCell, s, color, add);
        s = quint16(s + add);
    }

    void rasterizeTip(const DabParams &dab, const QRect &rect);
    void rasterizeSpray(const DabParams &dab, const QRect &rect);
    void composite(StrokeSurface &image, const QRect &rect, const TileSnapshot &before);

    std::unordered_map<qint64, CoverageTile> m_coverage;
    std::unordered_map<qint64, ColorTile> m_colors; // per-dab colour only
    std::unordered_map<qint64, QRect> m_touched;    // per tile: area to recomposite in this call
};

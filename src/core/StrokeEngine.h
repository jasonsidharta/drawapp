#pragma once

#include "BrushSettings.h"
#include "StrokeSmoother.h"
#include "StrokeSpacer.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <memory>
#include <unordered_map>
#include <vector>

class TileSnapshot;

namespace BrushTips {
struct Tip;
struct Texture;
}

// Processes and renders one stroke of any brush. It is the only brush renderer: every preset,
// the scratchpad and the preset thumbnails go through it. It takes input samples in document
// coordinates, so it can be driven without pointer events (tests, replays, future automation).
//
// Pipeline per call:
//   samples -> StrokeSmoother -> StrokeSpacer (distance-spaced dabs, input interpolated)
//           (+ airbrush dabs from elapsed time) -> dab parameters from BrushSettings
//           -> stroke coverage S (Paint, Erase) or direct pixel smudging (Smudge)
//
// Coverage (Paint, Erase). Each pixel holds the stroke's coverage S, 0..1, in a sparse map of
// 64 × 64 tiles (16-bit). A dab with tip value v, opacity ceiling C and per-dab flow f gives:
//   Uniform: S = max(S, v·C)            overlaps and self-crossings never darken
//   BuildUp: S = S + v·f·(C − S)        approaches C, never passes it
// C = opacity × pressure→opacity × tilt factor, rounded to a whole 8-bit level so flat interiors
// come out exact. f is normalised to the spacing: f = 1 − (1 − flow)^(step / diameter), so one
// pass over a pixel deposits about `flow` of the way to C whatever the spacing. Opacity therefore
// limits the stroke and flow sets how fast it gets there; neither is applied twice.
//
// After each call the changed pixels are recomputed from the layer as it was before the stroke
// (the stroke's undo snapshot holds it), so the result never depends on how the input was split:
//   k = S × texture × selection, dithered to 8 bits (exact levels stay exact)
//   Paint: result = before composited under (colour, k);  Erase: result = before × (1 − k)
//
// Smudge works on the pixels directly: a carried patch (premultiplied, so transparent pixels
// carry no colour) is laid down with strength × tip and then takes on the pixels it passed with
// the pickup rate (both per pass, normalised to the spacing like flow). Smudging into transparency thins the paint; dragging paint into transparency
// spreads it with its own alpha; pixels outside the layer are never picked up.
//
// Stroke state (settings, colour, random seed) is fixed at construction. All randomness (angle
// jitter, build-up rounding) comes from the seed and the dab index, so the same input and seed
// give identical pixels. To cancel a stroke, swap its undo snapshot back into the layer.
class StrokeEngine
{
public:
    static constexpr int Tile = 64;
    // At most this many airbrush dabs per call; a longer stall is not caught up all at once.
    static constexpr int MaxTimeDabsPerCall = 24;
    // Tips smaller than this radius are area-sampled (4 × 4 per pixel) instead of edge-estimated.
    static constexpr double SmallTipRadius = 3.0;
    // Pen tilt from vertical at which the tilt factors reach their full value, and below which a
    // tilt azimuth is too unsteady to orient the tip.
    static constexpr double FullTiltDegrees = 60.0;
    static constexpr double MinAzimuthTiltDegrees = 15.0;

    // smoothingRadius is the pull-string length in document pixels (0 = off).
    StrokeEngine(const BrushSettings &settings, const QColor &color, quint64 seed = 0, qreal smoothingRadius = 0);
    ~StrokeEngine();
    StrokeEngine(const StrokeEngine &) = delete;
    StrokeEngine &operator=(const StrokeEngine &) = delete;

    // Optional selection: a Format_Grayscale8 image the size of the layer, 255 = fully editable,
    // 0 = protected. Set before begin().
    void setClipMask(const QImage &mask);

    // Each call changes `image` (Format_ARGB32_Premultiplied, the stroke's layer) and captures
    // the tiles it is about to change into `before` (the stroke's undo snapshot) first. Returns
    // the changed area, clipped to the image, or an empty rect.
    QRect begin(QImage &image, const StrokeSample &sample, TileSnapshot &before);
    QRect moveTo(QImage &image, const StrokeSample &sample, TileSnapshot &before);
    // Airbrush: the pen is held at the same place until timeMs. No effect for other brushes.
    QRect advanceTime(QImage &image, double timeMs, TileSnapshot &before);
    // Pen up: completes the smoothed line to the last input position. A stroke that never moved
    // is a dot with one full pass of paint.
    QRect finish(QImage &image, TileSnapshot &before);

    const BrushSettings &settings() const { return m_settings; }
    bool usesTime() const;
    qint64 dabCount() const { return m_dabCount; }
    int coverageTileCount() const { return int(m_coverage.size()); }
    // Memory held by the stroke itself (coverage tiles and smudge patch), not the undo snapshot.
    qsizetype bufferBytes() const;

    // Renders a whole stroke at once; for previews, thumbnails, tests and replays.
    static QRect render(QImage &image, const BrushSettings &settings, const QColor &color,
                        const std::vector<StrokeSample> &samples, quint64 seed = 0, TileSnapshot *before = nullptr);

    // The parameters of one dab, as derived from the brush settings and the input sample.
    struct DabParams
    {
        QPointF center;
        double diameter = 1;
        double ceiling = 1;  // 0..1, a whole 8-bit level
        double flow = 1;     // per dab, already normalised to the spacing
        double passFlow = 1; // the flow setting: what one pass deposits (towards the ceiling)
        double angle = 0;    // radians
        double strength = 0; // smudge only
    };

private:
    using CoverageTile = std::vector<quint16>;

    // Spaces the smoothed samples into dabs, adds airbrush dabs up to nowMs (< 0: none) and
    // renders them; the caller composites.
    void process(QImage &image, TileSnapshot &before, double nowMs, QRect &dirty);
    void addTimeDabs(double nowMs);
    void updateHeading(double direction, double stepFraction);
    bool needsHeading(const StrokeSample &sample) const;
    DabParams paramsFor(const Dab &dab) const;
    void renderDab(QImage &image, const DabParams &dab, TileSnapshot &before, QRect &dirty);
    void rasterize(const DabParams &dab, const QRect &rect);
    void compositeTouched(QImage &image, const TileSnapshot &before);
    void composite(QImage &image, const QRect &rect, const TileSnapshot &before);
    void smudge(QImage &image, const DabParams &dab, const QRect &rect);
    double textureAt(int x, int y) const;
    CoverageTile &coverageTile(int column, int row);
    QRect dabRect(const DabParams &dab, const QImage &image) const;

    BrushSettings m_settings;
    quint32 m_color; // opaque RGB as ARGB32
    quint64 m_seed;
    bool m_buildUp;
    const BrushTips::Tip *m_tip = nullptr;
    const BrushTips::Texture *m_texture = nullptr;
    QImage m_clip;

    StrokeSmoother m_smoother;
    StrokeSpacer m_spacer;
    std::vector<StrokeSample> m_smoothed;
    std::vector<Dab> m_dabs;
    std::vector<bool> m_timed; // per entry of m_dabs: placed by elapsed time
    bool m_started = false;
    bool m_spacerStarted = false;

    // Direction of travel, averaged over roughly a dab's length so a tip that follows it turns
    // smoothly at corners. Unknown until the stroke has moved.
    QPointF m_heading;
    bool m_headingKnown = false;
    // A tip that follows the travel direction waits for the first movement to orient its first dab.
    bool m_firstDabPending = false;
    Dab m_firstDab{};

    double m_lastDabTimeMs = -1;
    double m_lastSeenTimeMs = -1;
    qint64 m_dabCount = 0;
    qint64 m_distanceDabCount = 0;
    qint64 m_timeDabCount = 0;
    DabParams m_firstParams; // the first rendered dab, for the dot top-up in finish()

    std::unordered_map<qint64, CoverageTile> m_coverage;
    std::unordered_map<qint64, QRect> m_touched; // per tile: area to recomposite in this call

    // Smudge: the carried paint, premultiplied RGBA as floats 0..255, centred on the dab.
    std::vector<float> m_patch;
    int m_patchSize = 0;
    bool m_patchLoaded = false;
};

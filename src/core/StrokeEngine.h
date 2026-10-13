#pragma once

#include "BrushSettings.h"
#include "StrokeContext.h"
#include "StrokeCurve.h"
#include "StrokeSurface.h"
#include "StrokeSmoother.h"
#include "StrokeSpacer.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

class TileSnapshot;
class StrokeDeposit;

namespace BrushTips {
struct Tip;
struct Texture;
}

// Processes and renders one stroke of any brush. It is the only brush renderer: every preset,
// the scratchpad, the Brush Studio preview and the preset thumbnails go through it. It takes input
// samples in document coordinates, so it can be driven without pointer events (tests, replays,
// future automation).
//
// Pipeline per call (docs/brush-engine-architecture.md):
//   input filters (jitter filter, pressure smoothing)
//     -> StrokeSmoother (pull string)
//     -> path sensors per smoothed sample (distance, speed, acceleration, direction)
//     -> end-taper hold-back (the last taperEnd px wait until the end is known)
//     -> StrokeSpacer (distance-spaced dabs; size and spacing from dynamics)
//     (+ airbrush dabs from elapsed time)
//     -> dab parameters (BrushSettings + CompiledDynamics + jitter + taper)
//     -> deposit strategy (StrokeDeposit.h): what the dab does to the stroke buffers and the layer
//        (coverage for Paint and Erase, the carried patch for Smudge and WetPaint, milestone 11
//        engines), brought up to date once per pixel per call, through the selection.
//
// Coverage (Paint, Erase). Each pixel holds the stroke's coverage S, 0..1, in a sparse map of
// 64 × 64 tiles (16-bit). A dab with tip value v, opacity ceiling C and per-dab flow f gives:
//   Uniform: S = max(S, v·C)            overlaps and self-crossings never darken
//   BuildUp: S = S + v·f·(C − S)        approaches C, never passes it
// C = opacity × pressure→opacity × tilt factor × dynamics × jitter × taper, rounded to a whole
// 8-bit level so flat interiors come out exact. f is normalised to the actual step after the dab
// and the tip: f = 1 − (1 − flow)^(step / L), with L the tip's pass length, so one pass along the
// centre of the stroke deposits `flow` of the way to C whatever the spacing, hardness or tip
// image. Opacity therefore limits the stroke and flow sets how fast it gets there; neither is
// applied twice.
//
// With colour dynamics (per-dab colour) the coverage tiles also keep a colour per pixel, mixed
// by coverage: c ← (c·S_old + c_dab·ΔS) / S_new.
//
// After each call the changed pixels are recomputed from the layer as it was before the stroke
// (the stroke's undo snapshot holds it), so the result never depends on how the input was split:
//   k = S × texture × selection, dithered to 8 bits (exact levels stay exact)
//   Paint: before composited with (colour, k) by the blend mode;  Erase: before × (1 − k)
//
// Smudge works on the pixels directly: a carried patch (premultiplied, so transparent pixels
// carry no colour) is laid down with strength × tip and then takes on the pixels it passed with
// the pickup rate (both per pass, normalised to the spacing like flow). Dulling replaces the
// patch by its average after each pickup. Wet paint is the same patch, refilled with the brush
// colour while the load lasts, composited over the pixels (attack) and picking up canvas paint
// (pull). Under a selection both work on an unclipped copy and write
// before + (work − before) × selection once per pixel.
//
// Stroke state (settings, colour, random seed) is fixed at construction. All randomness (jitter,
// scatter, random sensors, density, build-up rounding) comes from the seed and the dab index, so
// the same input and seed give identical pixels. To cancel a stroke, swap its undo snapshot back
// into the layer.
class StrokeEngine
{
public:
    static constexpr int Tile = 64;
    // At most this many airbrush dabs per call; a longer stall is not caught up all at once.
    static constexpr int MaxTimeDabsPerCall = 24;
    // Tips smaller than this radius are area-sampled (4 × 4 per pixel) instead of edge-estimated.
    static constexpr double SmallTipRadius = DabShape::SmallTipRadius;
    // Speed sensor: instantaneous speeds are capped and smoothed with this time constant.
    static constexpr double MaxSpeed = 100.0;      // px/ms
    static constexpr double MaxAcceleration = 10.0; // px/ms²
    static constexpr double SpeedTimeConstantMs = 25.0;
    static constexpr double PressureTimeConstantMs = 100.0; // at pressure smoothing 100 %
    // The smoothing setting's pull-string length: smoothing / 100 × this, in view pixels.
    static constexpr double MaxSmoothingRadius = 40.0;
    // In document pixels at this zoom (so it feels the same at every zoom).
    static double smoothingRadius(const BrushSettings &settings, double zoom = 1.0)
    {
        return settings.smoothing / 100.0 * MaxSmoothingRadius / (zoom > 0 ? zoom : 1.0);
    }

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
    //
    // The image is a StrokeSurface: a dense QImage or a tiled document layer (milestone 10). On a
    // tiled layer a write may find no memory for a tile; the engine then stops changing pixels
    // and allocationFailed() is true, and the caller should cancel the stroke.
    QRect begin(StrokeSurface &image, const StrokeSample &sample, TileSnapshot &before);
    QRect moveTo(StrokeSurface &image, const StrokeSample &sample, TileSnapshot &before);
    // Airbrush: the pen is held at the same place until timeMs. No effect for other brushes.
    QRect advanceTime(StrokeSurface &image, double timeMs, TileSnapshot &before);
    // Pen up: completes the smoothed line to the last input position and draws the held-back end
    // taper. A stroke that never moved is a dot with one full pass of paint.
    QRect finish(StrokeSurface &image, TileSnapshot &before);

    // Conveniences for the two kinds of image.
    QRect begin(QImage &image, const StrokeSample &s, TileSnapshot &b) { DenseSurface d(image); return begin(d, s, b); }
    QRect moveTo(QImage &image, const StrokeSample &s, TileSnapshot &b) { DenseSurface d(image); return moveTo(d, s, b); }
    QRect advanceTime(QImage &image, double t, TileSnapshot &b) { DenseSurface d(image); return advanceTime(d, t, b); }
    QRect finish(QImage &image, TileSnapshot &b) { DenseSurface d(image); return finish(d, b); }
    QRect begin(TiledImage &image, const StrokeSample &s, TileSnapshot &b) { TiledSurface d(image); return begin(d, s, b); }
    QRect moveTo(TiledImage &image, const StrokeSample &s, TileSnapshot &b) { TiledSurface d(image); return moveTo(d, s, b); }
    QRect advanceTime(TiledImage &image, double t, TileSnapshot &b) { TiledSurface d(image); return advanceTime(d, t, b); }
    QRect finish(TiledImage &image, TileSnapshot &b) { TiledSurface d(image); return finish(d, b); }

    // A tile could not be allocated during this stroke (memory budget); pixels stopped changing.
    bool allocationFailed() const { return m_ctx.allocationFailed; }

    const BrushSettings &settings() const { return m_ctx.settings; }
    bool usesTime() const;
    qint64 dabCount() const { return m_dabCount; }
    int coverageTileCount() const;
    // Memory held by the stroke itself (coverage and colour tiles, patch), not the undo snapshot.
    qsizetype bufferBytes() const;

    // Renders a whole stroke at once; for previews, thumbnails, tests and replays.
    static QRect render(StrokeSurface &image, const BrushSettings &settings, const QColor &color,
                        const std::vector<StrokeSample> &samples, quint64 seed = 0, TileSnapshot *before = nullptr);
    static QRect render(QImage &image, const BrushSettings &settings, const QColor &color,
                        const std::vector<StrokeSample> &samples, quint64 seed = 0, TileSnapshot *before = nullptr)
    {
        DenseSurface surface(image);
        return render(surface, settings, color, samples, seed, before);
    }
    static QRect render(TiledImage &image, const BrushSettings &settings, const QColor &color,
                        const std::vector<StrokeSample> &samples, quint64 seed = 0, TileSnapshot *before = nullptr)
    {
        TiledSurface surface(image);
        return render(surface, settings, color, samples, seed, before);
    }

    // The parameters of one dab (StrokeContext.h).
    using DabParams = ::DabParams;

private:
    void acceptInput(StrokeSample sample);
    void annotate(StrokeSample &sample);
    // Spaces the smoothed samples into dabs, adds airbrush dabs up to nowMs (< 0: none) and
    // renders them; the caller flushes the deposit.
    void process(StrokeSurface &image, TileSnapshot &before, double nowMs, QRect &dirty);
    void addTimeDabs(double nowMs);
    void updateHeading(double direction, double stepFraction);
    bool needsHeading(const StrokeSample &sample) const;
    SensorReadings readingsFor(const StrokeSample &sample, qint64 index) const;
    double diameterAt(const StrokeSample &sample);
    double spacingFactorAt(const StrokeSample &sample);
    double taperFactor(double distance) const;
    double random(qint64 index, quint64 salt) const { return m_ctx.random(index, salt); }
    DabParams paramsFor(const Dab &dab) const;
    QRgb dabColor(const SensorReadings &readings, qint64 index) const;
    void renderDab(StrokeSurface &image, const DabParams &dab, TileSnapshot &before, QRect &dirty);
    // Renders a dab placed by the pipeline and, for the live end taper, records it.
    void renderPlaced(StrokeSurface &image, const Dab &dab, bool timed, TileSnapshot &before, QRect &dirty);
    void applyLiveTaper(StrokeSurface &image, TileSnapshot &before, QRect &dirty);
    void adaptSmoothing(const StrokeSample &sample);
    void flushDeposit(StrokeSurface &image, const TileSnapshot &before);

    StrokeContext m_ctx; // settings, colour, seed and what every deposit strategy shares
    std::unique_ptr<BrushTips::Tip> m_importedTip;
    std::unique_ptr<StrokeDeposit> m_deposit;
    double m_hue = 0, m_saturation = 0, m_lightness = 0; // of the stroke colour, for per-dab colour changes
    double m_strokeSize = 1.0; // per-stroke size factor
    double m_minimumDiameter = 1.0;
    CompiledDynamics m_dyn[DynamicTargetCount];
    bool m_anyDynamics = false;

    StrokeSmoother m_smoother;
    StrokeCurve m_curve;
    std::vector<StrokeSample> m_unbent; // smoother output before curve interpolation
    double m_baseRadius = 0;            // the smoothing string at rest
    // Speed-adaptive smoothing: raw input speed, px/ms, smoothed.
    double m_rawSpeed = 0;
    bool m_rawSpeedKnown = false;
    QPointF m_rawPos;
    double m_rawTimeMs = -1;
    // Corner preservation through the smoother: the last two raw input positions.
    QPointF m_cornerPrev, m_cornerLast;
    int m_cornerCount = 0;
    // Live end taper: every placed dab and the path, so the tail can be drawn again at pen-up.
    struct Record
    {
        Dab dab;
        DabParams params;
        QRect rect;
        QPointF heading;
        bool headingKnown = false;
        bool timed = false;
    };
    bool m_liveTaper = false;
    std::vector<Record> m_records;
    std::vector<StrokeSample> m_path;
    StrokeSpacer m_spacer;
    std::vector<StrokeSample> m_smoothed;
    std::deque<StrokeSample> m_held; // end taper: samples within taperEnd of the newest one
    std::vector<Dab> m_dabs;
    std::vector<bool> m_timed; // per entry of m_dabs: placed by elapsed time
    bool m_started = false;
    bool m_spacerStarted = false;
    bool m_finishing = false;
    double m_totalLength = -1; // known at finish(); < 0 before

    // Input filters.
    StrokeSample m_lastAccepted;
    StrokeSample m_pending; // a move the jitter filter held back
    bool m_hasPending = false;
    double m_pressure = 1;
    double m_pressureTimeMs = -1;
    bool m_pressureStarted = false;

    // Path sensors.
    bool m_annotated = false;
    QPointF m_prevPos;
    double m_pathDistance = 0;
    double m_direction = 0;
    bool m_hasDirection = false;
    double m_speed = 0;
    bool m_speedKnown = false;
    double m_acceleration = 0;
    double m_speedRefTimeMs = -1;
    double m_speedRefDistance = 0;
    double m_startTimeMs = -1;
    qint64 m_sizeIndex = 0; // dabs sized so far (the size and spacing callbacks)

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
};

#pragma once

#include "BrushSettings.h"
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
//     -> stamp (tip, spray, hatching) into stroke coverage S (Paint, Erase),
//        or carried-patch rendering (Smudge, WetPaint)
//     -> composite once per pixel per call, through the selection.
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
    static constexpr double SmallTipRadius = 3.0;
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
    QRect begin(QImage &image, const StrokeSample &sample, TileSnapshot &before);
    QRect moveTo(QImage &image, const StrokeSample &sample, TileSnapshot &before);
    // Airbrush: the pen is held at the same place until timeMs. No effect for other brushes.
    QRect advanceTime(QImage &image, double timeMs, TileSnapshot &before);
    // Pen up: completes the smoothed line to the last input position and draws the held-back end
    // taper. A stroke that never moved is a dot with one full pass of paint.
    QRect finish(QImage &image, TileSnapshot &before);

    const BrushSettings &settings() const { return m_settings; }
    bool usesTime() const;
    qint64 dabCount() const { return m_dabCount; }
    int coverageTileCount() const { return int(m_coverage.size()); }
    // Memory held by the stroke itself (coverage and colour tiles, patch), not the undo snapshot.
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
        double strength = 0; // smudge: strength; wet paint: attack
        double roundness = 1;
        bool flipX = false;
        bool flipY = false;
        double stepFraction = 0.15; // step after this dab / diameter
        double textureStrength = 0;
        double distance = 0;  // along the stroke, px (wet paint load)
        QRgb color = 0;       // opaque stroke colour of this dab
        qint64 index = 0;     // render index, for per-dab randomness
    };

private:
    using CoverageTile = std::vector<quint16>;
    using ColorTile = std::vector<quint32>;

    void acceptInput(StrokeSample sample);
    void annotate(StrokeSample &sample);
    // Spaces the smoothed samples into dabs, adds airbrush dabs up to nowMs (< 0: none) and
    // renders them; the caller composites.
    void process(QImage &image, TileSnapshot &before, double nowMs, QRect &dirty);
    void addTimeDabs(double nowMs);
    void updateHeading(double direction, double stepFraction);
    bool needsHeading(const StrokeSample &sample) const;
    SensorReadings readingsFor(const StrokeSample &sample, qint64 index) const;
    double diameterAt(const StrokeSample &sample);
    double spacingFactorAt(const StrokeSample &sample);
    double taperFactor(double distance) const;
    double random(qint64 index, quint64 salt) const;
    DabParams paramsFor(const Dab &dab) const;
    QRgb dabColor(const SensorReadings &readings, qint64 index) const;
    void renderDab(QImage &image, const DabParams &dab, TileSnapshot &before, QRect &dirty);
    void rasterize(const DabParams &dab, const QRect &rect);
    void rasterizeSpray(const DabParams &dab, const QRect &rect);
    void compositeTouched(QImage &image, const TileSnapshot &before);
    void composite(QImage &image, const QRect &rect, const TileSnapshot &before);
    void carry(QImage &image, const DabParams &dab, const QRect &rect, const TileSnapshot &before);
    void carryInto(QImage &target, const QPoint &origin, const DabParams &dab, const QRect &rect);
    void loadPatch(const QImage &image, const DabParams &dab);
    QRgb workPixel(const QImage &image, int x, int y) const;
    double grain(double x, double y) const;
    double passExponent(double stepFraction) const;
    CoverageTile &coverageTile(int column, int row);
    QRect dabRect(const DabParams &dab, const QImage &image) const;

    BrushSettings m_settings;
    quint32 m_color; // opaque RGB as ARGB32, after the per-stroke colour jitter
    double m_hue = 0, m_saturation = 0, m_lightness = 0; // of m_color, for per-dab colour changes
    quint64 m_seed;
    bool m_buildUp;
    bool m_coverageBlend; // Paint or Erase
    bool m_colorPerDab = false;
    bool m_textureEachDab = false;
    std::unique_ptr<BrushTips::Tip> m_importedTip;
    const BrushTips::Tip *m_tip = nullptr;
    const BrushTips::Texture *m_texture = nullptr;
    bool m_textureNearest = true; // scale 1, no rotation: nearest texel
    QPoint m_textureOffset;
    double m_textureCos = 1, m_textureSin = 0;
    double m_passLength = 1.0; // in diameters
    double m_strokeSize = 1.0; // per-stroke size factor
    double m_minimumDiameter = 1.0;
    QImage m_clip;
    CompiledDynamics m_dyn[DynamicTargetCount];

    StrokeSmoother m_smoother;
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

    std::unordered_map<qint64, CoverageTile> m_coverage;
    std::unordered_map<qint64, ColorTile> m_colors; // per-dab colour only
    std::unordered_map<qint64, QRect> m_touched;    // per tile: area to recomposite in this call

    // Smudge and wet paint: the carried paint, premultiplied RGBA as floats 0..255, centred on the dab.
    std::vector<float> m_patch;
    int m_patchSize = 0;
    bool m_patchLoaded = false;
    // Under a selection: the unclipped result, per 64 × 64 tile touched.
    std::unordered_map<qint64, QImage> m_work;
};

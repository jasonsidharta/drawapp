#include "StrokeEngine.h"

#include "BrushTips.h"
#include "TileSnapshot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr int Tile = StrokeEngine::Tile;
constexpr quint32 FullCoverage = 65535; // = 255 × 257, so whole 8-bit levels are exact

// Random streams: one salt per use, so changing one setting never reshuffles another's values.
enum Salt : quint64 {
    SaltAngle = 0xA9u, // milestone 6 value, kept so existing strokes render the same
    SaltSize = 0x5100u,
    SaltStrokeSize,
    SaltSpacing,
    SaltScatterAcross,
    SaltScatterAlong,
    SaltCount,
    SaltOpacity,
    SaltFlow,
    SaltRoundness,
    SaltFlipX,
    SaltFlipY,
    SaltHue,
    SaltSaturation,
    SaltLightness,
    SaltStrokeHue,
    SaltStrokeSaturation,
    SaltStrokeLightness,
    SaltDensity,
    SaltSprayAngle,
    SaltSprayRadius,
    SaltSpraySize,
    SaltWet,
    SaltTextureX,
    SaltTextureY,
};

qint64 tileKey(int column, int row)
{
    return (qint64(row) << 32) | quint32(column);
}

// x * y / 255, rounded, for 0..255 operands.
inline quint32 mul255(quint32 x, quint32 y)
{
    const quint32 t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

// 0 for a pen held upright (or one that reports no tilt), 1 at fullTilt degrees or more.
double tiltMagnitude(const StrokeSample &s, double fullTilt)
{
    if (!s.hasTilt)
        return 0.0;
    return std::min(1.0, std::hypot(s.tiltX, s.tiltY) / fullTilt);
}

double tiltFactor(double atFullTilt, double magnitude)
{
    return 1.0 + (atFullTilt - 1.0) * magnitude;
}

// Fixed thresholds in [0, 1) on a 64 × 64 tile, for dithering and stochastic rounding.
const std::array<float, Tile * Tile> &thresholds()
{
    static const std::array<float, Tile * Tile> table = [] {
        std::array<float, Tile * Tile> t{};
        for (int i = 0; i < Tile * Tile; ++i)
            t[size_t(i)] = float(BrushTips::hash01(i % Tile, i / Tile, 0xD17Eu)) * 0.999f;
        return t;
    }();
    return table;
}

inline float threshold(int x, int y)
{
    return thresholds()[size_t((y & (Tile - 1)) * Tile + (x & (Tile - 1)))];
}

// Bilinear sample of a Grayscale8 image (pixel centres at i + 0.5); 0 outside the image.
double sampleBounded(const QImage &image, double x, double y)
{
    x -= 0.5;
    y -= 0.5;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    const int w = image.width(), h = image.height();
    const uchar *bits = image.constBits();
    const qsizetype bpl = image.bytesPerLine();
    auto at = [&](int ix, int iy) -> double {
        return (ix < 0 || iy < 0 || ix >= w || iy >= h) ? 0.0 : double(bits[iy * bpl + ix]);
    };
    const double a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
    const double b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
    return (a + (b - a) * fy) / 255.0;
}

// Bilinear sample of a tiling Grayscale8 image.
double sampleWrapped(const QImage &image, double x, double y)
{
    x -= 0.5;
    y -= 0.5;
    const int w = image.width(), h = image.height();
    const double fx0 = std::floor(x), fy0 = std::floor(y);
    // Keep the lattice index small so huge offsets cannot overflow int.
    const int x0 = int(std::fmod(fx0, double(w))), y0 = int(std::fmod(fy0, double(h)));
    const double fx = x - fx0, fy = y - fy0;
    const int xa = ((x0 % w) + w) % w, ya = ((y0 % h) + h) % h;
    const int xb = (xa + 1) % w, yb = (ya + 1) % h;
    const uchar *ra = image.constScanLine(ya), *rb = image.constScanLine(yb);
    const double a = ra[xa] + (ra[xb] - ra[xa]) * fx;
    const double b = rb[xa] + (rb[xb] - rb[xa]) * fx;
    return (a + (b - a) * fy) / 255.0;
}

// The footprint of one dab: tip value 0..1 at an offset from the dab centre.
struct DabShape
{
    double radius;
    double cosA, sinA;
    double invRoundness;
    double hardness;
    double flipU, flipW;
    bool small;
    const QImage *mip = nullptr; // sampled tips
    double mipScale = 1;         // tip pixels per document pixel

    DabShape(const StrokeEngine::DabParams &dab, const BrushSettings &s, const BrushTips::Tip *tip)
        : radius(dab.diameter / 2.0)
        , cosA(std::cos(dab.angle))
        , sinA(std::sin(dab.angle))
        , invRoundness(1.0 / std::max(0.05, dab.roundness))
        , hardness(s.hardness)
        , flipU(dab.flipX ? -1.0 : 1.0)
        , flipW(dab.flipY ? -1.0 : 1.0)
        , small(radius < StrokeEngine::SmallTipRadius)
    {
        if (tip && !tip->mips.empty()) {
            // The smallest level still at least as large as the dab, so it is only ever magnified
            // slightly or reduced by its own mip filter.
            size_t level = 0;
            while (level + 1 < tip->mips.size() && tip->mips[level + 1].width() >= dab.diameter)
                ++level;
            mip = &tip->mips[level];
            mipScale = mip->width() / dab.diameter;
        }
    }

    // Round tip without edge anti-aliasing: hard disc times the soft falloff.
    double roundPoint(double u, double w) const
    {
        const double d = std::hypot(u, w);
        if (d > radius)
            return 0.0;
        return falloff(d / radius);
    }

    // Smooth falloff from the hard core (hardness) to the rim: (1 − t²)², flat at both ends so
    // neither the core nor the rim shows a crease.
    double falloff(double x) const
    {
        if (hardness >= 1.0 || x <= hardness)
            return 1.0;
        const double t = (x - hardness) / (1.0 - hardness);
        if (t >= 1.0)
            return 0.0;
        const double q = 1.0 - t * t;
        return q * q;
    }

    double value(double dx, double dy) const
    {
        const double u = (dx * cosA + dy * sinA) * flipU;
        const double w = (-dx * sinA + dy * cosA) * invRoundness * flipW;
        if (mip) {
            const double half = mip->width() / 2.0;
            return sampleBounded(*mip, half + u * mipScale, half + w * mipScale);
        }
        if (small) {
            // Area coverage from a 4 × 4 grid of points inside the pixel.
            double sum = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const double ox = (sx + 0.5) / 4.0 - 0.5, oy = (sy + 0.5) / 4.0 - 0.5;
                    const double su = (dx + ox) * cosA + (dy + oy) * sinA;
                    const double sw = (-(dx + ox) * sinA + (dy + oy) * cosA) * invRoundness;
                    sum += roundPoint(su, sw);
                }
            return sum / 16.0;
        }
        // The pixel's coverage by the disc, estimated from its distance to the edge.
        const double d = std::hypot(u, w);
        const double edge = std::clamp(radius + 0.5 - d, 0.0, 1.0);
        if (edge <= 0.0)
            return 0.0;
        return std::min(edge, falloff(std::min(1.0, d / radius)));
    }
};

// (c·s + d·add) / (s + add) per channel of two opaque colours.
inline quint32 mixColor(quint32 c, quint32 s, quint32 d, quint32 add)
{
    const quint32 total = s + add;
    auto ch = [&](int shift) {
        return ((((c >> shift) & 0xFF) * s + ((d >> shift) & 0xFF) * add + total / 2) / total) << shift;
    };
    return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

} // namespace

StrokeEngine::StrokeEngine(const BrushSettings &settings, const QColor &color, quint64 seed, qreal smoothingRadius)
    : m_settings(settings.sanitized())
    , m_color(qRgb(color.red(), color.green(), color.blue()))
    , m_seed(seed)
    , m_buildUp(m_settings.accumulation == Accumulation::BuildUp || m_settings.airbrush)
    , m_coverageBlend(m_settings.blend == BrushBlend::Paint || m_settings.blend == BrushBlend::Erase)
    , m_smoother(smoothingRadius)
    , m_spacer([this](const StrokeSample &s) { return diameterAt(s); }, m_settings.spacing)
{
    const BrushSettings &s = m_settings;
    for (int i = 0; i < DynamicTargetCount; ++i) {
        m_dyn[i] = CompiledDynamics(s.dynamics[size_t(i)], DynamicTarget(i));
        m_anyDynamics = m_anyDynamics || m_dyn[i].isActive();
    }
    if (m_dyn[int(DynamicTarget::Spacing)].isActive() || s.spacingJitter > 0)
        m_spacer.setSpacingFunction([this](const StrokeSample &sample, qreal) { return spacingFactorAt(sample); });
    if (!m_coverageBlend)
        m_settings.stamp = BrushStamp::Tip; // spray and hatching are coverage stamps

    if (!s.tipImage.isNull()) {
        m_importedTip = std::make_unique<BrushTips::Tip>(BrushTips::makeTip(s.tipImage));
        m_tip = m_importedTip.get();
    } else if (!s.tip.isEmpty()) {
        m_tip = BrushTips::tip(s.tip); // unknown id: round tip
    }
    m_passLength = m_tip ? m_tip->passLength : BrushTips::roundPassLength(s.hardness);

    const bool textureDynamics = m_dyn[int(DynamicTarget::TextureStrength)].isActive();
    if (!s.texture.isEmpty() && s.textureStrength > 0)
        m_texture = BrushTips::texture(s.texture);
    m_textureEachDab = m_texture && m_coverageBlend && (s.textureEachDab || textureDynamics);
    m_textureNearest = s.textureScale == 1.0 && s.textureRotation == 0.0 && !(m_textureEachDab && s.textureMoving);
    m_textureCos = std::cos(s.textureRotation * Pi / 180.0);
    m_textureSin = std::sin(s.textureRotation * Pi / 180.0);
    if (s.textureRandomOffset)
        m_textureOffset = QPoint(int(random(-1, SaltTextureX) * 4096), int(random(-1, SaltTextureY) * 4096));

    m_strokeSize = 1.0 - s.strokeSizeJitter * random(-1, SaltStrokeSize);
    m_minimumDiameter = std::min(s.minimumSize, s.size);

    // Per-stroke colour jitter changes the colour once; per-dab colour needs colour tiles.
    QColor c = QColor::fromRgb(m_color);
    float h, sat, l;
    c.getHslF(&h, &sat, &l);
    m_hue = h < 0 ? 0.0 : h * 360.0;
    m_saturation = sat;
    m_lightness = l;
    if (s.strokeHueJitter > 0 || s.strokeSaturationJitter > 0 || s.strokeLightnessJitter > 0) {
        m_hue += (random(-1, SaltStrokeHue) * 2 - 1) * s.strokeHueJitter;
        m_saturation = std::clamp(m_saturation + (random(-1, SaltStrokeSaturation) * 2 - 1) * s.strokeSaturationJitter, 0.0, 1.0);
        m_lightness = std::clamp(m_lightness + (random(-1, SaltStrokeLightness) * 2 - 1) * s.strokeLightnessJitter, 0.0, 1.0);
        m_hue = std::fmod(std::fmod(m_hue, 360.0) + 360.0, 360.0);
        const QColor j = QColor::fromHslF(float(m_hue / 360.0), float(m_saturation), float(m_lightness));
        m_color = qRgb(j.red(), j.green(), j.blue());
    }
    m_colorPerDab = (s.blend == BrushBlend::Paint || s.blend == BrushBlend::WetPaint) && s.hasColorDynamics();
}

StrokeEngine::~StrokeEngine() = default;

double StrokeEngine::random(qint64 index, quint64 salt) const
{
    return BrushTips::hash01(qint64(m_seed), index, salt);
}

double StrokeEngine::passExponent(double stepFraction) const
{
    // Dabs per pass along the tip's densest line = pass length / step; each dab gets the share
    // of the per-pass amount that makes the pass add up to it.
    return std::min(1.0, stepFraction / m_passLength);
}

void StrokeEngine::setClipMask(const QImage &mask)
{
    m_clip = mask.isNull() ? QImage() : mask.convertToFormat(QImage::Format_Grayscale8);
}

bool StrokeEngine::usesTime() const
{
    return m_settings.airbrush && m_coverageBlend;
}

qsizetype StrokeEngine::bufferBytes() const
{
    return qsizetype(m_coverage.size()) * Tile * Tile * qsizetype(sizeof(quint16))
         + qsizetype(m_colors.size()) * Tile * Tile * qsizetype(sizeof(quint32))
         + qsizetype(m_patch.size() * sizeof(float)) + qsizetype(m_work.size()) * Tile * Tile * 4;
}

// --- input ---

void StrokeEngine::acceptInput(StrokeSample s)
{
    if (!std::isfinite(s.pressure))
        s.pressure = 1.0;
    s.pressure = std::clamp(s.pressure, 0.0, 1.0);
    if (!s.hasPressure)
        s.pressure = 1.0;
    if (m_settings.pressureSmoothing > 0 && s.hasPressure) {
        if (!m_pressureStarted) {
            m_pressure = s.pressure;
            m_pressureTimeMs = s.timeMs;
            m_pressureStarted = true;
        } else {
            double a;
            if (s.timeMs >= 0 && m_pressureTimeMs >= 0) {
                const double dt = s.timeMs - m_pressureTimeMs;
                a = dt > 0 ? 1.0 - std::exp(-dt / (m_settings.pressureSmoothing * PressureTimeConstantMs)) : 0.0;
                if (dt > 0)
                    m_pressureTimeMs = s.timeMs;
            } else {
                a = 1.0 - 0.9 * m_settings.pressureSmoothing; // no event times: per event
            }
            m_pressure += (s.pressure - m_pressure) * a;
        }
        s.pressure = m_pressure;
    }
    m_lastAccepted = s;
    if (!m_started) {
        m_started = true;
        m_smoother.begin(s, m_smoothed);
    } else {
        m_smoother.moveTo(s, m_smoothed);
    }
}

void StrokeEngine::annotate(StrokeSample &s)
{
    if (!m_annotated) {
        m_annotated = true;
        m_prevPos = s.pos;
        m_startTimeMs = s.timeMs;
        m_speedRefTimeMs = s.timeMs;
    } else {
        const double step = std::hypot(s.pos.x() - m_prevPos.x(), s.pos.y() - m_prevPos.y());
        if (std::isfinite(step) && step > 1e-9) {
            m_pathDistance += step;
            m_direction = std::atan2(s.pos.y() - m_prevPos.y(), s.pos.x() - m_prevPos.x());
            m_hasDirection = true;
            m_speedRefDistance += step;
        }
        m_prevPos = s.pos;
        if (s.timeMs >= 0 && m_speedRefTimeMs < 0)
            m_speedRefTimeMs = s.timeMs;
        if (m_startTimeMs < 0)
            m_startTimeMs = s.timeMs;
        // Speed from distance over time since the last sample with a later time: duplicate and
        // out-of-order events (dt ≤ 0) keep the previous speed; speeds are capped and smoothed.
        if (s.timeMs >= 0 && m_speedRefTimeMs >= 0) {
            const double dt = s.timeMs - m_speedRefTimeMs;
            if (dt > 0) {
                const double instant = std::min(MaxSpeed, m_speedRefDistance / dt);
                const double a = 1.0 - std::exp(-dt / SpeedTimeConstantMs);
                const double speed = m_speedKnown ? m_speed + (instant - m_speed) * a : instant;
                const double accel = m_speedKnown ? (speed - m_speed) / dt : 0.0;
                m_acceleration = std::clamp(m_acceleration + (accel - m_acceleration) * a, -MaxAcceleration, MaxAcceleration);
                m_speed = speed;
                m_speedKnown = true;
                m_speedRefTimeMs = s.timeMs;
                m_speedRefDistance = 0;
            }
        }
    }
    s.distance = m_pathDistance;
    s.speed = m_speedKnown ? m_speed : -1.0;
    s.acceleration = m_acceleration;
    s.direction = m_direction;
    s.hasDirection = m_hasDirection;
}

QRect StrokeEngine::begin(StrokeSurface &image, const StrokeSample &sample, TileSnapshot &before)
{
    QRect dirty;
    m_started = false;
    m_smoothed.clear();
    acceptInput(sample);
    process(image, before, sample.timeMs, dirty);
    compositeTouched(image, before);
    return dirty;
}

QRect StrokeEngine::moveTo(StrokeSurface &image, const StrokeSample &sample, TileSnapshot &before)
{
    QRect dirty;
    if (!m_started)
        return dirty;
    m_smoothed.clear();
    const double filter = m_settings.jitterFilter;
    if (filter > 0 && std::hypot(sample.pos.x() - m_lastAccepted.pos.x(), sample.pos.y() - m_lastAccepted.pos.y()) < filter) {
        // Too small a move: hold it back (the end point is still drawn at finish).
        m_pending = sample;
        m_hasPending = true;
    } else {
        m_hasPending = false;
        acceptInput(sample);
    }
    process(image, before, sample.timeMs, dirty);
    compositeTouched(image, before);
    return dirty;
}

QRect StrokeEngine::advanceTime(StrokeSurface &image, double timeMs, TileSnapshot &before)
{
    QRect dirty;
    if (!m_started || !usesTime())
        return dirty;
    m_smoothed.clear();
    process(image, before, timeMs, dirty);
    compositeTouched(image, before);
    return dirty;
}

QRect StrokeEngine::finish(StrokeSurface &image, TileSnapshot &before)
{
    QRect dirty;
    if (!m_started)
        return dirty;
    m_smoothed.clear();
    if (m_hasPending) {
        m_hasPending = false;
        acceptInput(m_pending);
    }
    if (m_settings.smoothingFinish)
        m_smoother.finish(m_smoothed);
    m_finishing = true;
    process(image, before, -1, dirty);
    if (m_firstDabPending) {
        // Never moved far enough to have a direction: a dot at the brush's own angle.
        m_firstDabPending = false;
        renderDab(image, paramsFor(m_firstDab), before, dirty);
    }
    // A dot gets one full pass of paint, like any point of a moving stroke, instead of the single
    // spacing-normalised dab (which for a low flow would be nearly invisible).
    if (m_buildUp && m_coverageBlend && m_distanceDabCount == 1 && m_timeDabCount == 0 && m_firstParams.flow < 1.0) {
        DabParams topUp = m_firstParams;
        topUp.flow = 1.0 - (1.0 - m_firstParams.passFlow) / (1.0 - m_firstParams.flow);
        topUp.index = m_dabCount;
        if (topUp.flow > 0)
            renderDab(image, topUp, before, dirty);
    }
    compositeTouched(image, before);
    m_started = false;
    return dirty;
}

QRect StrokeEngine::render(StrokeSurface &image, const BrushSettings &settings, const QColor &color,
                           const std::vector<StrokeSample> &samples, quint64 seed, TileSnapshot *before)
{
    if (samples.empty())
        return {};
    TileSnapshot local;
    TileSnapshot &snapshot = before ? *before : local;
    StrokeEngine engine(settings, color, seed, 0);
    QRect dirty = engine.begin(image, samples.front(), snapshot);
    for (size_t i = 1; i < samples.size(); ++i)
        dirty |= engine.moveTo(image, samples[i], snapshot);
    dirty |= engine.finish(image, snapshot);
    return dirty;
}

// --- dabs ---

void StrokeEngine::process(StrokeSurface &image, TileSnapshot &before, double nowMs, QRect &dirty)
{
    m_dabs.clear();
    for (StrokeSample &s : m_smoothed) {
        annotate(s);
        m_held.push_back(s);
    }
    m_smoothed.clear();
    if (m_finishing)
        m_totalLength = m_pathDistance;
    // End taper: the last taperEnd px wait until the end of the stroke is known.
    const double newest = m_held.empty() ? 0.0 : m_held.back().distance;
    while (!m_held.empty()) {
        if (!m_finishing && m_settings.taperEnd > 0 && newest - m_held.front().distance <= m_settings.taperEnd)
            break;
        const StrokeSample s = m_held.front();
        m_held.pop_front();
        if (!m_spacerStarted) {
            m_spacer.begin(s, m_dabs);
            m_spacerStarted = true;
        } else {
            m_spacer.moveTo(s, m_dabs);
        }
    }
    m_timed.assign(m_dabs.size(), false);
    // Distance dabs restart the airbrush clock; elapsed time adds dabs only where the pen lingers.
    for (const Dab &d : m_dabs)
        if (d.sample.timeMs >= 0)
            m_lastDabTimeMs = d.sample.timeMs;
    addTimeDabs(nowMs);

    const BrushSettings &st = m_settings;
    for (size_t i = 0; i < m_dabs.size(); ++i) {
        const Dab &dab = m_dabs[i];
        int copies = 1;
        if (m_timed[i]) {
            ++m_timeDabCount;
        } else {
            if (m_distanceDabCount > 0)
                updateHeading(dab.direction, m_spacer.stepFraction(dab.diameter));
            ++m_distanceDabCount;
            if (m_distanceDabCount == 1 && needsHeading(dab.sample)) {
                m_firstDab = dab;
                m_firstDabPending = true;
                continue;
            }
            copies = st.count;
            if (st.countJitter > 0 && copies > 1) {
                const int drop = int(random(m_distanceDabCount, SaltCount) * (st.countJitter * copies + 1.0));
                copies = std::max(1, copies - std::min(drop, copies - 1));
            }
        }
        if (m_firstDabPending) {
            m_firstDabPending = false;
            renderDab(image, paramsFor(m_firstDab), before, dirty);
        }
        for (int c = 0; c < copies; ++c)
            renderDab(image, paramsFor(dab), before, dirty);
    }
}

void StrokeEngine::addTimeDabs(double nowMs)
{
    if (!usesTime() || nowMs < 0 || !m_spacerStarted)
        return;
    if (m_lastDabTimeMs < 0) {
        m_lastDabTimeMs = nowMs;
        return;
    }
    const double interval = 1000.0 / m_settings.airbrushRate;
    int added = 0;
    while (nowMs - m_lastDabTimeMs >= interval) {
        if (added == MaxTimeDabsPerCall) {
            m_lastDabTimeMs = nowMs; // drop the rest of a long stall rather than flooding
            break;
        }
        m_lastDabTimeMs += interval;
        StrokeSample at = m_spacer.last();
        at.timeMs = m_lastDabTimeMs;
        const double direction = m_headingKnown ? std::atan2(m_heading.y(), m_heading.x()) : 0.0;
        m_dabs.push_back({at.pos, m_spacer.diameterFor(at), at, direction, 0});
        m_timed.push_back(true);
        ++added;
    }
    if (added > 0)
        m_spacer.restartDistance();
}

void StrokeEngine::updateHeading(double direction, double stepFraction)
{
    const QPointF dir(std::cos(direction), std::sin(direction));
    if (!m_headingKnown) {
        m_heading = dir;
        m_headingKnown = true;
        return;
    }
    // Averaged over about half a dab's length of travel.
    const double a = std::clamp(2.0 * stepFraction, 0.05, 1.0);
    const QPointF h = m_heading * (1.0 - a) + dir * a;
    const double len = std::hypot(h.x(), h.y());
    m_heading = len > 1e-6 ? h / len : dir;
}

bool StrokeEngine::needsHeading(const StrokeSample &sample) const
{
    return m_settings.followDirection
        && !(sample.hasTilt && std::hypot(sample.tiltX, sample.tiltY) >= m_settings.tiltAzimuthThreshold);
}

SensorReadings StrokeEngine::readingsFor(const StrokeSample &s, qint64 index) const
{
    SensorReadings r;
    r.seed = m_seed;
    r.dabIndex = index;
    if (s.hasPressure)
        r.set(Sensor::Pressure, std::clamp(s.pressure, 0.0, 1.0));
    if (s.hasTilt) {
        // Scaled so that the settings' full tilt angle reads as the sensors' full tilt.
        const double k = 60.0 / m_settings.fullTiltAngle;
        r.set(Sensor::TiltX, s.tiltX * k);
        r.set(Sensor::TiltY, s.tiltY * k);
        r.set(Sensor::TiltMagnitude, std::hypot(s.tiltX, s.tiltY) * k);
        r.set(Sensor::TiltDirection, std::atan2(s.tiltY, s.tiltX));
    }
    if (s.hasDirection)
        r.set(Sensor::Direction, s.direction);
    if (s.speed >= 0) {
        r.set(Sensor::Speed, s.speed);
        r.set(Sensor::Acceleration, s.acceleration);
    }
    r.set(Sensor::Distance, s.distance);
    r.set(Sensor::Fade, s.distance);
    if (s.timeMs >= 0 && m_startTimeMs >= 0)
        r.set(Sensor::Time, std::max(0.0, s.timeMs - m_startTimeMs));
    return r;
}

double StrokeEngine::taperFactor(double distance) const
{
    const BrushSettings &s = m_settings;
    double f = 1.0;
    if (s.taperStart > 0 && distance < s.taperStart) {
        const double t = std::max(0.0, distance) / s.taperStart;
        f *= s.taperStartSize + (1.0 - s.taperStartSize) * std::pow(t, s.taperShape);
    }
    if (s.taperEnd > 0 && m_totalLength > 0) {
        const double remaining = m_totalLength - distance;
        if (remaining < s.taperEnd) {
            const double t = std::max(0.0, remaining) / s.taperEnd;
            f *= s.taperEndSize + (1.0 - s.taperEndSize) * std::pow(t, s.taperShape);
        }
    }
    return f;
}

double StrokeEngine::diameterAt(const StrokeSample &s)
{
    const qint64 index = m_sizeIndex++;
    const BrushSettings &st = m_settings;
    const double pressure = s.hasPressure ? s.pressure : 1.0;
    double d = st.size * m_strokeSize * st.pressureSize.apply(pressure)
             * tiltFactor(st.tiltSize, tiltMagnitude(s, st.fullTiltAngle));
    double v;
    if (m_anyDynamics && m_dyn[int(DynamicTarget::Size)].evaluate(readingsFor(s, index), &v))
        d *= v;
    if (st.sizeJitter > 0)
        d *= 1.0 - st.sizeJitter * random(index, SaltSize);
    d *= taperFactor(s.distance);
    return std::max(m_minimumDiameter, d);
}

double StrokeEngine::spacingFactorAt(const StrokeSample &s)
{
    const qint64 index = m_sizeIndex - 1; // the dab just sized
    double factor = 1.0;
    double v;
    if (m_anyDynamics && m_dyn[int(DynamicTarget::Spacing)].evaluate(readingsFor(s, index), &v))
        factor *= v;
    if (m_settings.spacingJitter > 0)
        factor *= 1.0 + m_settings.spacingJitter * (random(index, SaltSpacing) * 2.0 - 1.0);
    return std::max(0.0, factor);
}

StrokeEngine::DabParams StrokeEngine::paramsFor(const Dab &dab) const
{
    const BrushSettings &st = m_settings;
    const StrokeSample &s = dab.sample;
    const qint64 index = m_dabCount; // the index renderDab will give this dab
    // Readings are only needed when a parameter follows sensors (evaluate() is false otherwise).
    const SensorReadings r = m_anyDynamics ? readingsFor(s, index) : SensorReadings();
    const double tilt = tiltMagnitude(s, st.fullTiltAngle);
    const double pressure = s.hasPressure ? s.pressure : 1.0;
    double v;

    DabParams d;
    d.index = index;
    d.center = dab.center;
    d.diameter = dab.diameter;
    d.distance = s.distance;
    if (st.scatter > 0 && st.blend != BrushBlend::Smudge) {
        double amount = st.scatter;
        if (m_dyn[int(DynamicTarget::Scatter)].evaluate(r, &v))
            amount *= v;
        const double across = (random(index, SaltScatterAcross) * 2 - 1) * amount * dab.diameter;
        const double along =
            st.scatterBothAxes ? (random(index, SaltScatterAlong) * 2 - 1) * amount * dab.diameter : 0.0;
        const QPointF dir(std::cos(dab.direction), std::sin(dab.direction));
        d.center += dir * along + QPointF(-dir.y(), dir.x()) * across;
    }

    double strength = st.pressureOpacity.apply(pressure) * tiltFactor(st.tiltOpacity, tilt);
    if (m_dyn[int(DynamicTarget::Opacity)].evaluate(r, &v))
        strength *= v;
    if (st.opacityJitter > 0)
        strength *= 1.0 - st.opacityJitter * random(index, SaltOpacity);
    if (st.taperOpacity > 0)
        strength *= 1.0 - st.taperOpacity * (1.0 - taperFactor(s.distance));
    d.ceiling = std::clamp(std::round(st.opacity * strength * 255.0), 0.0, 255.0) / 255.0;
    double carried = st.blend == BrushBlend::WetPaint ? st.wetAttack : st.smudgeStrength;
    if (m_dyn[int(DynamicTarget::SmudgeStrength)].evaluate(r, &v))
        carried *= v;
    d.strength = std::clamp(carried * strength, 0.0, 1.0);

    double passFlow = st.flow * st.pressureFlow.apply(pressure);
    if (m_dyn[int(DynamicTarget::Flow)].evaluate(r, &v))
        passFlow *= v;
    if (st.flowJitter > 0)
        passFlow *= 1.0 - st.flowJitter * random(index, SaltFlow);
    d.passFlow = std::clamp(passFlow, 0.0, 1.0);
    d.stepFraction = dab.step > 0 ? dab.step / std::max(StrokeSpacer::MinDiameter, dab.diameter)
                                  : m_spacer.stepFraction(dab.diameter);
    d.flow = m_buildUp ? 1.0 - std::pow(1.0 - d.passFlow, passExponent(d.stepFraction)) : 1.0;

    double angle = st.angle * Pi / 180.0;
    if (st.followDirection) {
        if (!needsHeading(s))
            angle += std::atan2(s.tiltY, s.tiltX); // pen azimuth
        else if (m_headingKnown)
            angle += std::atan2(m_heading.y(), m_heading.x());
    }
    if (m_dyn[int(DynamicTarget::Rotation)].evaluate(r, &v))
        angle += v * 2.0 * Pi;
    if (st.angleJitter > 0)
        angle += (BrushTips::hash01(qint64(m_seed), index, SaltAngle) * 2.0 - 1.0) * st.angleJitter * Pi / 180.0;
    d.angle = angle;

    double roundness = st.roundness;
    if (m_dyn[int(DynamicTarget::Roundness)].evaluate(r, &v))
        roundness *= v;
    if (st.roundnessJitter > 0)
        roundness *= 1.0 - st.roundnessJitter * random(index, SaltRoundness);
    d.roundness = std::max(0.05, roundness);
    d.flipX = st.flipX != (st.randomFlipX && random(index, SaltFlipX) < 0.5);
    d.flipY = st.flipY != (st.randomFlipY && random(index, SaltFlipY) < 0.5);

    d.textureStrength = st.textureStrength;
    if (m_dyn[int(DynamicTarget::TextureStrength)].evaluate(r, &v))
        d.textureStrength *= v;
    d.color = dabColor(r, index);
    return d;
}

QRgb StrokeEngine::dabColor(const SensorReadings &r, qint64 index) const
{
    if (!m_colorPerDab)
        return m_color;
    const BrushSettings &st = m_settings;
    double h = m_hue, s = m_saturation, l = m_lightness;
    if (st.hueJitter > 0)
        h += (random(index, SaltHue) * 2 - 1) * st.hueJitter;
    if (st.saturationJitter > 0)
        s += (random(index, SaltSaturation) * 2 - 1) * st.saturationJitter;
    if (st.lightnessJitter > 0)
        l += (random(index, SaltLightness) * 2 - 1) * st.lightnessJitter;
    double v;
    if (m_dyn[int(DynamicTarget::Hue)].evaluate(r, &v))
        h += v * 360.0;
    if (m_dyn[int(DynamicTarget::Saturation)].evaluate(r, &v))
        s *= v;
    if (m_dyn[int(DynamicTarget::Lightness)].evaluate(r, &v))
        l *= v;
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0);
    const QColor c = QColor::fromHslF(float(h / 360.0), float(std::clamp(s, 0.0, 1.0)), float(std::clamp(l, 0.0, 1.0)));
    return qRgb(c.red(), c.green(), c.blue());
}

QRect StrokeEngine::dabRect(const DabParams &dab, const StrokeSurface &image) const
{
    // A sampled tip is a square that may be rotated; a round tip never extends past its radius.
    double r = dab.diameter / 2.0 * (m_tip ? 1.4143 : 1.0) + 1.0;
    if (m_settings.stamp == BrushStamp::Spray)
        r = dab.diameter / 2.0 + m_settings.sprayParticleSize / 2.0 + 2.0;
    return QRectF(dab.center.x() - r, dab.center.y() - r, 2 * r, 2 * r).toAlignedRect() & image.rect();
}

void StrokeEngine::renderDab(StrokeSurface &image, const DabParams &dab, TileSnapshot &before, QRect &dirty)
{
    if (m_dabCount == 0)
        m_firstParams = dab;
    ++m_dabCount;
    const QRect rect = dabRect(dab, image);
    if (rect.isEmpty())
        return;
    if (!m_coverageBlend) {
        // Smudge: the first dab only loads the patch; after that each dab changes pixels. Wet
        // paint deposits from the first dab on.
        if (m_patchLoaded || m_settings.blend == BrushBlend::WetPaint)
            image.capture(before, rect);
        carry(image, dab, rect, before);
        dirty |= rect;
        return;
    }
    if (dab.ceiling <= 0 || dab.flow <= 0)
        return;
    // Per dab, not the union: a long diagonal segment must not capture its whole bounding box.
    image.capture(before, rect);
    if (m_settings.stamp == BrushStamp::Spray)
        rasterizeSpray(dab, rect);
    else
        rasterize(dab, rect);
    for (int row = rect.top() / Tile; row <= rect.bottom() / Tile; ++row)
        for (int column = rect.left() / Tile; column <= rect.right() / Tile; ++column)
            m_touched[tileKey(column, row)] |= rect & QRect(column * Tile, row * Tile, Tile, Tile);
    dirty |= rect;
}

StrokeEngine::CoverageTile &StrokeEngine::coverageTile(int column, int row)
{
    const auto [it, inserted] = m_coverage.try_emplace(tileKey(column, row));
    if (inserted)
        it->second.assign(size_t(Tile * Tile), 0);
    return it->second;
}

double StrokeEngine::grain(double x, double y) const
{
    const BrushSettings &s = m_settings;
    const QImage &t = m_texture->image;
    double value;
    if (m_textureNearest) {
        const int ix = int(std::floor(x)) + m_textureOffset.x(), iy = int(std::floor(y)) + m_textureOffset.y();
        value = t.constScanLine(((iy % t.height()) + t.height()) % t.height())[((ix % t.width()) + t.width()) % t.width()]
              / 255.0;
    } else {
        double u = x + m_textureOffset.x(), w = y + m_textureOffset.y();
        if (s.textureRotation != 0.0) {
            const double ru = u * m_textureCos + w * m_textureSin;
            w = -u * m_textureSin + w * m_textureCos;
            u = ru;
        }
        value = sampleWrapped(t, u / s.textureScale, w / s.textureScale);
    }
    if (s.textureInvert)
        value = 1.0 - value;
    if (s.textureContrast != 1.0 || s.textureBrightness != 0.0)
        value = std::clamp((value - 0.5) * s.textureContrast + 0.5 + s.textureBrightness, 0.0, 1.0);
    return value;
}

void StrokeEngine::rasterize(const DabParams &dab, const QRect &rect)
{
    const BrushSettings &st = m_settings;
    const DabShape shape(dab, st, m_tip);
    const quint32 ceiling = quint32(std::lround(dab.ceiling * 255.0)) * 257u;
    const qint64 index = dab.index + 1; // the milestone 6 numbering of the rounding pattern
    const bool hatch = st.stamp == BrushStamp::Hatching;
    const double hatchNx = -std::sin(st.hatchAngle * Pi / 180.0), hatchNy = std::cos(st.hatchAngle * Pi / 180.0);
    const double hatchHalf = st.hatchThickness / 2.0, hatchPeriod = st.hatchSpacing;
    const double density = st.tipDensity;
    const bool texture = m_textureEachDab;
    const bool moving = texture && st.textureMoving;
    const double texCos = std::cos(dab.angle), texSin = std::sin(dab.angle);
    const bool subtract = st.textureMode == TextureMode::Subtract;
    if (density >= 1.0 && !hatch && !texture && !m_colorPerDab) {
        // The common case keeps the milestone 6 loop: nothing per pixel beyond the tip and the
        // accumulation rule (the general loop below measured up to ~15 % slower on large dabs).
        for (int row = rect.top() / Tile; row <= rect.bottom() / Tile; ++row) {
            for (int column = rect.left() / Tile; column <= rect.right() / Tile; ++column) {
                const QRect part = rect & QRect(column * Tile, row * Tile, Tile, Tile);
                CoverageTile &tile = coverageTile(column, row);
                const int ox = column * Tile;
                for (int y = part.top(); y <= part.bottom(); ++y) {
                    const double dy = y + 0.5 - dab.center.y();
                    quint16 *line = tile.data() + (y - row * Tile) * Tile;
                    for (int x = part.left(); x <= part.right(); ++x) {
                        const double v = shape.value(x + 0.5 - dab.center.x(), dy);
                        if (v <= 0.0)
                            continue;
                        quint16 &s = line[x - ox];
                        if (!m_buildUp) {
                            const quint32 target = quint32(std::lround(v * ceiling));
                            if (target > s)
                                s = quint16(target);
                        } else if (ceiling > s) {
                            const double inc = v * dab.flow * double(ceiling - s);
                            const quint32 step = quint32(inc + threshold(x + int(index * 29), y + int(index * 17)));
                            s = quint16(std::min<quint32>(ceiling, s + step));
                        }
                    }
                }
            }
        }
        return;
    }
    for (int row = rect.top() / Tile; row <= rect.bottom() / Tile; ++row) {
        for (int column = rect.left() / Tile; column <= rect.right() / Tile; ++column) {
            const QRect part = rect & QRect(column * Tile, row * Tile, Tile, Tile);
            CoverageTile &tile = coverageTile(column, row);
            ColorTile *colors = nullptr;
            if (m_colorPerDab) {
                auto [it, inserted] = m_colors.try_emplace(tileKey(column, row));
                if (inserted)
                    it->second.assign(size_t(Tile * Tile), 0);
                colors = &it->second;
            }
            const int ox = column * Tile;
            for (int y = part.top(); y <= part.bottom(); ++y) {
                const double dy = y + 0.5 - dab.center.y();
                const int lineStart = (y - row * Tile) * Tile;
                quint16 *line = tile.data() + lineStart;
                for (int x = part.left(); x <= part.right(); ++x) {
                    const double dx = x + 0.5 - dab.center.x();
                    double v = shape.value(dx, dy);
                    if (v <= 0.0)
                        continue;
                    if (density < 1.0 && BrushTips::hash01((qint64(y) << 24) ^ x, index, SaltDensity) >= density)
                        continue;
                    if (hatch) {
                        const double p = (x + 0.5) * hatchNx + (y + 0.5) * hatchNy;
                        const double m = std::abs(p - hatchPeriod * std::floor(p / hatchPeriod + 0.5));
                        double stripe = std::clamp(hatchHalf + 0.5 - m, 0.0, 1.0);
                        if (hatchHalf < 0.5)
                            stripe = std::min(stripe, 2.0 * hatchHalf);
                        v *= stripe;
                        if (v <= 0.0)
                            continue;
                    }
                    if (texture) {
                        const double t = moving ? grain(dx * texCos + dy * texSin, -dx * texSin + dy * texCos)
                                                : grain(x + 0.5, y + 0.5);
                        const double valley = dab.textureStrength * (1.0 - t);
                        v = subtract ? std::max(0.0, v - valley) : v * (1.0 - valley);
                        if (v <= 0.0)
                            continue;
                    }
                    quint16 &s = line[x - ox];
                    quint32 add = 0;
                    if (!m_buildUp) {
                        const quint32 target = quint32(std::lround(v * ceiling));
                        if (target > s)
                            add = target - s;
                    } else if (ceiling > s) {
                        // Stochastic rounding: unbiased, and the stroke still reaches the ceiling.
                        const double inc = v * dab.flow * double(ceiling - s);
                        const quint32 step = quint32(inc + threshold(x + int(index * 29), y + int(index * 17)));
                        add = std::min<quint32>(ceiling - s, step);
                    }
                    if (add == 0)
                        continue;
                    if (colors) {
                        quint32 &c = (*colors)[size_t(lineStart + x - ox)];
                        c = s == 0 ? dab.color : mixColor(c, s, dab.color, add);
                    }
                    s = quint16(s + add);
                }
            }
        }
    }
}

void StrokeEngine::rasterizeSpray(const DabParams &dab, const QRect &rect)
{
    const BrushSettings &st = m_settings;
    const quint32 ceiling = quint32(std::lround(dab.ceiling * 255.0)) * 257u;
    const int particles = std::max(1, int(std::lround(st.sprayDensity)));
    const double radius = dab.diameter / 2.0;
    const bool subtract = st.textureMode == TextureMode::Subtract;
    qint64 cachedKey = -1;
    CoverageTile *tile = nullptr;
    ColorTile *colors = nullptr;
    for (int i = 0; i < particles; ++i) {
        const qint64 key = dab.index * 4096 + i;
        const double a = random(key, SaltSprayAngle) * 2.0 * Pi;
        const double r = radius * std::sqrt(random(key, SaltSprayRadius));
        const double pr = st.sprayParticleSize * (1.0 - st.sprayParticleJitter * random(key, SaltSpraySize)) / 2.0;
        const double cx = dab.center.x() + r * std::cos(a), cy = dab.center.y() + r * std::sin(a);
        const QRect area = QRectF(cx - pr - 1, cy - pr - 1, 2 * pr + 2, 2 * pr + 2).toAlignedRect() & rect;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                double v = std::clamp(pr + 0.5 - std::hypot(x + 0.5 - cx, y + 0.5 - cy), 0.0, 1.0);
                if (pr < 0.5)
                    v *= 2.0 * pr;
                if (v <= 0.0)
                    continue;
                if (m_textureEachDab) {
                    const double valley = dab.textureStrength * (1.0 - grain(x + 0.5, y + 0.5));
                    v = subtract ? std::max(0.0, v - valley) : v * (1.0 - valley);
                    if (v <= 0.0)
                        continue;
                }
                const qint64 tk = tileKey(x / Tile, y / Tile);
                if (tk != cachedKey) {
                    cachedKey = tk;
                    tile = &coverageTile(x / Tile, y / Tile);
                    if (m_colorPerDab) {
                        auto [it, inserted] = m_colors.try_emplace(tk);
                        if (inserted)
                            it->second.assign(size_t(Tile * Tile), 0);
                        colors = &it->second;
                    }
                }
                const size_t idx = size_t((y % Tile) * Tile + x % Tile);
                quint16 &s = (*tile)[idx];
                quint32 add = 0;
                if (!m_buildUp) {
                    const quint32 target = quint32(std::lround(v * ceiling));
                    if (target > s)
                        add = target - s;
                } else if (ceiling > s) {
                    const double inc = v * dab.flow * double(ceiling - s);
                    add = std::min<quint32>(ceiling - s, quint32(inc + threshold(x + int(key * 29), y + int(key * 17))));
                }
                if (add == 0)
                    continue;
                if (colors) {
                    quint32 &c = (*colors)[idx];
                    c = s == 0 ? dab.color : mixColor(c, s, dab.color, add);
                }
                s = quint16(s + add);
            }
        }
    }
}

void StrokeEngine::compositeTouched(StrokeSurface &image, const TileSnapshot &before)
{
    // Each pixel is recomputed once per call, however many dabs covered it.
    for (const auto &[key, area] : m_touched)
        composite(image, area, before);
    m_touched.clear();
}

void StrokeEngine::composite(StrokeSurface &image, const QRect &rect, const TileSnapshot &before)
{
    const int column = rect.left() / Tile, row = rect.top() / Tile;
    Q_ASSERT(before.storedRow(rect.left(), rect.top())); // renderDab captured every tile it touches
    if (!before.storedRow(rect.left(), rect.top()) || m_allocationFailed)
        return;
    const int x0 = rect.left();
    const auto found = m_coverage.find(tileKey(column, row));
    if (found == m_coverage.end())
        return; // a spray dab may touch a tile without any particle landing in it
    const CoverageTile &tile = found->second;
    const ColorTile *colors = nullptr;
    if (m_colorPerDab) {
        const auto it = m_colors.find(tileKey(column, row));
        colors = it == m_colors.end() ? nullptr : &it->second;
    }
    const int ox = column * Tile, oy = row * Tile;
    const bool erase = m_settings.blend == BrushBlend::Erase;
    const PaintMode mode = m_settings.paintMode;
    const bool strokeTexture = m_texture && !m_textureEachDab;
    const bool exact = !strokeTexture && m_clip.isNull();
    const double strength = m_settings.textureStrength;
    const double opacity = m_settings.opacity;
    const bool subtract = m_settings.textureMode == TextureMode::Subtract;
    // The usual grain (unscaled, unrotated, unadjusted) is read straight from its row.
    const BrushSettings &st = m_settings;
    const bool directGrain = strokeTexture && m_textureNearest && !st.textureInvert && st.textureContrast == 1.0
                          && st.textureBrightness == 0.0;
    const QImage *grainImage = strokeTexture ? &m_texture->image : nullptr;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        QRgb *out = image.writableRow(x0, y); // indexed from x0
        if (!out) {
            m_allocationFailed = true;
            return;
        }
        const QRgb *src = before.storedRow(x0, y);
        const quint16 *cov = tile.data() + (y - oy) * Tile;
        const uchar *grainRow = nullptr;
        int grainWidth = 0;
        if (directGrain) {
            const int gh = grainImage->height(), gy = y + m_textureOffset.y();
            grainRow = grainImage->constScanLine(((gy % gh) + gh) % gh);
            grainWidth = grainImage->width();
        }
        const quint32 *col = colors ? colors->data() + (y - oy) * Tile : nullptr;
        const uchar *clip = m_clip.isNull() ? nullptr : m_clip.constScanLine(y);
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const QRgb p = src[x - x0];
            const quint32 coverage = cov[x - ox];
            if (coverage == 0) {
                out[x - x0] = p;
                continue;
            }
            double k; // 8-bit units
            if (exact) {
                k = coverage / 257.0; // a whole level for a coverage that is one
            } else {
                double s = coverage / double(FullCoverage);
                if (strokeTexture) {
                    double t;
                    if (grainRow) {
                        const int gx = x + m_textureOffset.x();
                        t = grainRow[((gx % grainWidth) + grainWidth) % grainWidth] / 255.0;
                    } else {
                        t = grain(x + 0.5, y + 0.5);
                    }
                    const double valley = strength * (1.0 - t);
                    // Subtract works on the coverage relative to the brush opacity, so a light
                    // mark breaks up into grain while a heavy one fills the tooth.
                    s = subtract ? opacity * std::max(0.0, s / opacity - valley) : s * (1.0 - valley);
                }
                if (clip)
                    s *= clip[x] / 255.0;
                k = s * 255.0;
            }
            const quint32 a = quint32(std::clamp(int(k + threshold(x, y)), 0, 255));
            if (a == 0) {
                out[x - x0] = p;
                continue;
            }
            const quint32 keep = 255 - a;
            if (erase) {
                out[x - x0] = qRgba(int(mul255(qRed(p), keep)), int(mul255(qGreen(p), keep)), int(mul255(qBlue(p), keep)),
                               int(mul255(qAlpha(p), keep)));
                continue;
            }
            const QRgb c = col ? col[x - ox] : m_color;
            const quint32 sr = mul255(qRed(c), a), sg = mul255(qGreen(c), a), sb = mul255(qBlue(c), a); // premultiplied source
            const quint32 pr = qRed(p), pg = qGreen(p), pb = qBlue(p), pa = qAlpha(p);
            quint32 r, g, b, alpha;
            switch (mode) {
            case PaintMode::Multiply:
                // k·cs·(1 − αb) + cb'·(1 − k) + k·cs·cb'
                r = mul255(sr, 255 - pa) + mul255(pr, keep) + mul255(sr, pr);
                g = mul255(sg, 255 - pa) + mul255(pg, keep) + mul255(sg, pg);
                b = mul255(sb, 255 - pa) + mul255(pb, keep) + mul255(sb, pb);
                alpha = a + mul255(pa, keep);
                break;
            case PaintMode::Screen:
                // k·cs + cb' − k·cs·cb'
                r = sr + pr - mul255(sr, pr);
                g = sg + pg - mul255(sg, pg);
                b = sb + pb - mul255(sb, pb);
                alpha = a + mul255(pa, keep);
                break;
            case PaintMode::Behind:
                // The stroke goes under existing paint: cb' + k·cs·(1 − αb).
                r = pr + mul255(sr, 255 - pa);
                g = pg + mul255(sg, 255 - pa);
                b = pb + mul255(sb, 255 - pa);
                alpha = pa + mul255(a, 255 - pa);
                break;
            case PaintMode::PreserveAlpha:
                // Colour only where there is paint; alpha unchanged.
                r = mul255(pr, keep) + mul255(sr, pa);
                g = mul255(pg, keep) + mul255(sg, pa);
                b = mul255(pb, keep) + mul255(sb, pa);
                alpha = pa;
                break;
            case PaintMode::Normal:
            default:
                // Source-over with a premultiplied source (colour × a, alpha a).
                r = sr + mul255(pr, keep);
                g = sg + mul255(pg, keep);
                b = sb + mul255(pb, keep);
                alpha = a + mul255(pa, keep);
                break;
            }
            alpha = std::min<quint32>(alpha, 255);
            out[x - x0] = qRgba(int(std::min(r, alpha)), int(std::min(g, alpha)), int(std::min(b, alpha)), int(alpha));
        }
    }
}

// --- smudge and wet paint ---

QRgb StrokeEngine::workPixel(const StrokeSurface &image, int x, int y) const
{
    const auto it = m_work.find(tileKey(x / Tile, y / Tile));
    if (it == m_work.end())
        return image.pixel(x, y);
    return reinterpret_cast<const QRgb *>(it->second.constScanLine(y % Tile))[x % Tile];
}

void StrokeEngine::loadPatch(const StrokeSurface &image, const DabParams &dab)
{
    const int n = m_patchSize;
    if (m_settings.blend == BrushBlend::WetPaint) {
        // Charged with brush paint (thinned by the dilution), as much as the load says.
        const double alpha = (1.0 - m_settings.wetDilution) * m_settings.wetLoad;
        const float c[4] = {float(qRed(dab.color) * alpha), float(qGreen(dab.color) * alpha),
                            float(qBlue(dab.color) * alpha), float(255.0 * alpha)};
        for (int i = 0; i < n * n; ++i)
            std::memcpy(&m_patch[size_t(i) * 4], c, sizeof(c));
        return;
    }
    // Smudge: pick up what is under the first dab; beyond the layer edge, the nearest edge pixel.
    const int ox = int(std::floor(dab.center.x())) - n / 2, oy = int(std::floor(dab.center.y())) - n / 2;
    const int w = image.rect().width(), h = image.rect().height();
    double sum[4] = {0, 0, 0, 0};
    for (int j = 0; j < n; ++j) {
        const int py = std::clamp(oy + j, 0, h - 1);
        for (int i = 0; i < n; ++i) {
            const QRgb p = workPixel(image, std::clamp(ox + i, 0, w - 1), py);
            float *c = &m_patch[(size_t(j) * n + i) * 4];
            c[0] = float(qRed(p));
            c[1] = float(qGreen(p));
            c[2] = float(qBlue(p));
            c[3] = float(qAlpha(p));
            for (int k = 0; k < 4; ++k)
                sum[k] += c[k];
        }
    }
    if (m_settings.smudgeMode == SmudgeMode::Dulling) {
        const float mean[4] = {float(sum[0] / (n * n)), float(sum[1] / (n * n)), float(sum[2] / (n * n)),
                               float(sum[3] / (n * n))};
        for (int i = 0; i < n * n; ++i)
            std::memcpy(&m_patch[size_t(i) * 4], mean, sizeof(mean));
    }
}

void StrokeEngine::carry(StrokeSurface &image, const DabParams &dab, const QRect &rect, const TileSnapshot &before)
{
    if (m_patch.empty()) {
        // Large enough for the biggest dab this stroke can make (every other size factor is ≤ 1).
        const double largest = m_settings.size * std::max(1.0, m_settings.tiltSize);
        m_patchSize = int(std::ceil(largest)) + 4;
        m_patch.assign(size_t(m_patchSize) * m_patchSize * 4, 0.0f);
    }
    if (!m_patchLoaded) {
        loadPatch(image, dab);
        m_patchLoaded = true;
        if (m_settings.blend == BrushBlend::Smudge)
            return;
    }

    // The dab works on a copy of its area; without a selection the copy is written back as is.
    // With a selection: the copy is unclipped (so the carried colour is not weakened dab after
    // dab at soft edges), and  before + (work − before) × selection  is written once. The brush
    // may pick up colour from outside the selection; it writes only inside.
    QImage region(rect.size(), QImage::Format_ARGB32_Premultiplied);
    if (region.isNull()) {
        m_allocationFailed = true;
        return;
    }
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(region.scanLine(y - rect.top()));
        for (int x = rect.left(); x <= rect.right(); ++x)
            line[x - rect.left()] = workPixel(image, x, y);
    }
    carryInto(region, rect.topLeft(), dab, rect);
    if (m_clip.isNull()) {
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(region.constScanLine(y - rect.top()));
            for (int x = rect.left(); x <= rect.right();) {
                const int end = std::min(rect.right(), (x / Tile + 1) * Tile - 1);
                QRgb *out = image.writableRow(x, y);
                if (!out) {
                    m_allocationFailed = true;
                    return;
                }
                std::memcpy(out, line + (x - rect.left()), size_t(end - x + 1) * 4);
                x = end + 1;
            }
        }
        return;
    }
    for (int row = rect.top() / Tile; row <= rect.bottom() / Tile; ++row) {
        for (int column = rect.left() / Tile; column <= rect.right() / Tile; ++column) {
            const QRect tileRect = QRect(column * Tile, row * Tile, Tile, Tile) & image.rect();
            auto [it, inserted] = m_work.try_emplace(tileKey(column, row));
            if (inserted) {
                it->second = QImage(Tile, Tile, QImage::Format_ARGB32_Premultiplied);
                it->second.fill(Qt::transparent);
                for (int y = tileRect.top(); y <= tileRect.bottom(); ++y) {
                    const QRgb *src = image.constRow(tileRect.left(), y);
                    if (src)
                        std::memcpy(it->second.scanLine(y - row * Tile), src, size_t(tileRect.width()) * 4);
                }
            }
            const QRect part = rect & tileRect;
            for (int y = part.top(); y <= part.bottom(); ++y)
                std::memcpy(it->second.scanLine(y - row * Tile) + (part.left() - column * Tile) * 4,
                            region.constScanLine(y - rect.top()) + (part.left() - rect.left()) * 4,
                            size_t(part.width()) * 4);
            if (!before.storedRow(part.left(), part.top()))
                continue;
            for (int y = part.top(); y <= part.bottom(); ++y) {
                QRgb *out = image.writableRow(part.left(), y); // indexed from part.left()
                if (!out) {
                    m_allocationFailed = true;
                    return;
                }
                const QRgb *b = before.storedRow(part.left(), y);
                const QRgb *wk = reinterpret_cast<const QRgb *>(it->second.constScanLine(y - row * Tile)) + (part.left() - column * Tile);
                const uchar *clip = m_clip.constScanLine(y);
                for (int x = part.left(); x <= part.right(); ++x) {
                    const QRgb bp = b[x - part.left()], wp = wk[x - part.left()];
                    const int c = clip[x];
                    auto mix = [&](int from, int to) { return from + (to - from) * c / 255; };
                    const int a = std::clamp(mix(qAlpha(bp), qAlpha(wp)), 0, 255);
                    out[x - part.left()] = qRgba(std::min(a, mix(qRed(bp), qRed(wp))), std::min(a, mix(qGreen(bp), qGreen(wp))),
                                                 std::min(a, mix(qBlue(bp), qBlue(wp))), a);
                }
            }
        }
    }
}

void StrokeEngine::carryInto(QImage &target, const QPoint &origin, const DabParams &dab, const QRect &rect)
{
    const BrushSettings &st = m_settings;
    const int n = m_patchSize;
    const int ox = int(std::floor(dab.center.x())) - n / 2, oy = int(std::floor(dab.center.y())) - n / 2;
    const DabShape shape(dab, st, m_tip);
    const bool wet = st.blend == BrushBlend::WetPaint;
    const bool dulling = !wet && st.smudgeMode == SmudgeMode::Dulling;
    const bool keepAlpha = st.smudgePreserveAlpha;
    // Every rate is per pass, like flow: normalised to the spacing so a pixel passed once is
    // affected the same however many dabs cover it.
    const double fraction = passExponent(dab.stepFraction);
    auto perPass = [&](double rate) { return float(1.0 - std::pow(1.0 - std::clamp(rate, 0.0, 1.0), fraction)); };
    const float lay = perPass(dab.strength); // smudge strength, or wet paint attack
    float take, tint;
    float brush[4];
    if (wet) {
        const double remaining = st.wetLoadLength > 0 ? std::max(0.0, 1.0 - dab.distance / st.wetLoadLength) : 1.0;
        tint = perPass(st.wetLoad * remaining); // refill towards the brush paint
        take = perPass(st.wetPull);
        const double dilution = std::clamp(st.wetDilution + st.wetnessJitter * random(dab.index, SaltWet), 0.0, 1.0);
        const float alpha = float(1.0 - dilution);
        brush[0] = qRed(dab.color) * alpha;
        brush[1] = qGreen(dab.color) * alpha;
        brush[2] = qBlue(dab.color) * alpha;
        brush[3] = 255.0f * alpha;
    } else {
        take = perPass(st.smudgePickup);
        tint = perPass(st.smudgeColorRate);
        brush[0] = float(qRed(dab.color));
        brush[1] = float(qGreen(dab.color));
        brush[2] = float(qBlue(dab.color));
        brush[3] = 255.0f;
    }
    double sum[4] = {0, 0, 0, 0};
    double weight = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        const int j = y - oy;
        if (j < 0 || j >= n)
            continue;
        QRgb *line = reinterpret_cast<QRgb *>(target.scanLine(y - origin.y()));
        const double dy = y + 0.5 - dab.center.y();
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const int i = x - ox;
            if (i < 0 || i >= n)
                continue;
            double v = shape.value(x + 0.5 - dab.center.x(), dy);
            if (v <= 0.0)
                continue;
            if (m_texture) {
                const double valley = dab.textureStrength * (1.0 - grain(x + 0.5, y + 0.5));
                v *= 1.0 - valley;
            }
            if (v <= 0.0)
                continue;
            float *c = &m_patch[(size_t(j) * n + i) * 4];
            const QRgb p = line[x - origin.x()];
            const float d[4] = {float(qRed(p)), float(qGreen(p)), float(qBlue(p)), float(qAlpha(p))};
            const float fv = float(v);
            float r[4];
            if (wet) {
                for (int k = 0; k < 4; ++k)
                    c[k] += (brush[k] - c[k]) * tint * fv;
                // The carried paint composited over the pixel (never lowers its alpha).
                const float a = lay * fv;
                const float keep = 1.0f - a * c[3] / 255.0f;
                for (int k = 0; k < 4; ++k) {
                    r[k] = c[k] * a + d[k] * keep;
                    c[k] += (d[k] - c[k]) * take * fv; // picks up the pixel as it was before this dab
                }
            } else {
                for (int k = 0; k < 4; ++k) {
                    r[k] = d[k] + (c[k] - d[k]) * lay * fv;
                    c[k] += (d[k] - c[k]) * take * fv;
                    if (tint > 0)
                        c[k] += (brush[k] - c[k]) * tint * fv;
                }
                if (dulling) {
                    for (int k = 0; k < 4; ++k)
                        sum[k] += c[k] * v;
                    weight += v;
                }
            }
            if (keepAlpha) {
                // Colour moves, alpha stays: rescale the colour to the pixel's own alpha.
                if (r[3] > 0.5f && d[3] > 0) {
                    const float scale = d[3] / r[3];
                    for (int k = 0; k < 3; ++k)
                        r[k] *= scale;
                } else {
                    for (int k = 0; k < 3; ++k)
                        r[k] = d[k];
                }
                r[3] = d[3];
            }
            const int alpha = std::clamp(int(std::lround(r[3])), 0, 255);
            // Premultiplied: no channel may exceed alpha (transparent pixels carry no colour).
            line[x - origin.x()] = qRgba(std::clamp(int(std::lround(r[0])), 0, alpha),
                                         std::clamp(int(std::lround(r[1])), 0, alpha),
                                         std::clamp(int(std::lround(r[2])), 0, alpha), alpha);
        }
    }
    if (dulling && weight > 0) {
        // Dulling carries one colour: the average of what the brush now holds where it touched.
        const float mean[4] = {float(sum[0] / weight), float(sum[1] / weight), float(sum[2] / weight),
                               float(sum[3] / weight)};
        for (int i = 0; i < n * n; ++i)
            std::memcpy(&m_patch[size_t(i) * 4], mean, sizeof(mean));
    }
}

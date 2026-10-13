#include "StrokeEngine.h"

#include "BrushTips.h"
#include "StrokeDeposit.h"
#include "TileSnapshot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {

using namespace BrushRaster;

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

StrokeContext makeContext(const BrushSettings &settings, const QColor &color, quint64 seed)
{
    StrokeContext ctx;
    ctx.settings = settings.sanitized();
    ctx.color = qRgb(color.red(), color.green(), color.blue());
    ctx.seed = seed;
    ctx.buildUp = ctx.settings.accumulation == Accumulation::BuildUp || ctx.settings.airbrush;
    ctx.coverageBlend = ctx.settings.blend == BrushBlend::Paint || ctx.settings.blend == BrushBlend::Erase;
    return ctx;
}

} // namespace

StrokeEngine::StrokeEngine(const BrushSettings &settings, const QColor &color, quint64 seed, qreal smoothingRadius)
    : m_ctx(makeContext(settings, color, seed))
    , m_smoother(smoothingRadius)
    , m_spacer([this](const StrokeSample &s) { return diameterAt(s); }, m_ctx.settings.spacing)
{
    BrushSettings &s = m_ctx.settings;
    for (int i = 0; i < DynamicTargetCount; ++i) {
        m_dyn[i] = CompiledDynamics(s.dynamics[size_t(i)], DynamicTarget(i));
        m_anyDynamics = m_anyDynamics || m_dyn[i].isActive();
    }
    if (m_dyn[int(DynamicTarget::Spacing)].isActive() || s.spacingJitter > 0)
        m_spacer.setSpacingFunction([this](const StrokeSample &sample, qreal) { return spacingFactorAt(sample); });
    if (!m_ctx.coverageBlend)
        s.stamp = BrushStamp::Tip; // spray and hatching are coverage stamps

    if (!s.tipImage.isNull()) {
        m_importedTip = std::make_unique<BrushTips::Tip>(BrushTips::makeTip(s.tipImage));
        m_ctx.tip = m_importedTip.get();
    } else if (!s.tip.isEmpty()) {
        m_ctx.tip = BrushTips::tip(s.tip); // unknown id: round tip
    }
    m_ctx.passLength = m_ctx.tip ? m_ctx.tip->passLength : BrushTips::roundPassLength(s.hardness);

    const bool textureDynamics = m_dyn[int(DynamicTarget::TextureStrength)].isActive();
    if (!s.texture.isEmpty() && s.textureStrength > 0)
        m_ctx.texture = BrushTips::texture(s.texture);
    m_ctx.textureEachDab = m_ctx.texture && m_ctx.coverageBlend && (s.textureEachDab || textureDynamics);
    m_ctx.textureNearest = s.textureScale == 1.0 && s.textureRotation == 0.0 && !(m_ctx.textureEachDab && s.textureMoving);
    m_ctx.textureCos = std::cos(s.textureRotation * Pi / 180.0);
    m_ctx.textureSin = std::sin(s.textureRotation * Pi / 180.0);
    if (s.textureRandomOffset)
        m_ctx.textureOffset = QPoint(int(random(-1, StrokeSalt::TextureX) * 4096), int(random(-1, StrokeSalt::TextureY) * 4096));

    m_strokeSize = 1.0 - s.strokeSizeJitter * random(-1, StrokeSalt::StrokeSize);
    m_minimumDiameter = std::min(s.minimumSize, s.size);

    // Per-stroke colour jitter changes the colour once; per-dab colour needs colour tiles.
    QColor c = QColor::fromRgb(m_ctx.color);
    float h, sat, l;
    c.getHslF(&h, &sat, &l);
    m_hue = h < 0 ? 0.0 : h * 360.0;
    m_saturation = sat;
    m_lightness = l;
    if (s.strokeHueJitter > 0 || s.strokeSaturationJitter > 0 || s.strokeLightnessJitter > 0) {
        m_hue += (random(-1, StrokeSalt::StrokeHue) * 2 - 1) * s.strokeHueJitter;
        m_saturation = std::clamp(m_saturation + (random(-1, StrokeSalt::StrokeSaturation) * 2 - 1) * s.strokeSaturationJitter, 0.0, 1.0);
        m_lightness = std::clamp(m_lightness + (random(-1, StrokeSalt::StrokeLightness) * 2 - 1) * s.strokeLightnessJitter, 0.0, 1.0);
        m_hue = std::fmod(std::fmod(m_hue, 360.0) + 360.0, 360.0);
        const QColor j = QColor::fromHslF(float(m_hue / 360.0), float(m_saturation), float(m_lightness));
        m_ctx.color = qRgb(j.red(), j.green(), j.blue());
    }
    m_ctx.colorPerDab = (s.blend == BrushBlend::Paint || s.blend == BrushBlend::WetPaint) && s.hasColorDynamics();
    m_deposit = StrokeDeposit::create(m_ctx);
}

StrokeEngine::~StrokeEngine() = default;

void StrokeEngine::setClipMask(const QImage &mask)
{
    m_ctx.clip = mask.isNull() ? QImage() : mask.convertToFormat(QImage::Format_Grayscale8);
}

bool StrokeEngine::usesTime() const
{
    return m_ctx.settings.airbrush && m_ctx.coverageBlend;
}

qsizetype StrokeEngine::bufferBytes() const
{
    return m_deposit->bufferBytes();
}

int StrokeEngine::coverageTileCount() const
{
    return m_deposit->coverageTileCount();
}

// --- input ---

void StrokeEngine::acceptInput(StrokeSample s)
{
    if (!std::isfinite(s.pressure))
        s.pressure = 1.0;
    s.pressure = std::clamp(s.pressure, 0.0, 1.0);
    if (!s.hasPressure)
        s.pressure = 1.0;
    if (m_ctx.settings.pressureSmoothing > 0 && s.hasPressure) {
        if (!m_pressureStarted) {
            m_pressure = s.pressure;
            m_pressureTimeMs = s.timeMs;
            m_pressureStarted = true;
        } else {
            double a;
            if (s.timeMs >= 0 && m_pressureTimeMs >= 0) {
                const double dt = s.timeMs - m_pressureTimeMs;
                a = dt > 0 ? 1.0 - std::exp(-dt / (m_ctx.settings.pressureSmoothing * PressureTimeConstantMs)) : 0.0;
                if (dt > 0)
                    m_pressureTimeMs = s.timeMs;
            } else {
                a = 1.0 - 0.9 * m_ctx.settings.pressureSmoothing; // no event times: per event
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
    flushDeposit(image, before);
    return dirty;
}

QRect StrokeEngine::moveTo(StrokeSurface &image, const StrokeSample &sample, TileSnapshot &before)
{
    QRect dirty;
    if (!m_started)
        return dirty;
    m_smoothed.clear();
    const double filter = m_ctx.settings.jitterFilter;
    if (filter > 0 && std::hypot(sample.pos.x() - m_lastAccepted.pos.x(), sample.pos.y() - m_lastAccepted.pos.y()) < filter) {
        // Too small a move: hold it back (the end point is still drawn at finish).
        m_pending = sample;
        m_hasPending = true;
    } else {
        m_hasPending = false;
        acceptInput(sample);
    }
    process(image, before, sample.timeMs, dirty);
    flushDeposit(image, before);
    return dirty;
}

QRect StrokeEngine::advanceTime(StrokeSurface &image, double timeMs, TileSnapshot &before)
{
    QRect dirty;
    if (!m_started || !usesTime())
        return dirty;
    m_smoothed.clear();
    process(image, before, timeMs, dirty);
    flushDeposit(image, before);
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
    if (m_ctx.settings.smoothingFinish)
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
    if (m_ctx.buildUp && m_ctx.coverageBlend && m_distanceDabCount == 1 && m_timeDabCount == 0 && m_firstParams.flow < 1.0) {
        DabParams topUp = m_firstParams;
        topUp.flow = 1.0 - (1.0 - m_firstParams.passFlow) / (1.0 - m_firstParams.flow);
        topUp.index = m_dabCount;
        if (topUp.flow > 0)
            renderDab(image, topUp, before, dirty);
    }
    flushDeposit(image, before);
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
        if (!m_finishing && m_ctx.settings.taperEnd > 0 && newest - m_held.front().distance <= m_ctx.settings.taperEnd)
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

    const BrushSettings &st = m_ctx.settings;
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
                const int drop = int(random(m_distanceDabCount, StrokeSalt::Count) * (st.countJitter * copies + 1.0));
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
    const double interval = 1000.0 / m_ctx.settings.airbrushRate;
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
    return m_ctx.settings.followDirection
        && !(sample.hasTilt && std::hypot(sample.tiltX, sample.tiltY) >= m_ctx.settings.tiltAzimuthThreshold);
}

SensorReadings StrokeEngine::readingsFor(const StrokeSample &s, qint64 index) const
{
    SensorReadings r;
    r.seed = m_ctx.seed;
    r.dabIndex = index;
    if (s.hasPressure)
        r.set(Sensor::Pressure, std::clamp(s.pressure, 0.0, 1.0));
    if (s.hasTilt) {
        // Scaled so that the settings' full tilt angle reads as the sensors' full tilt.
        const double k = 60.0 / m_ctx.settings.fullTiltAngle;
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
    const BrushSettings &s = m_ctx.settings;
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
    const BrushSettings &st = m_ctx.settings;
    const double pressure = s.hasPressure ? s.pressure : 1.0;
    double d = st.size * m_strokeSize * st.pressureSize.apply(pressure)
             * tiltFactor(st.tiltSize, tiltMagnitude(s, st.fullTiltAngle));
    double v;
    if (m_anyDynamics && m_dyn[int(DynamicTarget::Size)].evaluate(readingsFor(s, index), &v))
        d *= v;
    if (st.sizeJitter > 0)
        d *= 1.0 - st.sizeJitter * random(index, StrokeSalt::Size);
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
    if (m_ctx.settings.spacingJitter > 0)
        factor *= 1.0 + m_ctx.settings.spacingJitter * (random(index, StrokeSalt::Spacing) * 2.0 - 1.0);
    return std::max(0.0, factor);
}

StrokeEngine::DabParams StrokeEngine::paramsFor(const Dab &dab) const
{
    const BrushSettings &st = m_ctx.settings;
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
        const double across = (random(index, StrokeSalt::ScatterAcross) * 2 - 1) * amount * dab.diameter;
        const double along =
            st.scatterBothAxes ? (random(index, StrokeSalt::ScatterAlong) * 2 - 1) * amount * dab.diameter : 0.0;
        const QPointF dir(std::cos(dab.direction), std::sin(dab.direction));
        d.center += dir * along + QPointF(-dir.y(), dir.x()) * across;
    }

    double strength = st.pressureOpacity.apply(pressure) * tiltFactor(st.tiltOpacity, tilt);
    if (m_dyn[int(DynamicTarget::Opacity)].evaluate(r, &v))
        strength *= v;
    if (st.opacityJitter > 0)
        strength *= 1.0 - st.opacityJitter * random(index, StrokeSalt::Opacity);
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
        passFlow *= 1.0 - st.flowJitter * random(index, StrokeSalt::Flow);
    d.passFlow = std::clamp(passFlow, 0.0, 1.0);
    d.stepFraction = dab.step > 0 ? dab.step / std::max(StrokeSpacer::MinDiameter, dab.diameter)
                                  : m_spacer.stepFraction(dab.diameter);
    d.flow = m_ctx.buildUp ? 1.0 - std::pow(1.0 - d.passFlow, m_ctx.passExponent(d.stepFraction)) : 1.0;

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
        angle += (BrushTips::hash01(qint64(m_ctx.seed), index, StrokeSalt::Angle) * 2.0 - 1.0) * st.angleJitter * Pi / 180.0;
    d.angle = angle;

    double roundness = st.roundness;
    if (m_dyn[int(DynamicTarget::Roundness)].evaluate(r, &v))
        roundness *= v;
    if (st.roundnessJitter > 0)
        roundness *= 1.0 - st.roundnessJitter * random(index, StrokeSalt::Roundness);
    d.roundness = std::max(0.05, roundness);
    d.flipX = st.flipX != (st.randomFlipX && random(index, StrokeSalt::FlipX) < 0.5);
    d.flipY = st.flipY != (st.randomFlipY && random(index, StrokeSalt::FlipY) < 0.5);

    d.textureStrength = st.textureStrength;
    if (m_dyn[int(DynamicTarget::TextureStrength)].evaluate(r, &v))
        d.textureStrength *= v;
    d.color = dabColor(r, index);
    // For milestone 11 strategies (bristle contact, dry-media penetration, strands): not used by the
    // milestone 10 engines.
    d.pressure = pressure;
    if (m_headingKnown) {
        d.direction = std::atan2(m_heading.y(), m_heading.x());
        d.hasDirection = true;
    } else if (s.hasDirection) {
        d.direction = s.direction;
        d.hasDirection = true;
    }
    return d;
}

QRgb StrokeEngine::dabColor(const SensorReadings &r, qint64 index) const
{
    if (!m_ctx.colorPerDab)
        return m_ctx.color;
    const BrushSettings &st = m_ctx.settings;
    double h = m_hue, s = m_saturation, l = m_lightness;
    if (st.hueJitter > 0)
        h += (random(index, StrokeSalt::Hue) * 2 - 1) * st.hueJitter;
    if (st.saturationJitter > 0)
        s += (random(index, StrokeSalt::Saturation) * 2 - 1) * st.saturationJitter;
    if (st.lightnessJitter > 0)
        l += (random(index, StrokeSalt::Lightness) * 2 - 1) * st.lightnessJitter;
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

void StrokeEngine::renderDab(StrokeSurface &image, const DabParams &dab, TileSnapshot &before, QRect &dirty)
{
    if (m_dabCount == 0)
        m_firstParams = dab;
    ++m_dabCount;
    const QRect rect = m_ctx.dabRect(dab, image);
    if (rect.isEmpty())
        return;
    m_deposit->dab(image, dab, rect, before, dirty);
}

void StrokeEngine::flushDeposit(StrokeSurface &image, const TileSnapshot &before)
{
    m_deposit->flush(image, before);
}

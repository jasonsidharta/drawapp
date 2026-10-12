#include "StrokeEngine.h"

#include "BrushTips.h"
#include "TileSnapshot.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr int Tile = StrokeEngine::Tile;
constexpr quint32 FullCoverage = 65535; // = 255 × 257, so whole 8-bit levels are exact

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

// 0 for a pen held upright (or one that reports no tilt), 1 at FullTiltDegrees or more.
double tiltMagnitude(const StrokeSample &s)
{
    if (!s.hasTilt)
        return 0.0;
    return std::min(1.0, std::hypot(s.tiltX, s.tiltY) / StrokeEngine::FullTiltDegrees);
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
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const double fx = x - x0, fy = y - y0;
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
    bool small;
    const QImage *mip = nullptr; // sampled tips
    double mipScale = 1;         // tip pixels per document pixel

    DabShape(const StrokeEngine::DabParams &dab, const BrushSettings &s, const BrushTips::Tip *tip)
        : radius(dab.diameter / 2.0)
        , cosA(std::cos(dab.angle))
        , sinA(std::sin(dab.angle))
        , invRoundness(1.0 / s.roundness)
        , hardness(s.hardness)
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
        const double u = dx * cosA + dy * sinA;
        const double w = (-dx * sinA + dy * cosA) * invRoundness;
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

} // namespace

StrokeEngine::StrokeEngine(const BrushSettings &settings, const QColor &color, quint64 seed, qreal smoothingRadius)
    : m_settings(settings.sanitized())
    , m_color(qRgb(color.red(), color.green(), color.blue()))
    , m_seed(seed)
    , m_buildUp(m_settings.accumulation == Accumulation::BuildUp || m_settings.airbrush)
    , m_smoother(smoothingRadius)
    , m_spacer(
          [size = m_settings.size, mapping = m_settings.pressureSize, tilt = m_settings.tiltSize](const StrokeSample &s) {
              return size * mapping.apply(s.pressure) * tiltFactor(tilt, tiltMagnitude(s));
          },
          m_settings.spacing)
{
    if (!m_settings.tip.isEmpty())
        m_tip = BrushTips::tip(m_settings.tip); // unknown id: round tip
    m_passLength = m_tip ? m_tip->passLength : BrushTips::roundPassLength(m_settings.hardness);
    if (!m_settings.texture.isEmpty() && m_settings.textureStrength > 0)
        m_texture = BrushTips::texture(m_settings.texture);
}

StrokeEngine::~StrokeEngine() = default;

double StrokeEngine::passExponent(double diameter) const
{
    // Dabs per pass along the tip's densest line = pass length / step; each dab gets the share
    // of the per-pass amount that makes the pass add up to it.
    return std::min(1.0, m_spacer.stepFraction(diameter) / m_passLength);
}

void StrokeEngine::setClipMask(const QImage &mask)
{
    m_clip = mask.isNull() ? QImage() : mask.convertToFormat(QImage::Format_Grayscale8);
}

bool StrokeEngine::usesTime() const
{
    return m_settings.airbrush && m_settings.blend != BrushBlend::Smudge;
}

qsizetype StrokeEngine::bufferBytes() const
{
    return qsizetype(m_coverage.size()) * Tile * Tile * qsizetype(sizeof(quint16))
         + qsizetype(m_patch.size() * sizeof(float));
}

QRect StrokeEngine::begin(QImage &image, const StrokeSample &sample, TileSnapshot &before)
{
    Q_ASSERT(image.format() == QImage::Format_ARGB32_Premultiplied);
    QRect dirty;
    m_started = true;
    m_smoothed.clear();
    m_smoother.begin(sample, m_smoothed);
    process(image, before, sample.timeMs, dirty);
    compositeTouched(image, before);
    return dirty;
}

QRect StrokeEngine::moveTo(QImage &image, const StrokeSample &sample, TileSnapshot &before)
{
    QRect dirty;
    if (!m_started)
        return dirty;
    m_smoothed.clear();
    m_smoother.moveTo(sample, m_smoothed);
    process(image, before, sample.timeMs, dirty);
    compositeTouched(image, before);
    return dirty;
}

QRect StrokeEngine::advanceTime(QImage &image, double timeMs, TileSnapshot &before)
{
    QRect dirty;
    if (!m_started || !usesTime())
        return dirty;
    m_smoothed.clear();
    process(image, before, timeMs, dirty);
    compositeTouched(image, before);
    return dirty;
}

QRect StrokeEngine::finish(QImage &image, TileSnapshot &before)
{
    QRect dirty;
    if (!m_started)
        return dirty;
    m_smoothed.clear();
    m_smoother.finish(m_smoothed);
    process(image, before, -1, dirty);
    if (m_firstDabPending) {
        // Never moved far enough to have a direction: a dot at the brush's own angle.
        m_firstDabPending = false;
        renderDab(image, paramsFor(m_firstDab), before, dirty);
    }
    // A dot gets one full pass of paint, like any point of a moving stroke, instead of the single
    // spacing-normalised dab (which for a low flow would be nearly invisible).
    if (m_buildUp && m_settings.blend != BrushBlend::Smudge && m_distanceDabCount == 1 && m_timeDabCount == 0
        && m_firstParams.flow < 1.0) {
        DabParams topUp = m_firstParams;
        topUp.flow = 1.0 - (1.0 - m_firstParams.passFlow) / (1.0 - m_firstParams.flow);
        if (topUp.flow > 0)
            renderDab(image, topUp, before, dirty);
    }
    compositeTouched(image, before);
    m_started = false;
    return dirty;
}

QRect StrokeEngine::render(QImage &image, const BrushSettings &settings, const QColor &color,
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

void StrokeEngine::process(QImage &image, TileSnapshot &before, double nowMs, QRect &dirty)
{
    m_dabs.clear();
    for (const StrokeSample &s : m_smoothed) {
        if (!m_spacerStarted) {
            m_spacer.begin(s, m_dabs);
            m_spacerStarted = true;
        } else {
            m_spacer.moveTo(s, m_dabs);
        }
    }
    m_smoothed.clear();
    m_timed.assign(m_dabs.size(), false);
    // Distance dabs restart the airbrush clock; elapsed time adds dabs only where the pen lingers.
    for (const Dab &d : m_dabs)
        if (d.sample.timeMs >= 0)
            m_lastDabTimeMs = d.sample.timeMs;
    addTimeDabs(nowMs);

    for (size_t i = 0; i < m_dabs.size(); ++i) {
        const Dab &dab = m_dabs[i];
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
        }
        if (m_firstDabPending) {
            m_firstDabPending = false;
            renderDab(image, paramsFor(m_firstDab), before, dirty);
        }
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
        m_dabs.push_back({at.pos, m_spacer.diameterFor(at), at, direction});
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
        && !(sample.hasTilt && std::hypot(sample.tiltX, sample.tiltY) >= MinAzimuthTiltDegrees);
}

StrokeEngine::DabParams StrokeEngine::paramsFor(const Dab &dab) const
{
    const StrokeSample &s = dab.sample;
    const double tilt = tiltMagnitude(s);
    DabParams d;
    d.center = dab.center;
    d.diameter = dab.diameter;
    const double strength = m_settings.pressureOpacity.apply(s.pressure) * tiltFactor(m_settings.tiltOpacity, tilt);
    d.ceiling = std::clamp(std::round(m_settings.opacity * strength * 255.0), 0.0, 255.0) / 255.0;
    d.strength = std::clamp(m_settings.smudgeStrength * strength, 0.0, 1.0);
    d.passFlow = std::clamp(m_settings.flow * m_settings.pressureFlow.apply(s.pressure), 0.0, 1.0);
    d.flow = m_buildUp ? 1.0 - std::pow(1.0 - d.passFlow, passExponent(d.diameter)) : 1.0;

    double angle = m_settings.angle * Pi / 180.0;
    if (m_settings.followDirection) {
        if (!needsHeading(s))
            angle += std::atan2(s.tiltY, s.tiltX); // pen azimuth
        else if (m_headingKnown)
            angle += std::atan2(m_heading.y(), m_heading.x());
    }
    if (m_settings.angleJitter > 0)
        angle += (BrushTips::hash01(qint64(m_seed), m_dabCount, 0xA9u) * 2.0 - 1.0) * m_settings.angleJitter * Pi / 180.0;
    d.angle = angle;
    return d;
}

QRect StrokeEngine::dabRect(const DabParams &dab, const QImage &image) const
{
    // A sampled tip is a square that may be rotated; a round tip never extends past its radius.
    const double r = dab.diameter / 2.0 * (m_tip ? 1.4143 : 1.0) + 1.0;
    return QRectF(dab.center.x() - r, dab.center.y() - r, 2 * r, 2 * r).toAlignedRect() & image.rect();
}

void StrokeEngine::renderDab(QImage &image, const DabParams &dab, TileSnapshot &before, QRect &dirty)
{
    if (m_dabCount == 0)
        m_firstParams = dab;
    ++m_dabCount;
    const QRect rect = dabRect(dab, image);
    if (rect.isEmpty())
        return;
    if (m_settings.blend == BrushBlend::Smudge) {
        // The first dab only loads the patch; after that each dab changes pixels.
        if (m_patchLoaded)
            before.capture(image, rect);
        smudge(image, dab, rect);
        dirty |= rect;
        return;
    }
    if (dab.ceiling <= 0 || dab.flow <= 0)
        return;
    // Per dab, not the union: a long diagonal segment must not capture its whole bounding box.
    before.capture(image, rect);
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

void StrokeEngine::rasterize(const DabParams &dab, const QRect &rect)
{
    const DabShape shape(dab, m_settings, m_tip);
    const quint32 ceiling = quint32(std::lround(dab.ceiling * 255.0)) * 257u;
    const qint64 index = m_dabCount;
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
                        // Stochastic rounding: unbiased, and the stroke still reaches the ceiling.
                        const double inc = v * dab.flow * double(ceiling - s);
                        const quint32 step = quint32(inc + threshold(x + int(index * 29), y + int(index * 17)));
                        s = quint16(std::min<quint32>(ceiling, s + step));
                    }
                }
            }
        }
    }
}

double StrokeEngine::textureAt(int x, int y) const
{
    const double scale = m_settings.textureScale;
    if (scale == 1.0) {
        const QImage &t = m_texture->image;
        return t.constScanLine(((y % t.height()) + t.height()) % t.height())[((x % t.width()) + t.width()) % t.width()]
             / 255.0;
    }
    return sampleWrapped(m_texture->image, (x + 0.5) / scale, (y + 0.5) / scale);
}

void StrokeEngine::compositeTouched(QImage &image, const TileSnapshot &before)
{
    // Each pixel is recomputed once per call, however many dabs covered it.
    for (const auto &[key, area] : m_touched)
        composite(image, area, before);
    m_touched.clear();
}

void StrokeEngine::composite(QImage &image, const QRect &rect, const TileSnapshot &before)
{
    const int column = rect.left() / Tile, row = rect.top() / Tile;
    const QImage *original = before.storedTile(column, row);
    Q_ASSERT(original); // renderDab captured every tile it touches
    if (!original)
        return;
    const CoverageTile &tile = m_coverage.at(tileKey(column, row));
    const int ox = column * Tile, oy = row * Tile;
    const quint32 cr = qRed(m_color), cg = qGreen(m_color), cb = qBlue(m_color);
    const bool erase = m_settings.blend == BrushBlend::Erase;
    const bool exact = !m_texture && m_clip.isNull();
    const double strength = m_settings.textureStrength;
    const double opacity = m_settings.opacity;
    const bool subtract = m_settings.textureMode == TextureMode::Subtract;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        QRgb *out = reinterpret_cast<QRgb *>(image.scanLine(y));
        const QRgb *src = reinterpret_cast<const QRgb *>(original->constScanLine(y - oy));
        const quint16 *cov = tile.data() + (y - oy) * Tile;
        const uchar *clip = m_clip.isNull() ? nullptr : m_clip.constScanLine(y);
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const QRgb p = src[x - ox];
            const quint32 coverage = cov[x - ox];
            if (coverage == 0) {
                out[x] = p;
                continue;
            }
            double k; // 8-bit units
            if (exact) {
                k = coverage / 257.0; // a whole level for a coverage that is one
            } else {
                double s = coverage / double(FullCoverage);
                if (m_texture) {
                    const double valley = strength * (1.0 - textureAt(x, y));
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
                out[x] = p;
                continue;
            }
            const quint32 keep = 255 - a;
            if (erase) {
                out[x] = qRgba(int(mul255(qRed(p), keep)), int(mul255(qGreen(p), keep)), int(mul255(qBlue(p), keep)),
                               int(mul255(qAlpha(p), keep)));
            } else {
                // Source-over with a premultiplied source (colour × a, alpha a).
                out[x] = qRgba(int(mul255(cr, a) + mul255(qRed(p), keep)), int(mul255(cg, a) + mul255(qGreen(p), keep)),
                               int(mul255(cb, a) + mul255(qBlue(p), keep)), int(a + mul255(qAlpha(p), keep)));
            }
        }
    }
}

void StrokeEngine::smudge(QImage &image, const DabParams &dab, const QRect &rect)
{
    if (m_patch.empty()) {
        // Large enough for the biggest dab this stroke can make.
        const double largest = m_settings.size * std::max(1.0, m_settings.tiltSize);
        m_patchSize = int(std::ceil(largest)) + 4;
        m_patch.assign(size_t(m_patchSize) * m_patchSize * 4, 0.0f);
    }
    const int n = m_patchSize;
    const int ox = int(std::floor(dab.center.x())) - n / 2, oy = int(std::floor(dab.center.y())) - n / 2;
    const int w = image.width(), h = image.height();

    if (!m_patchLoaded) {
        // Pick up what is under the first dab; beyond the layer edge, the nearest edge pixel.
        for (int j = 0; j < n; ++j) {
            const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(std::clamp(oy + j, 0, h - 1)));
            for (int i = 0; i < n; ++i) {
                const QRgb p = line[std::clamp(ox + i, 0, w - 1)];
                float *c = &m_patch[(size_t(j) * n + i) * 4];
                c[0] = float(qRed(p));
                c[1] = float(qGreen(p));
                c[2] = float(qBlue(p));
                c[3] = float(qAlpha(p));
            }
        }
        m_patchLoaded = true;
        return;
    }

    const DabShape shape(dab, m_settings, m_tip);
    // Strength and pickup are per pass, like flow: normalised to the spacing so a pixel passed
    // once is affected the same however many dabs cover it.
    const double fraction = passExponent(dab.diameter);
    const float strength = float(1.0 - std::pow(1.0 - dab.strength, fraction));
    const float pickup = float(1.0 - std::pow(1.0 - m_settings.smudgePickup, fraction));
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        const int j = y - oy;
        if (j < 0 || j >= n)
            continue;
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        const uchar *clip = m_clip.isNull() ? nullptr : m_clip.constScanLine(y);
        const double dy = y + 0.5 - dab.center.y();
        for (int x = rect.left(); x <= rect.right(); ++x) {
            const int i = x - ox;
            if (i < 0 || i >= n)
                continue;
            double v = shape.value(x + 0.5 - dab.center.x(), dy);
            if (v <= 0.0)
                continue;
            if (m_texture) {
                const double valley = m_settings.textureStrength * (1.0 - textureAt(x, y));
                v *= 1.0 - valley;
            }
            if (clip)
                v *= clip[x] / 255.0;
            if (v <= 0.0)
                continue;
            float *c = &m_patch[(size_t(j) * n + i) * 4];
            const QRgb p = line[x];
            const float d[4] = {float(qRed(p)), float(qGreen(p)), float(qBlue(p)), float(qAlpha(p))};
            const float lay = strength * float(v);
            const float take = pickup * float(v);
            float r[4];
            for (int k = 0; k < 4; ++k) {
                r[k] = d[k] + (c[k] - d[k]) * lay;
                c[k] += (d[k] - c[k]) * take; // picks up the pixel as it was before this dab
            }
            const int alpha = std::clamp(int(std::lround(r[3])), 0, 255);
            // Premultiplied: no channel may exceed alpha (transparent pixels carry no colour).
            line[x] = qRgba(std::min(alpha, int(std::lround(r[0]))), std::min(alpha, int(std::lround(r[1]))),
                            std::min(alpha, int(std::lround(r[2]))), alpha);
        }
    }
}

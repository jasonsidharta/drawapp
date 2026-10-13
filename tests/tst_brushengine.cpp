// Milestone 9 brush engine: the sensor and dynamics framework, the property registry, every
// implemented property's effect on rendering, the new stamps and blends, and preset format 2.
//
// Pressure and tilt in these tests are simulated (StrokeSample values); they show the engine's
// response, not that any physical pen delivers them.

#include "BrushPresetFile.h"
#include "BrushPresetStore.h"
#include "BrushPresets.h"
#include "BrushProperties.h"
#include "BrushTips.h"
#include "StrokeEngine.h"
#include "TiledImage.h"
#include "TileSnapshot.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QRandomGenerator>
#include <QSet>
#include <QtTest>
#include <cmath>
#include <functional>

namespace {

constexpr double Pi = 3.14159265358979323846;
const QColor Paint(200, 120, 40);

QImage transparentCanvas(QSize size = {400, 200})
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

// Vertical bands: red, transparent, blue, green, transparent, grey …, 40 px each.
QImage stripes(QSize size = {400, 200})
{
    QImage image = transparentCanvas(size);
    QPainter p(&image);
    const QColor bands[] = {QColor(220, 30, 30), Qt::transparent, QColor(30, 60, 220), QColor(30, 180, 60),
                            Qt::transparent, QColor(128, 128, 128)};
    for (int i = 0; i * 40 < size.width(); ++i)
        p.fillRect(QRect(i * 40, 0, 40, size.height()), bands[i % 6]);
    return image;
}

StrokeSample sample(double x, double y, double pressure = 1.0, double timeMs = -1)
{
    StrokeSample s;
    s.pos = QPointF(x, y);
    s.pressure = pressure;
    s.timeMs = timeMs;
    return s;
}

// A wobbly left-to-right stroke with simulated pressure (ramp with alternating jitter), tilt
// (about 10°), varying speed, and a pause at the end (for the airbrush).
std::vector<StrokeSample> standardStroke()
{
    std::vector<StrokeSample> v;
    double t = 0;
    const int n = 106;
    for (int i = 0; i <= n; ++i) {
        StrokeSample s;
        s.pos = QPointF(40 + i * 3.0, 100 + 12 * std::sin(i * 0.35));
        s.pressure = std::clamp(0.2 + 0.7 * std::sin(Pi * i / n) + (i % 2 ? 0.1 : -0.1), 0.05, 1.0);
        s.hasTilt = true;
        s.tiltX = 8;
        s.tiltY = 6;
        s.timeMs = t;
        t += 2.0 + (i % 7 == 0 ? 6.0 : 0.0);
        v.push_back(s);
    }
    for (int i = 0; i < 15; ++i) {
        StrokeSample s = v.back();
        t += 20;
        s.timeMs = t;
        v.push_back(s);
    }
    return v;
}

std::vector<StrokeSample> line(QPointF from, QPointF to, int steps, std::function<double(double)> pressure = nullptr,
                               double msPerStep = -1)
{
    std::vector<StrokeSample> v;
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        StrokeSample s;
        s.pos = from + (to - from) * t;
        s.pressure = pressure ? pressure(t) : 1.0;
        s.timeMs = msPerStep < 0 ? -1 : i * msPerStep;
        v.push_back(s);
    }
    return v;
}

QImage render(const BrushSettings &settings, const std::vector<StrokeSample> &samples, const QImage &background,
              quint64 seed = 7, const QColor &color = Paint, const QImage &clip = QImage(), qint64 *dabs = nullptr)
{
    QImage image = background.copy();
    TileSnapshot before;
    StrokeEngine engine(settings, color, seed, StrokeEngine::smoothingRadius(settings));
    if (!clip.isNull())
        engine.setClipMask(clip);
    engine.begin(image, samples.front(), before);
    for (size_t i = 1; i < samples.size(); ++i)
        engine.moveTo(image, samples[i], before);
    engine.finish(image, before);
    if (dabs)
        *dabs = engine.dabCount();
    return image;
}

// The same stroke on a tiled layer (milestone 10), as the canvas paints document layers.
TiledImage renderTiled(const BrushSettings &settings, const std::vector<StrokeSample> &samples, const QImage &background,
                       TileSnapshot *snapshot, quint64 seed = 7, const QColor &color = Paint, const QImage &clip = QImage())
{
    TiledImage image = TiledImage::fromImage(background);
    StrokeEngine engine(settings, color, seed, StrokeEngine::smoothingRadius(settings));
    if (!clip.isNull())
        engine.setClipMask(clip);
    engine.begin(image, samples.front(), *snapshot);
    for (size_t i = 1; i < samples.size(); ++i)
        engine.moveTo(image, samples[i], *snapshot);
    engine.finish(image, *snapshot);
    return engine.allocationFailed() ? TiledImage() : image;
}

// A stroke along tile seams and through tile corners (64-pixel tiles).
std::vector<StrokeSample> seamStroke()
{
    std::vector<StrokeSample> v;
    for (int i = 0; i <= 60; ++i) {
        StrokeSample s = sample(20 + i * 6.0, i < 30 ? 64.0 + (i % 3) * 0.5 : 128.0 - (i - 30) * 2.1,
                                0.3 + 0.7 * (i % 10) / 10.0, i * 4.0);
        v.push_back(s);
    }
    v.push_back(sample(128, 128, 1.0, 260)); // a dab centred on a tile corner
    v.push_back(sample(192, 64, 0.5, 270));
    return v;
}

bool validPremultiplied(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qRed(row[x]) > qAlpha(row[x]) || qGreen(row[x]) > qAlpha(row[x]) || qBlue(row[x]) > qAlpha(row[x]))
                return false;
    }
    return true;
}

int differingPixels(const QImage &a, const QImage &b)
{
    int n = 0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *ra = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *rb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
            n += ra[x] != rb[x];
    }
    return n;
}

// Pixels in column x with alpha above half.
int columnHeight(const QImage &image, int x)
{
    int n = 0;
    for (int y = 0; y < image.height(); ++y)
        n += qAlpha(image.pixel(x, y)) > 127;
    return n;
}

int columnMaxAlpha(const QImage &image, int x)
{
    int m = 0;
    for (int y = 0; y < image.height(); ++y)
        m = std::max(m, qAlpha(image.pixel(x, y)));
    return m;
}

// A right triangle in the upper left: an asymmetric tip image with transparency.
QImage asymmetricImage()
{
    QImage image(64, 64, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::black);
    p.setPen(Qt::NoPen);
    p.drawPolygon(QPolygonF({QPointF(4, 4), QPointF(60, 4), QPointF(4, 40)}));
    return image;
}

QVariantMap dynamicsMap(Sensor sensor, SensorCurve curve = SensorCurve(), double minimum = 0)
{
    ParameterDynamics d;
    d.enabled = true;
    d.minimum = minimum;
    d.mappings.push_back({sensor, curve, 0});
    return d.toJson().toVariantMap();
}

void set(BrushSettings &s, const char *id, const QVariant &value)
{
    const BrushProperty *p = BrushProperties::find(QLatin1String(id));
    QVERIFY2(p && p->implemented, id);
    p->set(s, value);
    s = s.sanitized();
}

BrushSettings baseSettings()
{
    BrushSettings s;
    s.name = QStringLiteral("test");
    s.size = 24;
    return s;
}

// The engine a property is tested on: the first it applies to, in this order.
unsigned testEngine(unsigned engines)
{
    for (unsigned e : {unsigned(EnginePaint), unsigned(EngineSmudge), unsigned(EngineWet), unsigned(EngineSpray),
                       unsigned(EngineHatching), unsigned(EngineErase), unsigned(EngineDry), unsigned(EngineBristle),
                       unsigned(EngineWatercolor), unsigned(EngineStrands), unsigned(EngineGrass), unsigned(EngineFoliage),
                       unsigned(EngineSplatter), unsigned(EnginePattern)})
        if (engines & e)
            return e;
    return EnginePaint;
}

void applyEngine(BrushSettings &s, unsigned engine)
{
    switch (engine) {
    case EngineErase: s.blend = BrushBlend::Erase; break;
    case EngineSmudge: s.blend = BrushBlend::Smudge; break;
    case EngineWet: s.blend = BrushBlend::WetPaint; break;
    case EngineSpray: s.stamp = BrushStamp::Spray; break;
    case EngineHatching: s.stamp = BrushStamp::Hatching; break;
    case EngineDry: s.stamp = BrushStamp::Dry; break;
    case EngineBristle: s.stamp = BrushStamp::Bristle; break;
    case EngineWatercolor: s.blend = BrushBlend::Watercolor; break;
    case EngineStrands: s.stamp = BrushStamp::Strands; break;
    case EngineGrass: s.stamp = BrushStamp::Grass; break;
    case EngineFoliage: s.stamp = BrushStamp::Foliage; break;
    case EngineSplatter: s.stamp = BrushStamp::Splatter; break;
    case EnginePattern: s.stamp = BrushStamp::Pattern; break;
    default: break;
    }
}

struct Scenario
{
    std::function<void(BrushSettings &)> setup; // preconditions, applied to both renders
    QVariant changed;
    // The effect shows while the stroke is drawn, not in the finished stroke (live end taper: the
    // finished tail equals the held-back one, the difference is that nothing lags while drawing).
    bool whileDrawing = false;
};

// The stroke drawn up to its middle, without lifting the pen.
QImage renderUnfinished(const BrushSettings &settings, const std::vector<StrokeSample> &samples, const QImage &background)
{
    QImage image = background.copy();
    TileSnapshot before;
    StrokeEngine engine(settings, Paint, 7, StrokeEngine::smoothingRadius(settings));
    engine.begin(image, samples.front(), before);
    for (size_t i = 1; i < samples.size() / 2; ++i)
        engine.moveTo(image, samples[i], before);
    return image;
}

std::map<QString, Scenario> scenarios()
{
    const QVariant randomDyn = dynamicsMap(Sensor::Random);
    auto buildUp = [](double flow) {
        return [flow](BrushSettings &s) {
            s.accumulation = Accumulation::BuildUp;
            s.flow = flow;
        };
    };
    auto textured = [](BrushSettings &s) {
        s.texture = QStringLiteral("paper");
        s.textureStrength = 0.6;
    };
    auto oval = [](BrushSettings &s) { s.roundness = 0.3; };
    auto asym = [](BrushSettings &s) { s.tipImage = BrushProperties::tipFromImage(asymmetricImage()); };
    auto none = [](BrushSettings &) {};
    auto mixMode = [](BrushSettings &s) { s.smudgeMode = SmudgeMode::Mix; };
    auto bristle = [](BrushSettings &s) { s.stamp = BrushStamp::Bristle; s.size = 40; };
    auto watercolor = [](BrushSettings &s) { s.blend = BrushBlend::Watercolor; s.size = 30; };
    auto dry = [](BrushSettings &s) {
        s.stamp = BrushStamp::Dry;
        s.texture = QStringLiteral("rough");
        s.textureStrength = 0.9;
        s.dryDepth = 0.6;
    };
    const QVariant asymImage = QVariant::fromValue(asymmetricImage());
    return {
        // Engine
        {"blend", {none, QStringLiteral("smudge")}},
        {"stamp", {none, QStringLiteral("spray")}},
        // Brush tip
        {"tip", {none, QStringLiteral("bristle")}},
        {"tipImage", {none, asymImage}},
        {"size", {none, 40.0}},
        {"hardness", {none, 0.0}},
        {"roundness", {none, 0.3}},
        {"angle", {oval, 60.0}},
        {"flipX", {asym, true}},
        {"flipY", {asym, true}},
        {"minimumSize", {none, 20.0}},
        {"tipDensity", {none, 0.3}},
        // Stroke path
        {"spacing", {none, 1.0}},
        {"spacingJitter", {[](BrushSettings &s) { s.spacing = 1.0; }, 0.8}},
        {"scatter", {none, 1.0}},
        {"scatterBothAxes", {[](BrushSettings &s) { s.scatter = 1.0; }, true}},
        {"count", {buildUp(0.3), 4}},
        {"countJitter", {[](BrushSettings &s) { s.accumulation = Accumulation::BuildUp; s.flow = 0.2; s.count = 8; }, 1.0}},
        {"airbrush", {none, true}},
        {"airbrushRate", {[](BrushSettings &s) { s.airbrush = true; s.flow = 0.1; }, 120.0}},
        // Pressure and tilt
        {"pressureSize", {none, QVariantMap{{"enabled", false}}}},
        {"pressureOpacity", {none, QVariantMap{{"enabled", true}, {"minimum", 0.0}}}},
        {"pressureFlow", {buildUp(0.5), QVariantMap{{"enabled", true}, {"minimum", 0.0}}}},
        {"tiltSize", {none, 2.5}},
        {"tiltOpacity", {none, 0.2}},
        {"fullTiltAngle", {[](BrushSettings &s) { s.tiltSize = 2.5; }, 20.0}},
        {"tiltAzimuthThreshold", {[](BrushSettings &s) { s.followDirection = true; s.roundness = 0.3; }, 5.0}},
        {"dynamics.size", {none, randomDyn}},
        {"dynamics.opacity", {none, randomDyn}},
        {"dynamics.flow", {buildUp(0.6), randomDyn}},
        // Opacity and flow
        {"opacity", {none, 0.5}},
        {"flow", {buildUp(1.0), 0.3}},
        {"accumulation", {[](BrushSettings &s) { s.flow = 0.5; }, QStringLiteral("buildUp")}},
        {"opacityJitter", {none, 0.8}},
        {"flowJitter", {buildUp(0.5), 0.9}},
        // Texture and grain
        {"texture", {[](BrushSettings &s) { s.textureStrength = 0.6; }, QStringLiteral("paper")}},
        {"textureScale", {textured, 3.0}},
        {"textureStrength", {[](BrushSettings &s) { s.texture = QStringLiteral("paper"); }, 0.9}},
        {"textureMode", {textured, QStringLiteral("subtract")}},
        {"textureRotation", {textured, 45.0}},
        {"textureBrightness", {textured, 0.4}},
        {"textureContrast", {textured, 2.5}},
        {"textureInvert", {textured, true}},
        {"textureEachDab", {[=](BrushSettings &s) { textured(s); buildUp(0.5)(s); }, true}},
        {"textureMoving", {[=](BrushSettings &s) { textured(s); s.textureEachDab = true; }, true}},
        {"textureRandomOffset", {textured, true}},
        {"dynamics.textureStrength", {textured, randomDyn}},
        // Shape dynamics
        {"followDirection", {oval, true}},
        {"angleJitter", {oval, 90.0}},
        {"sizeJitter", {none, 0.8}},
        {"strokeSizeJitter", {none, 0.9}},
        {"roundnessJitter", {none, 0.9}},
        {"randomFlipX", {asym, true}},
        {"randomFlipY", {asym, true}},
        {"dynamics.rotation", {oval, randomDyn}},
        {"dynamics.roundness", {none, randomDyn}},
        {"dynamics.scatter", {[](BrushSettings &s) { s.scatter = 1.0; }, randomDyn}},
        {"dynamics.spacing", {[](BrushSettings &s) { s.spacing = 1.0; }, randomDyn}},
        // Colour dynamics
        {"hueJitter", {none, 120.0}},
        {"saturationJitter", {none, 0.8}},
        {"lightnessJitter", {none, 0.5}},
        {"strokeHueJitter", {none, 120.0}},
        {"strokeSaturationJitter", {none, 0.8}},
        {"strokeLightnessJitter", {none, 0.5}},
        {"dynamics.hue", {none, randomDyn}},
        {"dynamics.saturation", {none, randomDyn}},
        {"dynamics.lightness", {none, randomDyn}},
        // Blending and smudge
        {"paintMode", {none, QStringLiteral("multiply")}},
        {"smudgeStrength", {none, 0.95}},
        {"smudgePickup", {none, 0.9}},
        {"smudgeMode", {none, QStringLiteral("dulling")}},
        {"smudgeColorRate", {none, 0.6}},
        {"smudgePreserveAlpha", {none, true}},
        {"dynamics.smudgeStrength", {none, randomDyn}},
        // Wet paint
        {"wetLoad", {none, 0.2}},
        {"wetLoadLength", {none, 50.0}},
        {"wetAttack", {none, 0.1}},
        {"wetPull", {none, 0.9}},
        {"wetDilution", {none, 0.8}},
        {"wetnessJitter", {none, 1.0}},
        // Stabilisation
        {"smoothing", {none, 80}},
        {"pressureSmoothing", {none, 1.0}},
        {"jitterFilter", {none, 12.0}},
        {"smoothingFinish", {[](BrushSettings &s) { s.smoothing = 80; }, false}},
        // Taper
        {"taperStart", {none, 120.0}},
        {"taperEnd", {none, 120.0}},
        {"taperStartSize", {[](BrushSettings &s) { s.taperStart = 120; }, 0.9}},
        {"taperEndSize", {[](BrushSettings &s) { s.taperEnd = 120; }, 0.9}},
        {"taperOpacity", {[](BrushSettings &s) { s.taperStart = 120; }, 1.0}},
        {"taperShape", {[](BrushSettings &s) { s.taperStart = 120; }, 3.0}},
        // Special effects
        {"sprayDensity", {none, 150.0}},
        {"sprayParticleSize", {none, 6.0}},
        {"sprayParticleJitter", {none, 1.0}},
        {"hatchAngle", {none, -30.0}},
        {"hatchSpacing", {none, 12.0}},
        {"hatchThickness", {none, 4.0}},
        // Bristles (milestone 11)
        {"bristleCount", {bristle, 8}},
        {"bristleThickness", {bristle, 0.2}},
        {"bristleSpread", {bristle, 1.0}},
        {"bristleLengthJitter", {bristle, 1.0}},
        {"bristleLoad", {bristle, 0.2}},
        {"bristleDepletion", {bristle, 0.9}},
        {"bristlePickup", {bristle, 0.9}},
        {"bristleWobble", {bristle, 1.0}},
        // Watercolour (milestone 11); lifting acts on what the stroke itself has let settle
        {"wcWater", {watercolor, 1.0}},
        {"wcPigment", {watercolor, 0.15}},
        {"wcSpread", {watercolor, 1.0}},
        {"wcEdgeDarkening", {watercolor, 1.0}},
        {"wcGranulation", {[=](BrushSettings &s) { watercolor(s); s.texture = QStringLiteral("rough"); s.textureStrength = 0.9; }, 1.0}},
        {"wcDryingTime", {watercolor, 1.0}},
        {"wcLifting", {watercolor, 1.0}},
        // Procedural (milestone 11), each on the first engine it applies to (strands unless noted)
        {"procCount", {none, 20}},
        {"procLength", {none, 3.0}},
        {"procWidth", {none, 6.0}},
        {"procLengthJitter", {none, 1.0}},
        {"procAngleJitter", {none, 90.0}},
        {"procCurl", {none, -1.0}},
        {"procTaper", {none, 0.0}},
        {"procColorJitter", {none, 1.0}},
        {"procSpread", {none, 0.2}},
        {"procDirection", {none, QStringLiteral("up")}},
        {"procStretch", {none, 3.0}},                          // splatter
        {"patternShape", {none, QStringLiteral("zigzag")}},     // pattern
        {"hatchCrossPasses", {none, 3}},                       // hatching
        {"sprayDistribution", {none, QStringLiteral("gaussian")}}, // spray
        // Mixing (milestone 11), on the smudge engine in mix mode
        {"smudgeLength", {mixMode, 0.95}},
        {"smudgeRadius", {mixMode, 0.25}},
        {"smudgeDepletion", {mixMode, 0.9}},
        {"pigmentMixing", {[=](BrushSettings &s) { mixMode(s); s.smudgeColorRate = 0.5; }, true}},
        {"dynamics.colorRate", {[](BrushSettings &s) { s.smudgeColorRate = 0.6; }, randomDyn}},
        // Ink and stroke (milestone 11)
        {"curveInterpolation", {none, true}},
        {"cornerAngle", {[](BrushSettings &s) { s.curveInterpolation = true; s.smoothing = 80; }, 10.0}},
        {"taperLive", {[](BrushSettings &s) { s.taperEnd = 120; }, true, true}},
        {"smoothingSpeedAdapt", {[](BrushSettings &s) { s.smoothing = 80; }, 1.0}},
        // Dry media (milestone 11), on the dry stamp with a strong tooth
        {"dryDepth", {dry, 0.3}},
        {"pressureDepth", {dry, QVariantMap{{"enabled", true}, {"minimum", 0.0}, {"gamma", 2.5}}}},
        {"drySoftness", {dry, 0.45}},
        {"dryStreaks", {dry, 0.8}},
        {"dryStreakLength", {[=](BrushSettings &s) { dry(s); s.dryStreaks = 0.8; }, 14.0}},
        {"dryStreakScale", {[=](BrushSettings &s) { dry(s); s.dryStreaks = 0.8; }, 3.0}},
        {"dryEdgeBreakup", {dry, 1.0}},
        {"dryGrit", {dry, 0.5}},
        {"dryTiltDepth", {dry, 0.2}},
    };
}


QString goldenPath(const QString &name)
{
    return QStringLiteral(DRAWAPP_SOURCE_DIR "/tests/golden/") + name;
}

bool writeGolden()
{
    return qEnvironmentVariableIntValue("DRAWAPP_WRITE_GOLDEN") == 1;
}

// Milestone 11: golden images of new presets are written only when missing
// (DRAWAPP_WRITE_NEW_GOLDEN=1), so the committed milestone 6-10 references are never regenerated.
bool writeMissingGolden(const QString &path)
{
    return writeGolden() || (!QFile::exists(path) && qEnvironmentVariableIntValue("DRAWAPP_WRITE_NEW_GOLDEN") == 1);
}

// A tip computed pixel by pixel (no QPainter), so it is the same on every platform.
QImage mathTip()
{
    QImage tip(32, 32, QImage::Format_Grayscale8);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            tip.scanLine(y)[x] = uchar(x + y < 40 && x > 2 && y > 2 ? 255 - 4 * x : 0);
    return tip;
}

// The golden preset: most new properties away from their defaults.
BrushSettings goldenBrush()
{
    BrushSettings s;
    s.name = QStringLiteral("Golden wet ink");
    s.blend = BrushBlend::WetPaint;
    s.size = 18;
    s.tipImage = mathTip();
    s.spacing = 0.12;
    s.scatter = 0.2;
    s.count = 2;
    s.hueJitter = 12;
    s.texture = QStringLiteral("canvas");
    s.textureStrength = 0.35;
    s.textureRotation = 30;
    s.taperStart = 40;
    s.taperEnd = 60;
    s.wetLoad = 0.8;
    s.wetLoadLength = 400;
    s.wetPull = 0.4;
    s.pressureSize = {true, 0.2, 1.4};
    ParameterDynamics d;
    d.enabled = true;
    d.minimum = 0.25;
    d.combine = CombineMode::Max;
    d.mappings = {{Sensor::Pressure, SensorCurve::exponential(1.8), 0}, {Sensor::Speed, SensorCurve::inverted(), 3.0}};
    s.dynamicsFor(DynamicTarget::Opacity) = d;
    return s.sanitized();
}

// Compares two preset JSON objects; the embedded tip image by pixels (PNG bytes may differ
// between zlib versions).
bool sameJson(QJsonObject a, QJsonObject b, QString *why)
{
    const QJsonValue ta = a.take("tipImage"), tb = b.take("tipImage");
    bool okA = false, okB = false;
    if (BrushProperties::decodeImage(ta.toString(), &okA).convertToFormat(QImage::Format_Grayscale8)
        != BrushProperties::decodeImage(tb.toString(), &okB).convertToFormat(QImage::Format_Grayscale8)) {
        *why = QStringLiteral("tip image differs");
        return false;
    }
    if (a != b) {
        *why = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)) + QStringLiteral(" vs ")
             + QString::fromUtf8(QJsonDocument(b).toJson(QJsonDocument::Compact));
        return false;
    }
    return true;
}

// Images equal within a small tolerance (platform floating-point differences): at most maxShare
// of the pixels may differ, and none by more than maxDelta in any channel.
bool closeImages(const QImage &a, const QImage &b, int maxDelta, double maxShare, QString *why)
{
    if (a.size() != b.size()) {
        *why = QStringLiteral("size differs");
        return false;
    }
    int differ = 0, worst = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            const int d = std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                                    std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
            if (d > 0)
                ++differ;
            worst = std::max(worst, d);
        }
    const double share = double(differ) / (a.width() * a.height());
    *why = QStringLiteral("%1 % of pixels differ, by up to %2").arg(share * 100, 0, 'f', 2).arg(worst);
    return share <= maxShare && worst <= maxDelta;
}

} // namespace

class TestBrushEngine : public QObject
{
    Q_OBJECT

private slots:
    // --- sensor curves ---

    void sensorCurves()
    {
        const SensorCurve lin;
        for (double x : {0.0, 0.1, 0.37, 0.5, 0.99, 1.0})
            QVERIFY(std::abs(lin.value(x) - x) < 1e-9);
        QVERIFY(std::abs(SensorCurve::inverted().value(0.25) - 0.75) < 1e-9);
        const SensorCurve step = SensorCurve::threshold(0.5);
        QCOMPARE(step.value(0.3), 0.0);
        QCOMPARE(step.value(0.6), 1.0);
        const SensorCurve firm = SensorCurve::exponential(2.0);
        QVERIFY(std::abs(firm.value(0.5) - 0.25) < 0.01);
        const SensorCurve band = SensorCurve::clamped(0.2, 0.8);
        QVERIFY(std::abs(band.value(0.0) - 0.2) < 1e-9);
        QVERIFY(std::abs(band.value(1.0) - 0.8) < 1e-9);
        for (const SensorCurve &c : {firm, step, band, SensorCurve::exponential(0.4)}) {
            double last = -1;
            for (int i = 0; i <= 200; ++i) {
                const double y = c.value(i / 200.0);
                QVERIFY(y >= last - 1e-12); // these presets are monotone
                last = y;
            }
        }
        // Monotone cubic: never overshoots its points, even for a spike.
        const SensorCurve spike({{0, 0}, {0.1, 1}, {0.2, 0}, {1, 1}});
        for (int i = 0; i <= 1000; ++i) {
            const double x = i / 1000.0, y = spike.value(x);
            QVERIFY(y >= 0 && y <= 1);
            if (x > 0.1 && x < 0.2)
                QVERIFY(y <= 1.0 && y >= 0.0);
        }
        // Sanitising: sorted, NaN dropped, at most 16 points, and never fewer than 2.
        const SensorCurve messy({{1, 1}, {0, 0}, {0.5, std::nan("")}, {0.5, 0.3}});
        QCOMPARE(messy.points().size(), size_t(3));
        QCOMPARE(messy.points()[1], QPointF(0.5, 0.3));
        std::vector<QPointF> many;
        for (int i = 0; i < 30; ++i)
            many.emplace_back(i / 29.0, 0.5);
        QCOMPARE(SensorCurve(many).points().size(), size_t(SensorCurve::MaxPoints));
        QCOMPARE(SensorCurve({{0.5, 0.5}}).points().size(), size_t(2));
        // JSON
        SensorCurve back;
        QVERIFY(SensorCurve::fromJson(firm.toJson(), &back));
        QCOMPARE(back, firm);
        QVERIFY(!SensorCurve::fromJson(QJsonArray{QJsonArray{0, 0}}, &back));
        QVERIFY(!SensorCurve::fromJson(QJsonArray{QJsonArray{0, "a"}, QJsonArray{1, 1}}, &back));
        QVERIFY(!SensorCurve::fromJson(QJsonArray{1, 2}, &back));
    }

    void dynamicsCombination()
    {
        ParameterDynamics d;
        d.enabled = true;
        d.mappings = {{Sensor::Pressure, SensorCurve(), 0}, {Sensor::TiltMagnitude, SensorCurve(), 0}};
        SensorReadings r;
        r.set(Sensor::Pressure, 0.5);
        r.set(Sensor::TiltMagnitude, 30); // degrees: 0.5 of full tilt
        auto eval = [&](CombineMode mode, double minimum = 0) {
            ParameterDynamics x = d;
            x.combine = mode;
            x.minimum = minimum;
            double v = -1;
            const bool ok = CompiledDynamics(x, DynamicTarget::Size).evaluate(r, &v);
            return ok ? v : -1.0;
        };
        QVERIFY(std::abs(eval(CombineMode::Multiply) - 0.25) < 0.01);
        QVERIFY(std::abs(eval(CombineMode::Add) - 1.0) < 0.01);
        QVERIFY(std::abs(eval(CombineMode::Max) - 0.5) < 0.01);
        QVERIFY(std::abs(eval(CombineMode::Min) - 0.5) < 0.01);
        QVERIFY(std::abs(eval(CombineMode::Difference)) < 0.01);
        QVERIFY(std::abs(eval(CombineMode::Multiply, 0.5) - 0.625) < 0.01);
        // A sensor without data is left out; none at all: no value (the parameter keeps its base).
        r.available[size_t(Sensor::TiltMagnitude)] = false;
        QVERIFY(std::abs(eval(CombineMode::Multiply) - 0.5) < 0.01);
        r.available[size_t(Sensor::Pressure)] = false;
        QCOMPARE(eval(CombineMode::Multiply), -1.0);
        d.enabled = false;
        r.set(Sensor::Pressure, 0.5);
        QCOMPARE(eval(CombineMode::Multiply), -1.0);
        // Random: repeatable per (seed, dab, parameter), different across dabs and parameters.
        ParameterDynamics rnd;
        rnd.enabled = true;
        rnd.mappings = {{Sensor::Random, SensorCurve(), 0}};
        SensorReadings a;
        a.seed = 9;
        a.dabIndex = 3;
        double v1, v2, v3, v4;
        QVERIFY(CompiledDynamics(rnd, DynamicTarget::Size).evaluate(a, &v1));
        QVERIFY(CompiledDynamics(rnd, DynamicTarget::Size).evaluate(a, &v2));
        QCOMPARE(v1, v2);
        QVERIFY(CompiledDynamics(rnd, DynamicTarget::Opacity).evaluate(a, &v3));
        QVERIFY(v3 != v1);
        a.dabIndex = 4;
        QVERIFY(CompiledDynamics(rnd, DynamicTarget::Size).evaluate(a, &v4));
        QVERIFY(v4 != v1);
    }

    void dynamicsJson()
    {
        ParameterDynamics d;
        d.enabled = true;
        d.minimum = 0.2;
        d.combine = CombineMode::Max;
        d.mappings = {{Sensor::Speed, SensorCurve::inverted(), 4.0}, {Sensor::Random, SensorCurve::threshold(0.7), 0}};
        ParameterDynamics back;
        QString error;
        QVERIFY(ParameterDynamics::fromJson(d.toJson(), &back, &error));
        QCOMPARE(back, d);
        QJsonObject bad = d.toJson();
        bad["combine"] = "sometimes";
        QVERIFY(!ParameterDynamics::fromJson(bad, &back, &error));
        bad = d.toJson();
        bad["sensors"] = QJsonArray{QJsonObject{{"sensor", "mood"}}};
        QVERIFY(!ParameterDynamics::fromJson(bad, &back, &error));
        QVERIFY(error.contains("mood"));
        // Ranges are clamped; mappings beyond the maximum are dropped.
        bad = d.toJson();
        bad["sensors"] = QJsonArray{QJsonObject{{"sensor", "speed"}, {"range", 1e9}}, QJsonObject{{"sensor", "random"}},
                                    QJsonObject{{"sensor", "random"}}, QJsonObject{{"sensor", "random"}},
                                    QJsonObject{{"sensor", "random"}}};
        QVERIFY(ParameterDynamics::fromJson(bad, &back, &error));
        QCOMPARE(int(back.mappings.size()), ParameterDynamics::MaxMappings);
        QCOMPARE(back.mappings[0].range, Sensors::maxRange(Sensor::Speed));
    }

    // --- sensors driving parameters (simulated input) ---

    void dynamicsPressureToSize()
    {
        BrushSettings s = baseSettings();
        s.size = 40;
        s.pressureSize.enabled = false;
        set(s, "dynamics.size", dynamicsMap(Sensor::Pressure));
        auto pressure = [](double t) { return t < 0.5 ? 0.25 : 1.0; };
        const QImage img = render(s, line({30, 100}, {370, 100}, 170, pressure), transparentCanvas());
        QVERIFY(std::abs(columnHeight(img, 100) - 10) <= 2);
        QVERIFY(std::abs(columnHeight(img, 300) - 40) <= 2);
        // A device without pressure: no reading, so the size stays the brush size.
        auto noPressure = line({30, 100}, {370, 100}, 170, pressure);
        for (StrokeSample &x : noPressure)
            x.hasPressure = false;
        const QImage flat = render(s, noPressure, transparentCanvas());
        QVERIFY(std::abs(columnHeight(flat, 100) - 40) <= 2);
        // Inverted curve: light pressure is large.
        set(s, "dynamics.size", dynamicsMap(Sensor::Pressure, SensorCurve::inverted(), 0.2));
        const QImage inv = render(s, line({30, 100}, {370, 100}, 170, pressure), transparentCanvas());
        QVERIFY(columnHeight(inv, 100) > columnHeight(inv, 300));
    }

    void dynamicsPressureToOpacityAndFlow()
    {
        BrushSettings s = baseSettings();
        s.pressureSize.enabled = false;
        set(s, "dynamics.opacity", dynamicsMap(Sensor::Pressure));
        auto pressure = [](double t) { return t < 0.5 ? 0.3 : 1.0; };
        const auto stroke = line({30, 100}, {370, 100}, 170, pressure);
        const QImage img = render(s, stroke, transparentCanvas());
        QVERIFY(std::abs(columnMaxAlpha(img, 100) - 77) <= 2); // round(0.3 × 255)
        QCOMPARE(columnMaxAlpha(img, 300), 255);
        // Flow: a build-up brush deposits less per pass at light pressure, below the ceiling.
        BrushSettings f = baseSettings();
        f.pressureSize.enabled = false;
        f.accumulation = Accumulation::BuildUp;
        f.flow = 0.6;
        set(f, "dynamics.flow", dynamicsMap(Sensor::Pressure));
        const QImage flow = render(f, stroke, transparentCanvas());
        QVERIFY(columnMaxAlpha(flow, 100) < columnMaxAlpha(flow, 300));
        // One pass deposits the flow along the centre, within the discreteness of the dab count.
        QVERIFY2(columnMaxAlpha(flow, 300) <= 255 * 0.6 + 6,
                 qPrintable(QString("%1 %2").arg(columnMaxAlpha(flow, 100)).arg(columnMaxAlpha(flow, 300))));
    }

    void dynamicsTiltToRotation()
    {
        BrushSettings s = baseSettings();
        s.size = 60;
        s.roundness = 0.2;
        s.pressureSize.enabled = false;
        set(s, "dynamics.rotation", dynamicsMap(Sensor::TiltDirection));
        auto dot = [&](double tiltX, double tiltY) {
            StrokeSample a = sample(200, 100);
            a.hasTilt = true;
            a.tiltX = tiltX;
            a.tiltY = tiltY;
            return render(s, {a}, transparentCanvas());
        };
        auto extent = [](const QImage &img) {
            QRect r;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) > 127)
                        r |= QRect(x, y, 1, 1);
            return r;
        };
        const QRect east = extent(dot(40, 0)); // azimuth 0: tip along x
        const QRect south = extent(dot(0, 40)); // azimuth 90°: tip along y
        QVERIFY(east.width() > east.height() * 3);
        QVERIFY(south.height() > south.width() * 3);
        // Without tilt data the angle is the brush's own (along x).
        const QRect none = extent(render(s, {sample(200, 100)}, transparentCanvas()));
        QVERIFY(none.width() > none.height() * 3);
    }

    void dynamicsSpeedToSpacing()
    {
        BrushSettings s = baseSettings();
        s.spacing = 0.5;
        // Spacing follows speed: slow strokes place dabs close together, fast ones far apart.
        set(s, "dynamics.spacing", dynamicsMap(Sensor::Speed, SensorCurve::linear(), 0.05));
        qint64 slow = 0, fast = 0, untimed = 0;
        render(s, line({30, 100}, {370, 100}, 170, nullptr, 20.0), transparentCanvas(), 7, Paint, QImage(), &slow);
        render(s, line({30, 100}, {370, 100}, 170, nullptr, 1.0), transparentCanvas(), 7, Paint, QImage(), &fast);
        render(s, line({30, 100}, {370, 100}, 170), transparentCanvas(), 7, Paint, QImage(), &untimed);
        QVERIFY2(slow > fast * 3, qPrintable(QString("slow %1, fast %2").arg(slow).arg(fast)));
        // Without event times there is no speed: the spacing is the brush's own.
        QVERIFY(std::abs(untimed - (340.0 / 12.0 + 1)) <= 2);
    }

    void dynamicsAreStableAtExtremes()
    {
        BrushSettings s = baseSettings();
        set(s, "dynamics.size", dynamicsMap(Sensor::Speed));
        set(s, "dynamics.opacity", dynamicsMap(Sensor::Pressure));
        set(s, "dynamics.spacing", dynamicsMap(Sensor::Acceleration, SensorCurve::inverted()));
        set(s, "dynamics.rotation", dynamicsMap(Sensor::Direction));
        s.minimumSize = 2;
        std::vector<StrokeSample> v{sample(10, 10, 0.0, 0), sample(20, 10, std::nan(""), 0), // duplicate time
                                    sample(30, 10, 0.0, -5),                                 // time going back
                                    sample(390, 190, 1.0, 1),                                // 380 px in 1 ms
                                    sample(390, 190, 1.0, 1), sample(10, 190, 2.0, 1e9), sample(200, 100, -1, 1e9 + 1)};
        qint64 dabs = 0;
        const QImage img = render(s, v, transparentCanvas(), 7, Paint, QImage(), &dabs);
        QVERIFY(validPremultiplied(img));
        QVERIFY(dabs > 0 && dabs < 20000);
        // Zero pressure with a pressure-driven size never gives a zero or negative dab.
        BrushSettings z = baseSettings();
        set(z, "dynamics.size", dynamicsMap(Sensor::Pressure));
        const QImage zero = render(z, line({30, 100}, {370, 100}, 100, [](double) { return 0.0; }), transparentCanvas());
        QVERIFY(validPremultiplied(zero));
        QVERIFY(columnMaxAlpha(zero, 200) > 0); // minimum size 1 px still draws
    }

    void randomSeedReproducibility()
    {
        BrushSettings s = baseSettings();
        s.sizeJitter = 0.5;
        s.scatter = 0.8;
        s.hueJitter = 60;
        s.tipDensity = 0.6;
        set(s, "dynamics.rotation", dynamicsMap(Sensor::Random));
        s.roundness = 0.4;
        const auto stroke = standardStroke();
        const QImage a = render(s, stroke, stripes(), 3);
        const QImage b = render(s, stroke, stripes(), 3);
        QCOMPARE(a, b);
        QVERIFY(differingPixels(a, render(s, stroke, stripes(), 4)) > 100);
        // Splitting the input differently gives the same pixels.
        QImage split = stripes();
        TileSnapshot before;
        StrokeEngine e(s, Paint, 3, 0);
        e.begin(split, stroke.front(), before);
        for (size_t i = 1; i < stroke.size(); ++i) {
            if (i % 2 == 0) { // an extra event half way along each segment
                StrokeSample mid = StrokeSample::lerp(stroke[i - 1], stroke[i], 0.5);
                e.moveTo(split, mid, before);
            }
            e.moveTo(split, stroke[i], before);
        }
        e.finish(split, before);
        QVERIFY(differingPixels(split, a) < a.width() * a.height() / 50); // only the spacing phase may shift
    }

    void colorDynamicsVaryPerDab()
    {
        BrushSettings s = baseSettings();
        s.size = 30;
        const auto stroke = line({30, 100}, {370, 100}, 170);
        const QImage plain = render(s, stroke, transparentCanvas());
        s.hueJitter = 180;
        const QImage varied = render(s, stroke, transparentCanvas());
        QSet<int> hues;
        for (int x = 30; x < 370; x += 2) {
            const QColor c = QColor::fromRgba(varied.pixel(x, 100));
            if (c.alpha() == 255)
                hues.insert(c.hslHue() / 10);
        }
        QVERIFY2(hues.size() > 8, qPrintable(QString::number(hues.size())));
        // Colour changes never change coverage.
        for (int y = 0; y < plain.height(); ++y)
            for (int x = 0; x < plain.width(); ++x)
                QCOMPARE(qAlpha(varied.pixel(x, y)), qAlpha(plain.pixel(x, y)));
        // Pressure → lightness: light pressure is darker (lightness × value).
        BrushSettings l = baseSettings();
        l.pressureSize.enabled = false;
        set(l, "dynamics.lightness", dynamicsMap(Sensor::Pressure, SensorCurve(), 0.2));
        const QImage lit = render(l, line({30, 100}, {370, 100}, 170, [](double t) { return t < 0.5 ? 0.0 : 1.0; }),
                                  transparentCanvas());
        QVERIFY(QColor::fromRgba(lit.pixel(100, 100)).lightness() < QColor::fromRgba(lit.pixel(300, 100)).lightness());
        QCOMPARE(QColor::fromRgba(lit.pixel(300, 100)).rgb(), Paint.rgb());
    }

    void taperStartAndEnd()
    {
        BrushSettings s = baseSettings();
        s.size = 30;
        s.pressureSize.enabled = false;
        s.taperStart = 100;
        s.taperEnd = 100;
        const auto stroke = line({30, 100}, {370, 100}, 170);
        const QImage img = render(s, stroke, transparentCanvas());
        QVERIFY(columnHeight(img, 40) < 10);
        QVERIFY(std::abs(columnHeight(img, 200) - 30) <= 1);
        QVERIFY(columnHeight(img, 360) < 10);
        QVERIFY(columnHeight(img, 80) < columnHeight(img, 120));
        QVERIFY(columnHeight(img, 320) < columnHeight(img, 280));
        // The end is held back until the pen lifts, then drawn with the taper.
        QImage live = transparentCanvas();
        TileSnapshot before;
        StrokeEngine e(s, Paint, 7, 0);
        e.begin(live, stroke.front(), before);
        for (size_t i = 1; i < stroke.size(); ++i)
            e.moveTo(live, stroke[i], before);
        QCOMPARE(columnMaxAlpha(live, 330), 0);
        QVERIFY(columnMaxAlpha(live, 200) > 0);
        e.finish(live, before);
        QCOMPARE(live, img);
    }

    void sprayAndHatching()
    {
        BrushSettings spray = baseSettings();
        spray.size = 60;
        spray.stamp = BrushStamp::Spray;
        spray.sprayDensity = 40;
        spray.sprayParticleSize = 2;
        spray.spacing = 2.0; // separate dabs
        const QImage dots = render(spray, line({60, 100}, {340, 100}, 20), transparentCanvas());
        int covered = 0, area = 0;
        for (int y = 70; y < 130; ++y)
            for (int x = 60; x < 340; ++x) {
                ++area;
                covered += qAlpha(dots.pixel(x, y)) > 0;
            }
        QVERIFY(covered > area / 50 && covered < area / 2);
        QCOMPARE(dots, render(spray, line({60, 100}, {340, 100}, 20), transparentCanvas())); // deterministic

        BrushSettings hatch = baseSettings();
        hatch.size = 120;
        hatch.stamp = BrushStamp::Hatching;
        hatch.hatchAngle = 0; // horizontal lines
        hatch.hatchSpacing = 10;
        hatch.hatchThickness = 2;
        const QImage lines = render(hatch, line({100, 100}, {300, 100}, 50), transparentCanvas());
        // Down a column the alpha alternates with the line period.
        int runs = 0;
        bool inLine = false;
        for (int y = 50; y < 150; ++y) {
            const bool on = qAlpha(lines.pixel(200, y)) > 127;
            if (on && !inLine)
                ++runs;
            inLine = on;
        }
        QVERIFY2(runs >= 9 && runs <= 11, qPrintable(QString::number(runs)));
    }

    void paintModes()
    {
        QImage bg = transparentCanvas();
        QPainter(&bg).fillRect(QRect(0, 0, 200, 200), QColor(128, 128, 128));
        BrushSettings s = baseSettings();
        s.size = 30;
        const auto stroke = line({20, 100}, {380, 100}, 180);
        auto at = [&](const char *mode, int x) {
            BrushSettings m = s;
            set(m, "paintMode", QString::fromLatin1(mode));
            return QColor::fromRgba(render(m, stroke, bg).pixel(x, 100));
        };
        QCOMPARE(at("normal", 100).rgb(), Paint.rgb());
        const QColor mul = at("multiply", 100);
        QVERIFY(std::abs(mul.red() - 200 * 128 / 255) <= 1 && std::abs(mul.blue() - 40 * 128 / 255) <= 1);
        QCOMPARE(at("multiply", 300).rgb(), Paint.rgb()); // over transparency: the colour itself
        const QColor scr = at("screen", 100);
        QVERIFY(scr.red() > 200 && scr.blue() > 128);
        QCOMPARE(at("behind", 100).rgb(), QColor(128, 128, 128).rgb()); // opaque paint stays in front
        QCOMPARE(at("behind", 300).rgb(), Paint.rgb());
        QCOMPARE(at("preserveAlpha", 100).rgb(), Paint.rgb());
        QCOMPARE(at("preserveAlpha", 300).alpha(), 0); // transparency stays transparent
    }

    void smudgeModes()
    {
        BrushSettings s = baseSettings();
        s.blend = BrushBlend::Smudge;
        s.size = 40;
        s.smudgeStrength = 0.9;
        const auto stroke = line({20, 100}, {300, 100}, 140);
        const QImage smear = render(s, stroke, stripes());
        s.smudgeMode = SmudgeMode::Dulling;
        const QImage dull = render(s, stroke, stripes());
        QVERIFY(differingPixels(smear, dull) > 1000);
        // Dulling carries one colour: across the stroke the smudged pixels are more alike.
        auto spread = [](const QImage &img, int x) {
            int lo = 255, hi = 0;
            for (int y = 90; y <= 110; ++y) {
                lo = std::min(lo, qRed(img.pixel(x, y)));
                hi = std::max(hi, qRed(img.pixel(x, y)));
            }
            return hi - lo;
        };
        QVERIFY(spread(dull, 150) <= spread(smear, 150) + 2);
        // Colour rate paints the brush colour in while smudging.
        BrushSettings tint = baseSettings();
        tint.blend = BrushBlend::Smudge;
        tint.size = 40;
        tint.smudgeColorRate = 0.8;
        const QImage tinted = render(tint, stroke, stripes(), 7, QColor(0, 255, 0));
        QVERIFY(qGreen(tinted.pixel(250, 100)) > qGreen(smear.pixel(250, 100)) + 30);
    }

    void wetPaintLoadsDepositsAndDrags()
    {
        BrushSettings s = baseSettings();
        s.blend = BrushBlend::WetPaint;
        s.size = 30;
        s.pressureSize.enabled = false;
        s.wetLoadLength = 120;
        s.wetAttack = 0.8;
        const auto stroke = line({20, 100}, {380, 100}, 180);
        const QImage onEmpty = render(s, stroke, transparentCanvas());
        // Charged: brush colour at the start; the load runs out, so the far end gets nothing new.
        const QColor start = QColor::fromRgba(qUnpremultiply(onEmpty.pixel(40, 100)));
        // Attack 0.8 per pass, less what the pull takes back up from the empty layer.
        QVERIFY2(start.alpha() > 170, qPrintable(QString("alpha %1 at 40, %2 at 100, %3 at 360").arg(start.alpha())
                                                    .arg(qAlpha(onEmpty.pixel(100, 100))).arg(qAlpha(onEmpty.pixel(360, 100)))));
        QVERIFY(std::abs(start.red() - Paint.red()) < 30 && std::abs(start.green() - Paint.green()) < 30);
        QVERIFY(qAlpha(onEmpty.pixel(360, 100)) < qAlpha(onEmpty.pixel(40, 100)) / 2);
        // Dilution thins the paint.
        BrushSettings thin = s;
        thin.wetDilution = 0.7;
        QVERIFY(qAlpha(render(thin, stroke, transparentCanvas()).pixel(40, 100)) < start.alpha() - 60);
        // Pull drags canvas paint: after the red band, the brush lays down reddish paint.
        BrushSettings drag = s;
        drag.wetLoad = 0;
        drag.wetPull = 0.6;
        const QImage dragged = render(drag, stroke, stripes());
        QVERIFY(qRed(dragged.pixel(45, 100)) > 60 && qAlpha(dragged.pixel(45, 100)) > 60); // into the transparent band
        QVERIFY(validPremultiplied(dragged));
        // A feathered selection is applied once: nothing changes where it is 0.
        QImage clip(400, 200, QImage::Format_Grayscale8);
        clip.fill(0);
        QPainter(&clip).fillRect(QRect(100, 0, 200, 200), QColor(255, 255, 255));
        const QImage clipped = render(drag, stroke, stripes(), 7, Paint, clip);
        const QImage bg = stripes();
        for (int x : {50, 90, 310, 350})
            QCOMPARE(clipped.pixel(x, 100), bg.pixel(x, 100));
        QVERIFY(clipped.pixel(150, 100) != bg.pixel(150, 100));
    }

    void importedTipIsUsedAndSaved()
    {
        const QImage tip = BrushProperties::tipFromImage(asymmetricImage());
        QCOMPARE(tip.format(), QImage::Format_Grayscale8);
        QCOMPARE(tip.width(), tip.height());
        QVERIFY(tip.pixelColor(10, 10).red() > 200 && tip.pixelColor(50, 50).red() == 0);
        // An opaque image uses darkness.
        QImage opaque(32, 16, QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter(&opaque).fillRect(QRect(0, 0, 8, 16), Qt::black);
        const QImage fromDark = BrushProperties::tipFromImage(opaque);
        QCOMPARE(fromDark.size(), QSize(32, 32));
        QVERIFY(fromDark.pixelColor(2, 16).red() == 255 && fromDark.pixelColor(20, 16).red() == 0);
        QVERIFY(BrushProperties::tipFromImage(QImage(1000, 600, QImage::Format_ARGB32)).width() <= BrushSettings::MaxTipImageSize);

        BrushSettings s = baseSettings();
        s.size = 64;
        s.tipImage = tip;
        const QImage dot = render(s, {sample(200, 100)}, transparentCanvas());
        // The triangle's corner is upper left: more paint there than lower right.
        QVERIFY(qAlpha(dot.pixel(180, 80)) > 200);
        QCOMPARE(qAlpha(dot.pixel(220, 120)), 0);

        BrushSettings back;
        QString error;
        QStringList warnings;
        QVERIFY(BrushSettings::fromJson(s.toJson(), &back, &error, &warnings));
        QCOMPARE(back.tipImage, s.tipImage);
        QVERIFY(back == s);
        QJsonObject broken = s.toJson();
        broken["tipImage"] = "not a png!";
        QVERIFY(BrushSettings::fromJson(broken, &back, &error, &warnings));
        QVERIFY(back.tipImage.isNull());
        QVERIFY(!warnings.isEmpty());
    }

    // --- presets ---

    void presetFormat2RoundTripAndUnknownKeys()
    {
        BrushSettings s = baseSettings();
        s.blend = BrushBlend::WetPaint;
        s.scatter = 0.5;
        s.count = 3;
        s.hueJitter = 30;
        s.taperEnd = 80;
        s.textureRotation = 30;
        s.paintMode = PaintMode::Screen;
        s.smudgeMode = SmudgeMode::Dulling;
        set(s, "dynamics.size", dynamicsMap(Sensor::Speed, SensorCurve::exponential(2), 0.3));
        set(s, "dynamics.hue", dynamicsMap(Sensor::Random, SensorCurve::threshold(0.4)));
        BrushSettings back;
        QString error;
        QVERIFY(BrushSettings::fromJson(s.toJson(), &back, &error));
        QVERIFY(back == s);
        QCOMPARE(back.toJson(), s.toJson());
        // Keys from a newer version survive a round trip, at the top level and inside dynamics.
        QJsonObject future = s.toJson();
        future["glitter"] = QJsonObject{{"amount", 0.5}};
        QJsonObject dyn = future["dynamics"].toObject();
        dyn["sparkle"] = QJsonObject{{"enabled", true}};
        future["dynamics"] = dyn;
        QVERIFY(BrushSettings::fromJson(future, &back, &error));
        QCOMPARE(back.toJson()["glitter"], future["glitter"]);
        QCOMPARE(back.toJson()["dynamics"].toObject()["sparkle"], dyn["sparkle"]);
        QVERIFY(back.toJson()["dynamics"].toObject().contains("size"));
        // Version 1 settings (milestone 6) load with the new properties at their defaults.
        const QJsonObject v1{{"name", "Old ink"}, {"size", 9.0}, {"pressureSize", QJsonObject{{"enabled", true}, {"minimum", 0.2}, {"gamma", 1.5}}},
                             {"accumulation", "buildUp"}, {"texture", "paper"}, {"textureStrength", 0.3}};
        QVERIFY(BrushSettings::fromJson(v1, &back, &error));
        QCOMPARE(back.size, 9.0);
        QCOMPARE(back.pressureSize.gamma, 1.5);
        QCOMPARE(back.count, 1);
        QVERIFY(!back.dynamicsFor(DynamicTarget::Size).enabled);
        // Wrong types and unknown option names are refused; unknown resource ids only warned about.
        QJsonObject bad = s.toJson();
        bad["count"] = "three";
        QVERIFY(!BrushSettings::fromJson(bad, &back, &error));
        bad = s.toJson();
        bad["paintMode"] = "glow";
        QVERIFY(!BrushSettings::fromJson(bad, &back, &error));
        bad = s.toJson();
        bad["dynamics"] = QJsonObject{{"size", QJsonObject{{"sensors", QJsonArray{QJsonObject{{"sensor", "mood"}}}}}}};
        QVERIFY(!BrushSettings::fromJson(bad, &back, &error));
        QStringList warnings;
        bad = s.toJson();
        bad["tip"] = "future-tip";
        QVERIFY(BrushSettings::fromJson(bad, &back, &error, &warnings));
        QCOMPARE(back.tip, QStringLiteral("future-tip")); // kept, drawn round
        QVERIFY(!warnings.isEmpty());
        // Out-of-range values are clamped.
        bad = s.toJson();
        bad["count"] = 1000;
        bad["scatter"] = -3;
        QVERIFY(BrushSettings::fromJson(bad, &back, &error));
        QCOMPARE(back.count, 16);
        QCOMPARE(back.scatter, 0.0);
    }

    void malformedPresetFuzz()
    {
        // Random type swaps and values in a full preset: never a crash, and anything accepted is
        // within range and renders.
        BrushSettings s = baseSettings();
        set(s, "dynamics.size", dynamicsMap(Sensor::Pressure));
        const QJsonObject valid = s.toJson();
        const QStringList keys = valid.keys();
        QRandomGenerator rng(1234);
        const QJsonValue junk[] = {QJsonValue(), QJsonValue(true), QJsonValue(-1e300), QJsonValue(1e300),
                                   QJsonValue(QStringLiteral("x")), QJsonValue(QJsonArray{1, 2}), QJsonValue(QJsonObject{{"a", 1}}),
                                   QJsonValue(0.5), QJsonValue(qQNaN())};
        int accepted = 0;
        for (int i = 0; i < 400; ++i) {
            QJsonObject o = valid;
            const int changes = 1 + int(rng.bounded(3));
            for (int k = 0; k < changes; ++k)
                o[keys[int(rng.bounded(keys.size()))]] = junk[rng.bounded(int(std::size(junk)))];
            BrushSettings out;
            QString error;
            QStringList warnings;
            if (!BrushSettings::fromJson(o, &out, &error, &warnings)) {
                QVERIFY(!error.isEmpty());
                continue;
            }
            ++accepted;
            for (const BrushProperty &p : BrushProperties::all()) {
                if (!p.implemented || (p.type != PropertyType::Number && p.type != PropertyType::Integer))
                    continue;
                const double v = p.get(out).toDouble();
                QVERIFY2(std::isfinite(v) && v >= p.minimum && v <= p.maximum, qPrintable(p.id));
            }
            if (i % 20 == 0)
                QVERIFY(validPremultiplied(render(out, line({10, 50}, {120, 60}, 10), transparentCanvas({140, 100}))));
        }
        QVERIFY2(accepted > 10, qPrintable(QString::number(accepted)));
    }

    void randomSettingsFuzz()
    {
        // Every implemented property at random values, including outside its range: rendering
        // stays bounded and valid.
        QRandomGenerator rng(99);
        const auto stroke = standardStroke();
        for (int i = 0; i < 60; ++i) {
            BrushSettings s = baseSettings();
            for (const BrushProperty &p : BrushProperties::all()) {
                if (!p.implemented || rng.bounded(3) != 0)
                    continue;
                switch (p.type) {
                case PropertyType::Number:
                case PropertyType::Integer: {
                    const double span = p.maximum - p.minimum;
                    p.set(s, p.minimum - span * 0.2 + rng.generateDouble() * span * 1.4);
                    break;
                }
                case PropertyType::Bool: p.set(s, rng.bounded(2) == 1); break;
                case PropertyType::Choice: p.set(s, p.choices.value(int(rng.bounded(p.choices.size())))); break;
                case PropertyType::Dynamics:
                    p.set(s, dynamicsMap(Sensor(rng.bounded(SensorCount)), SensorCurve::exponential(0.3 + rng.generateDouble() * 3)));
                    break;
                default: break;
                }
            }
            s = s.sanitized();
            s.size = std::min(s.size, 80.0); // keep the test quick
            s.sprayDensity = std::min(s.sprayDensity, 60.0);
            s.smoothing = std::min(s.smoothing, 30);
            qint64 dabs = 0;
            const QImage img = render(s, stroke, stripes(), quint64(i), Paint, QImage(), &dabs);
            QVERIFY(validPremultiplied(img));
            QVERIFY(dabs < 200000);
        }
    }

    // --- the registry ---

    void registryIsComplete()
    {
        const auto &all = BrushProperties::all();
        QCOMPARE(BrushProperties::designTargetCount(), 120);
        const std::map<QString, int> target{{"Brush Tip", 12},        {"Stroke Path", 10},       {"Pressure and Tilt", 12},
                                            {"Opacity and Flow", 8},  {"Texture and Grain", 12}, {"Shape Dynamics", 12},
                                            {"Color Dynamics", 10},   {"Blending and Smudge", 12}, {"Wet Paint", 10},
                                            {"Stabilization", 8},     {"Taper", 8},              {"Special Effects", 6}};
        for (const auto &[group, count] : target)
            QCOMPARE(int(std::count_if(all.begin(), all.end(), [&](const BrushProperty &p) { return p.group == group; })), count);
        QSet<QString> ids;
        int implemented = 0;
        for (const BrushProperty &p : all) {
            QVERIFY2(!ids.contains(p.id), qPrintable(p.id));
            ids.insert(p.id);
            QVERIFY2(!p.name.isEmpty() && !p.description.isEmpty(), qPrintable(p.id));
            if (!p.implemented) {
                QVERIFY2(!p.get && !p.set, qPrintable(p.id));
                QVERIFY2(p.description.contains("Not implemented") || p.description.contains("Planned"), qPrintable(p.id));
                continue;
            }
            if (p.group != QLatin1String("Engine"))
                ++implemented;
            QVERIFY2(p.get && p.set && !p.rendering.isEmpty() && !p.cost.isEmpty() && !p.tests.isEmpty(), qPrintable(p.id));
            QVERIFY2(p.engines != 0, qPrintable(p.id));
            const QVariant d = p.defaultValue();
            if (p.type == PropertyType::Number || p.type == PropertyType::Integer)
                QVERIFY2(d.toDouble() >= p.minimum && d.toDouble() <= p.maximum, qPrintable(p.id));
            if (p.type == PropertyType::Choice)
                QVERIFY2(p.choices.contains(d.toString()) && p.choices.size() == p.choiceLabels.size(), qPrintable(p.id));
        }
        QCOMPARE(implemented, BrushProperties::implementedCount());
        const QStringList design = BrushProperties::designGroups();
        const int inDesign = int(std::count_if(all.begin(), all.end(), [&](const BrushProperty &p) {
            return p.implemented && design.contains(p.group);
        }));
        const int inEngines = implemented - inDesign;
        QCOMPARE(BrushProperties::engineGroupCount(),
                 int(std::count_if(all.begin(), all.end(), [&](const BrushProperty &p) {
                     return BrushProperties::engineGroups().contains(p.group);
                 })));
        qInfo("Implemented brush properties: %d of the %d in the milestone 9 design target, plus %d of %d in the "
              "milestone 11 engine groups",
              inDesign, BrushProperties::designTargetCount(), inEngines, BrushProperties::engineGroupCount());
    }

    void everyImplementedPropertyChangesRendering()
    {
        const auto table = scenarios();
        const auto stroke = standardStroke();
        int checked = 0;
        for (const BrushProperty &p : BrushProperties::all()) {
            if (!p.implemented)
                continue;
            const auto it = table.find(p.id);
            QVERIFY2(it != table.end(), qPrintable(QStringLiteral("no rendering scenario for ") + p.id));
            BrushSettings base = baseSettings();
            const unsigned engine = testEngine(p.engines);
            applyEngine(base, engine);
            it->second.setup(base);
            base = base.sanitized();
            BrushSettings changed = base;
            p.set(changed, it->second.changed);
            changed = changed.sanitized();
            QVERIFY2(changed != base, qPrintable(QStringLiteral("%1: the changed value equals the default").arg(p.id)));
            QVERIFY2(BrushProperties::appliesTo(p, base), qPrintable(p.id));
            const bool whileDrawing = it->second.whileDrawing;
            const QImage a = whileDrawing ? renderUnfinished(base, stroke, stripes()) : render(base, stroke, stripes());
            const QImage b = whileDrawing ? renderUnfinished(changed, stroke, stripes()) : render(changed, stroke, stripes());
            QVERIFY(validPremultiplied(b));
            const int diff = differingPixels(a, b);
            QVERIFY2(diff > 0, qPrintable(QStringLiteral("%1 has no visible effect").arg(p.id)));
            ++checked;
        }
        qInfo("Properties with a demonstrated rendering effect: %d", checked);
        QCOMPARE(checked, BrushProperties::implementedCount() + 2); // + the two engine selectors
    }

    void specificationIsCurrent()
    {
        const QString path = QStringLiteral(DRAWAPP_SOURCE_DIR "/docs/brush-settings-specification.md");
        const QString spec = BrushProperties::specificationMarkdown();
        if (qEnvironmentVariableIntValue("DRAWAPP_WRITE_SPEC") == 1) {
            QFile out(path);
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(spec.toUtf8());
        }
        QFile in(path);
        QVERIFY2(in.open(QIODevice::ReadOnly), "docs/brush-settings-specification.md is missing (run with DRAWAPP_WRITE_SPEC=1)");
        QVERIFY2(QString::fromUtf8(in.readAll()).replace("\r\n", "\n") == spec,
                 "docs/brush-settings-specification.md is out of date: run tst_brushengine with DRAWAPP_WRITE_SPEC=1");
    }

    // --- preset files and golden data ---

    void presetFileGolden()
    {
        const BrushSettings s = goldenBrush();
        const QByteArray encoded = BrushPresetFile::encode(s);
        const QString path = goldenPath(QStringLiteral("golden-wet-ink.drawbrush"));
        if (writeGolden()) {
            QFile out(path);
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(encoded);
        }
        QFile in(path);
        QVERIFY2(in.open(QIODevice::ReadOnly), "golden file missing (run with DRAWAPP_WRITE_GOLDEN=1)");
        const QByteArray stored = in.readAll();
        // Stable serialisation: today's encoding equals the committed file.
        const QJsonObject now = QJsonDocument::fromJson(encoded).object(), then = QJsonDocument::fromJson(stored).object();
        QCOMPARE(then.value("format").toString(), QStringLiteral("drawapp-brush"));
        QCOMPARE(then.value("version").toInt(), 2);
        QString why;
        QVERIFY2(sameJson(now.value("settings").toObject(), then.value("settings").toObject(), &why), qPrintable(why));
        // Round trip from the committed file.
        BrushSettings back;
        QString error;
        QStringList warnings;
        QVERIFY2(BrushPresetFile::decode(stored, &back, &error, &warnings), qPrintable(error));
        QVERIFY(warnings.isEmpty());
        QVERIFY(back == s);

        // A file from a future version 2 writer with keys we do not know: kept on re-export.
        QJsonObject root = then;
        QJsonObject settings = root.value("settings").toObject();
        settings["sparkleAmount"] = 0.7;
        root["settings"] = settings;
        root["metadata"] = QJsonObject{{"app", "DrawApp 12"}, {"author", "someone"}};
        QVERIFY(BrushPresetFile::decode(QJsonDocument(root).toJson(), &back, &error, &warnings));
        QCOMPARE(QJsonDocument::fromJson(BrushPresetFile::encode(back)).object()["settings"].toObject()["sparkleAmount"].toDouble(), 0.7);

        // Malformed files are refused with a message, never half-loaded.
        const QList<QByteArray> malformed{QByteArray(), QByteArray("{"), QByteArray("[]"),
                                          QByteArray(R"({"format":"drawapp-brushes","version":2})"),
                                          QByteArray(R"({"format":"drawapp-brush","version":3,"settings":{}})"),
                                          QByteArray(R"({"format":"drawapp-brush","version":"2","settings":{}})"),
                                          QByteArray(R"({"format":"drawapp-brush","version":2})"),
                                          QByteArray(R"({"format":"drawapp-brush","version":2,"settings":{"size":"big"}})"),
                                          QByteArray(BrushPresetFile::MaxFileBytes + 1, ' ')};
        for (const QByteArray &bad : malformed) {
            error.clear();
            QVERIFY2(!BrushPresetFile::decode(bad, &back, &error, &warnings), bad.left(80).constData());
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(!BrushPresetFile::decode(R"({"format":"drawapp-brush","version":3,"settings":{}})", &back, &error, &warnings));
        QVERIFY(error.contains("newer"));
        // Missing resources: an unknown texture loads (kept, drawn without it) with a warning.
        warnings.clear();
        QVERIFY(BrushPresetFile::decode(R"({"format":"drawapp-brush","version":2,"settings":{"texture":"marble","textureStrength":0.5}})",
                                        &back, &error, &warnings));
        QCOMPARE(back.texture, QStringLiteral("marble"));
        QVERIFY(!warnings.isEmpty());
        QVERIFY(validPremultiplied(render(back, standardStroke(), stripes())));
    }

    void libraryVersion1Migrates()
    {
        QFile in(goldenPath(QStringLiteral("library-v1.json")));
        QVERIFY(in.open(QIODevice::ReadOnly));
        BrushPresetStore::Data data;
        QString error;
        QStringList warnings;
        QVERIFY2(BrushPresetStore::decode(in.readAll(), &data, &error, &warnings), qPrintable(error));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("; ")));
        QCOMPARE(data.custom.size(), size_t(1));
        const BrushSettings &pencil = data.custom[0].settings;
        QCOMPARE(pencil.name, QStringLiteral("My pencil"));
        QCOMPARE(pencil.size, 6.0);
        QCOMPARE(pencil.pressureSize.gamma, 1.5);
        QCOMPARE(pencil.textureMode, TextureMode::Subtract);
        QCOMPARE(pencil.tiltSize, 1.5);
        // New properties take their defaults, so the brush draws as it did in milestone 6.
        QCOMPARE(pencil.count, 1);
        QCOMPARE(pencil.stamp, BrushStamp::Tip);
        QCOMPARE(pencil.taperEnd, 0.0);
        QVERIFY(!pencil.hasColorDynamics());
        for (const ParameterDynamics &d : pencil.dynamics)
            QVERIFY(!d.enabled);
        QCOMPARE(data.overrides.at("builtin.solid-round").opacity, 0.5);
        QCOMPARE(data.paintId, QStringLiteral("custom.1"));
        QCOMPARE(data.favorites, QStringList({"custom.1", "builtin.clean-ink"}));
        // Written back as version 2, and read again unchanged.
        const QByteArray v2 = BrushPresetStore::encode(data);
        QCOMPARE(QJsonDocument::fromJson(v2).object()["version"].toInt(), 2);
        BrushPresetStore::Data again;
        QVERIFY(BrushPresetStore::decode(v2, &again, &error, &warnings));
        QVERIFY(again.custom[0].settings == pencil);
    }

    void goldenImages()
    {
        // Representative strokes compared with committed reference images. A small tolerance
        // allows for floating-point differences between compilers and CPUs; anything more is a
        // visual regression to investigate (DRAWAPP_WRITE_GOLDEN=1 rewrites them after a
        // deliberate change).
        std::vector<std::pair<QString, BrushSettings>> cases;
        for (const BrushPreset &p : BrushPresets::builtIns())
            cases.emplace_back(p.id.mid(8), p.settings);
        BrushSettings spray = baseSettings();
        spray.stamp = BrushStamp::Spray;
        spray.size = 40;
        cases.emplace_back(QStringLiteral("m9-spray"), spray);
        BrushSettings hatch = baseSettings();
        hatch.stamp = BrushStamp::Hatching;
        hatch.size = 50;
        cases.emplace_back(QStringLiteral("m9-hatching"), hatch);
        BrushSettings color = baseSettings();
        color.hueJitter = 90;
        color.size = 30;
        cases.emplace_back(QStringLiteral("m9-hue-jitter"), color);
        cases.emplace_back(QStringLiteral("m9-golden-wet-ink"), goldenBrush());
        BrushSettings taper = baseSettings();
        taper.size = 30;
        taper.taperStart = 80;
        taper.taperEnd = 80;
        set(taper, "dynamics.size", dynamicsMap(Sensor::Pressure, SensorCurve::exponential(0.6), 0.3));
        cases.emplace_back(QStringLiteral("m9-taper-dynamics"), taper);
        for (const auto &[name, settings] : cases) {
            const QImage img = render(settings, standardStroke(), stripes());
            const QString path = goldenPath(QStringLiteral("stroke-%1.png").arg(name));
            if (writeMissingGolden(path))
                QVERIFY(img.save(path));
            const QImage ref = QImage(path).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QVERIFY2(!ref.isNull(), qPrintable("missing " + path + " (run with DRAWAPP_WRITE_GOLDEN=1)"));
            QString why;
            QVERIFY2(closeImages(img, ref, 3, 0.01, &why), qPrintable(name + ": " + why));
        }
    }

    void tiledLayersPaintExactlyLikeDenseImages()
    {
        // Zero regression for milestone 10: every brush paints the same pixels on a tiled layer as
        // on one dense image, across tile seams and corners, on paint and on transparency, with
        // and without a soft selection; undo restores the layer exactly; and on transparency only
        // the tiles the stroke reaches are allocated.
        std::vector<std::pair<QString, BrushSettings>> cases;
        for (const BrushPreset &p : BrushPresets::builtIns())
            cases.emplace_back(p.id, p.settings);
        BrushSettings spray = baseSettings();
        spray.stamp = BrushStamp::Spray;
        spray.size = 40;
        cases.emplace_back(QStringLiteral("spray"), spray);
        BrushSettings hatch = baseSettings();
        hatch.stamp = BrushStamp::Hatching;
        hatch.size = 50;
        cases.emplace_back(QStringLiteral("hatching"), hatch);
        cases.emplace_back(QStringLiteral("golden wet ink"), goldenBrush());
        QImage clip(400, 200, QImage::Format_Grayscale8);
        clip.fill(0);
        {
            QPainter p(&clip);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255));
            p.drawEllipse(QRectF(60, 30, 230, 140));
        }
        const QImage transparent = transparentCanvas(QSize(400, 200));
        for (const auto &[name, settings] : cases) {
            for (const auto &stroke : {standardStroke(), seamStroke()}) {
                for (const QImage &background : {stripes(), transparent}) {
                    for (const QImage &mask : {QImage(), clip}) {
                        const QString what = QStringLiteral("%1, %2 background%3")
                                                 .arg(name, background.pixel(10, 10) ? "striped" : "transparent",
                                                      mask.isNull() ? "" : ", soft selection");
                        const QImage dense = render(settings, stroke, background, 7, Paint, mask);
                        TileSnapshot before;
                        TiledImage tiled = renderTiled(settings, stroke, background, &before, 7, Paint, mask);
                        QVERIFY2(!tiled.isNull(), qPrintable(what));
                        QVERIFY2(tiled == dense, qPrintable(what));
                        if (!background.pixel(10, 10))
                            QVERIFY2(tiled.allocatedTileCount() < tiled.columns() * tiled.rows(), qPrintable(what));
                        before.swapWith(tiled);
                        QVERIFY2(tiled == background, qPrintable(what + " (undo)"));
                    }
                }
            }
        }
    }

    void tileSeamGoldenImages()
    {
        // Strokes along tile seams and through tile corners, painted on tiled layers, against
        // committed references (milestone 10). Same tolerance as goldenImages().
        for (const BrushPreset &p : BrushPresets::builtIns()) {
            for (const bool onTransparency : {false, true}) {
                const QImage background = onTransparency ? transparentCanvas(QSize(400, 200)) : stripes();
                TileSnapshot before;
                const TiledImage tiled = renderTiled(p.settings, seamStroke(), background, &before);
                QVERIFY2(!tiled.isNull(), qPrintable(p.id));
                const QImage img = tiled.toImage();
                const QString name = QStringLiteral("m10-seam-%1%2").arg(p.id.mid(8), onTransparency ? "-transparent" : "");
                const QString path = goldenPath(QStringLiteral("stroke-%1.png").arg(name));
                if (writeMissingGolden(path))
                    QVERIFY(img.save(path));
                const QImage ref = QImage(path).convertToFormat(QImage::Format_ARGB32_Premultiplied);
                QVERIFY2(!ref.isNull(), qPrintable("missing " + path + " (run with DRAWAPP_WRITE_GOLDEN=1)"));
                QString why;
                QVERIFY2(closeImages(img, ref, 3, 0.01, &why), qPrintable(name + ": " + why));
            }
        }
    }

    void milestone10EngineReference()
    {
        // Milestone 11 rebuilds the engine around deposit strategies (research/m11-brush-architecture.md,
        // A1, A9). Every existing brush must keep its exact pixels, not merely stay within the golden
        // images' tolerance. This corpus was rendered by the milestone 10 engine (commit dcde383) and
        // stored as SHA-256 hashes; any change of any pixel fails. The corpus is fixed: the eight
        // milestone 10 built-in presets by id, both values of every milestone 9 property scenario
        // (ids recorded in the file), selections, airbrush timing, transparency and tile seams.
        // DRAWAPP_WRITE_ENGINE_REFERENCE=1 rewrote it once, from the unmodified engine.
        const QString path = goldenPath(QStringLiteral("m10-engine-reference.json"));
        const bool write = qEnvironmentVariableIntValue("DRAWAPP_WRITE_ENGINE_REFERENCE") == 1;
        QJsonObject stored;
        if (!write) {
            QFile in(path);
            QVERIFY2(in.open(QIODevice::ReadOnly), "m10-engine-reference.json is missing");
            stored = QJsonDocument::fromJson(in.readAll()).object();
            QVERIFY(!stored.isEmpty());
        }
        auto hash = [](const QImage &img) {
            QCryptographicHash h(QCryptographicHash::Sha256);
            for (int y = 0; y < img.height(); ++y)
                h.addData(QByteArrayView(reinterpret_cast<const char *>(img.constScanLine(y)), img.width() * 4));
            return QString::fromLatin1(h.result().toHex());
        };
        QImage clip(400, 200, QImage::Format_Grayscale8);
        clip.fill(0);
        {
            QPainter p(&clip);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255));
            p.drawEllipse(QRectF(60, 30, 230, 140));
        }
        const QImage transparent = transparentCanvas(QSize(400, 200));
        QJsonObject computed;
        auto add = [&](const QString &name, const BrushSettings &settings, const std::vector<StrokeSample> &stroke,
                       const QImage &background, const QImage &mask = QImage()) {
            computed.insert(name, hash(render(settings, stroke, background, 7, Paint, mask)));
        };
        const QStringList presetIds{"builtin.sketch-pencil", "builtin.clean-ink", "builtin.solid-round",
                                    "builtin.soft-airbrush", "builtin.textured-paint", "builtin.blend-smudge",
                                    "builtin.hard-eraser", "builtin.soft-eraser"};
        for (const QString &id : presetIds) {
            const BrushPreset *p = BrushPresets::builtIn(id);
            QVERIFY2(p, qPrintable(id));
            add(id + "/standard/stripes", p->settings, standardStroke(), stripes());
            add(id + "/standard/transparent", p->settings, standardStroke(), transparent);
            add(id + "/seam/stripes", p->settings, seamStroke(), stripes());
            add(id + "/seam/transparent/selection", p->settings, seamStroke(), transparent, clip);
            add(id + "/standard/stripes/selection", p->settings, standardStroke(), stripes(), clip);
        }
        add("golden-wet-ink/standard", goldenBrush(), standardStroke(), stripes());
        add("golden-wet-ink/seam/selection", goldenBrush(), seamStroke(), stripes(), clip);
        // Every property scenario, both values, on the engine it is tested on (as
        // everyImplementedPropertyChangesRendering), plus each on transparency.
        const auto table = scenarios();
        const QJsonArray recordedIds = stored.value("scenarioIds").toArray();
        QStringList ids;
        if (write) {
            for (const BrushProperty &p : BrushProperties::all())
                if (p.implemented && table.count(p.id))
                    ids.append(p.id);
            ids.append({"blend", "stamp"});
        } else {
            for (const QJsonValue &v : recordedIds)
                ids.append(v.toString());
        }
        for (const QString &id : ids) {
            const BrushProperty *p = BrushProperties::find(id);
            QVERIFY2(p, qPrintable(id));
            const auto it = table.find(id);
            QVERIFY2(it != table.end(), qPrintable(id));
            BrushSettings base = baseSettings();
            applyEngine(base, testEngine(p->engines));
            it->second.setup(base);
            base = base.sanitized();
            BrushSettings changed = base;
            p->set(changed, it->second.changed);
            changed = changed.sanitized();
            add("property/" + id + "/default", base, standardStroke(), stripes());
            add("property/" + id + "/changed", changed, standardStroke(), stripes());
            add("property/" + id + "/changed/transparent", changed, seamStroke(), transparent);
        }
        if (write) {
            QJsonObject out = computed;
            out.insert("scenarioIds", QJsonArray::fromStringList(ids));
            out.insert("engine", QStringLiteral("milestone 10, commit dcde383"));
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QJsonDocument(out).toJson(QJsonDocument::Indented));
            qInfo("Wrote %d reference hashes", int(computed.size()));
            return;
        }
        int compared = 0;
        QStringList differ;
        for (auto it = stored.begin(); it != stored.end(); ++it) {
            if (it.key() == "scenarioIds" || it.key() == "engine")
                continue;
            QVERIFY2(computed.contains(it.key()), qPrintable("not rendered: " + it.key()));
            if (computed.value(it.key()) != it.value())
                differ.append(it.key());
            ++compared;
        }
        QVERIFY2(differ.isEmpty(), qPrintable(QString::number(differ.size()) + " differ: " + differ.mid(0, 12).join(", ")));
        qInfo("Milestone 10 engine reference: %d renders identical", compared);
    }

    void builtInPresetsStillRender()
    {
        // The milestone 6 presets still render through the extended engine (their behaviour is
        // checked in tst_brush; here: valid and visible on every background).
        for (const BrushPreset &p : BrushPresets::builtIns()) {
            const QImage img = render(p.settings, standardStroke(), stripes());
            QVERIFY2(validPremultiplied(img), qPrintable(p.id));
            QVERIFY2(differingPixels(img, stripes()) > 100, qPrintable(p.id));
        }
    }
};

QTEST_GUILESS_MAIN(TestBrushEngine)
#include "tst_brushengine.moc"

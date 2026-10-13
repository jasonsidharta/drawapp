// Milestone 11 brush engines (research/m11-brush-architecture.md): dry media, ink, mixing, bristle,
// watercolour and procedural strokes. Each test measures the behaviour the engine claims, on
// rendered pixels; golden images (new files only) guard against unintended change.
//
// Pressure and tilt here are simulated (StrokeSample values). They show the engine's response,
// not that any physical pen delivers them.

#include "BrushPresets.h"
#include "BrushProperties.h"
#include "BrushTips.h"
#include "MemoryBudget.h"
#include "PigmentMix.h"
#include "StrokeEngine.h"
#include "TiledImage.h"
#include "TileSnapshot.h"
#include "WetLayer.h"

#include <QFile>
#include <QPainter>
#include <QSet>
#include <QtTest>
#include <array>
#include <cmath>
#include <functional>

namespace {

constexpr double Pi = 3.14159265358979323846;
const QColor Ink(30, 30, 40);

QImage canvas(QSize size = {400, 200}, QColor fill = Qt::transparent)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(fill);
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

// A straight stroke from `from` to `to` in `steps` events, 8 ms apart, at a fixed pressure.
std::vector<StrokeSample> line(QPointF from, QPointF to, int steps, double pressure = 1.0)
{
    std::vector<StrokeSample> v;
    for (int i = 0; i <= steps; ++i) {
        const QPointF p = from + (to - from) * (double(i) / steps);
        v.push_back(sample(p.x(), p.y(), pressure, i * 8.0));
    }
    return v;
}

void paint(QImage &image, const BrushSettings &settings, const std::vector<StrokeSample> &samples, quint64 seed = 7,
           const QColor &color = Ink)
{
    TileSnapshot before;
    StrokeEngine engine(settings, color, seed, StrokeEngine::smoothingRadius(settings));
    engine.begin(image, samples.front(), before);
    for (size_t i = 1; i < samples.size(); ++i)
        engine.moveTo(image, samples[i], before);
    engine.finish(image, before);
}

QImage render(const BrushSettings &settings, const std::vector<StrokeSample> &samples, const QImage &background,
              quint64 seed = 7, const QColor &color = Ink)
{
    QImage image = background.copy();
    paint(image, settings, samples, seed, color);
    return image;
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
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            n += a.pixel(x, y) != b.pixel(x, y);
    return n;
}

double meanAlpha(const QImage &image, const QRect &area)
{
    double sum = 0;
    int n = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            sum += qAlpha(image.pixel(x, y));
            ++n;
        }
    return n ? sum / n : 0.0;
}

// The paper's height at a document pixel, as dry media see it at texture scale 1 without rotation
// or offset (nearest texel): 0 = valley, 1 = peak.
double paperHeight(const QString &id, int x, int y)
{
    const QImage &t = BrushTips::texture(id)->image;
    return t.pixelColor(x % t.width(), y % t.height()).redF();
}

// Mean alpha of the pixels of `area` whose paper height is below `low` (valleys) and above `high`
// (peaks).
std::pair<double, double> valleyAndPeak(const QImage &image, const QRect &area, const QString &paper = QStringLiteral("paper"),
                                        double low = 0.3, double high = 0.7)
{
    double v = 0, p = 0;
    int nv = 0, np = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            const double h = paperHeight(paper, x, y);
            if (h < low) {
                v += qAlpha(image.pixel(x, y));
                ++nv;
            } else if (h > high) {
                p += qAlpha(image.pixel(x, y));
                ++np;
            }
        }
    return {nv ? v / nv : 0.0, np ? p / np : 0.0};
}

// A dry brush for measurements: a hard round tip large enough that its core is a clean area.
BrushSettings dryBrush()
{
    BrushSettings s;
    s.name = QStringLiteral("dry test");
    s.stamp = BrushStamp::Dry;
    s.size = 40;
    s.hardness = 1.0;
    s.spacing = 0.05;
    s.opacity = 1.0;
    s.flow = 1.0;
    s.texture = QStringLiteral("paper");
    s.textureStrength = 1.0;
    s.dryDepth = 1.0; // full pressure reaches the bottom of the tooth
    s.drySoftness = 0.08;
    s.pressureSize = {false, 0.0, 1.0}; // size fixed, so only penetration follows pressure
    s.pressureDepth = {true, 0.0, 1.0};
    return s.sanitized();
}

// The core of a horizontal stroke along y = 100 from x = 60 to 340 (40 px wide brush).
const QRect Core(80, 90, 240, 21);

QString goldenPath(const QString &name)
{
    return QStringLiteral(DRAWAPP_SOURCE_DIR "/tests/golden/") + name;
}

// Golden images of milestone 11 engines. A missing file is written when DRAWAPP_WRITE_NEW_GOLDEN=1;
// an existing file is never rewritten here (legacy goldens stay as committed).
void checkGolden(const QString &name, const QImage &image, int maxDelta = 3, double maxShare = 0.01)
{
    const QString path = goldenPath(QStringLiteral("stroke-m11-%1.png").arg(name));
    if (!QFile::exists(path) && qEnvironmentVariableIntValue("DRAWAPP_WRITE_NEW_GOLDEN") == 1)
        QVERIFY(image.save(path));
    const QImage ref = QImage(path).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QVERIFY2(!ref.isNull(), qPrintable("missing " + path + " (run with DRAWAPP_WRITE_NEW_GOLDEN=1 once)"));
    QCOMPARE(ref.size(), image.size());
    int differ = 0, worst = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = image.pixel(x, y), q = ref.pixel(x, y);
            const int d = std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                                    std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
            differ += d > 0;
            worst = std::max(worst, d);
        }
    const double share = double(differ) / (image.width() * image.height());
    QVERIFY2(share <= maxShare && worst <= maxDelta,
             qPrintable(QStringLiteral("%1: %2 % differ, by up to %3").arg(name).arg(share * 100, 0, 'f', 2).arg(worst)));
}

// A wobbly stroke with a pressure ramp, as the golden images use.
std::vector<StrokeSample> wobble()
{
    std::vector<StrokeSample> v;
    for (int i = 0; i <= 90; ++i) {
        StrokeSample s = sample(40 + i * 3.5, 100 + 30 * std::sin(i * 0.09), std::clamp(0.15 + 0.85 * std::sin(Pi * i / 90), 0.05, 1.0),
                                i * 8.0);
        s.hasTilt = true;
        s.tiltX = 6;
        s.tiltY = 4;
        v.push_back(s);
    }
    return v;
}

BrushSettings bristleBrush()
{
    BrushSettings s;
    s.stamp = BrushStamp::Bristle;
    s.size = 40;
    s.spacing = 0.05;
    s.pressureSize = {false, 0, 1};
    s.bristleCount = 60;
    s.bristleThickness = 0.04;
    s.bristleSpread = 0.5;
    s.bristleLengthJitter = 0.5;
    s.bristleDepletion = 0.0;
    return s.sanitized();
}

int painted(const QImage &img, const QRect &r, int threshold = 128)
{
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x)
            n += qAlpha(img.pixel(x, y)) > threshold;
    return n;
}

// Watercolour for measurements: smooth paper (no tooth) unless a test sets it, nothing lifted.
BrushSettings watercolor(double spread = 0.5, double edge = 0.5, double granulation = 0.0)
{
    BrushSettings s;
    s.blend = BrushBlend::Watercolor;
    s.size = 30;
    s.hardness = 0.5;
    s.spacing = 0.06;
    s.flow = 0.8;
    s.pressureSize = {false, 0, 1};
    s.texture = QStringLiteral("rough");
    s.textureStrength = 0.0;
    s.wcWater = 0.6;
    s.wcPigment = 0.5;
    s.wcSpread = spread;
    s.wcEdgeDarkening = edge;
    s.wcGranulation = granulation;
    s.wcDryingTime = 3;
    s.wcLifting = 0.0;
    return s.sanitized();
}

// One watercolour stroke into a wet layer that outlives it (as the canvas keeps one).
void wetStroke(QImage &image, WetLayer &wet, const BrushSettings &settings, const std::vector<StrokeSample> &samples,
               const QColor &color = QColor(40, 70, 160), quint64 seed = 7)
{
    TileSnapshot before;
    StrokeEngine engine(settings, color, seed, 0);
    engine.setWetLayer(&wet);
    engine.begin(image, samples.front(), before);
    for (size_t i = 1; i < samples.size(); ++i)
        engine.moveTo(image, samples[i], before);
    engine.finish(image, before);
}

// Lets the paint dry completely; returns the simulation steps it took.
int dry(QImage &image, WetLayer &wet, int maxSteps = 20000)
{
    DenseSurface surface(image);
    TileSnapshot before;
    int steps = 0;
    for (; wet.isWet() && steps < maxSteps; steps += 10)
        wet.step(surface, before, 10);
    return steps;
}

// 0 (white paper) .. 255 (black).
int darkness(const QImage &image, int x, int y)
{
    return 255 - qGray(image.pixel(x, y));
}

// Mean darkness over columns x0..x1 at distances d0..d1 from the row yc (both sides).
double darknessAt(const QImage &image, int x0, int x1, int yc, int d0, int d1)
{
    double sum = 0;
    int n = 0;
    for (int x = x0; x <= x1; ++x)
        for (int d = d0; d <= d1; ++d)
            for (const int y : {yc - d, yc + d}) {
                sum += darkness(image, x, y);
                ++n;
                if (d == 0)
                    break;
            }
    return n ? sum / n : 0.0;
}

// Pixels painted (alpha above `threshold`) in one image but not the other.
int differingPixelsAbove(const QImage &a, const QImage &b, int threshold)
{
    int n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            n += (qAlpha(a.pixel(x, y)) > threshold) != (qAlpha(b.pixel(x, y)) > threshold);
    return n;
}

} // namespace

class TestBrushMedia : public QObject
{
    Q_OBJECT

private slots:
    // --- dry media (A3) ---

    void dryPenetrationFollowsPressure()
    {
        // Light pressure catches the peaks of the tooth and leaves the valleys bare; heavy pressure
        // fills the valleys. Measured on the paper's own heights.
        const BrushSettings s = dryBrush();
        double previousValley = -1;
        double lightRatio = 0, heavyRatio = 0;
        for (const double pressure : {0.2, 0.45, 0.7, 1.0}) {
            const QImage img = render(s, line({60, 100}, {340, 100}, 40, pressure), canvas());
            QVERIFY(validPremultiplied(img));
            const auto [valley, peak] = valleyAndPeak(img, Core);
            QVERIFY2(peak > 0, qPrintable(QString::number(pressure)));
            QVERIFY2(valley >= previousValley, qPrintable(QStringLiteral("valleys fill monotonically: %1 at %2").arg(valley).arg(pressure)));
            previousValley = valley;
            if (pressure == 0.2)
                lightRatio = valley / peak;
            if (pressure == 1.0)
                heavyRatio = valley / peak;
            qInfo("pressure %.2f: valleys %.1f, peaks %.1f", pressure, valley, peak);
        }
        QVERIFY2(lightRatio < 0.2, qPrintable(QString::number(lightRatio)));  // light: valleys stay nearly bare
        QVERIFY2(heavyRatio > 0.75, qPrintable(QString::number(heavyRatio))); // heavy: valleys nearly as full as peaks
    }

    void drySoftnessAndDepthChangeTheGrain()
    {
        // More depth fills more of the tooth at the same pressure; less softness makes the grain
        // crisper (fewer pixels in between bare and full).
        BrushSettings shallow = dryBrush(), deep = dryBrush();
        shallow.dryDepth = 0.45;
        deep.dryDepth = 0.9;
        const auto stroke = line({60, 100}, {340, 100}, 40, 0.8);
        QVERIFY(meanAlpha(render(deep, stroke, canvas()), Core) > meanAlpha(render(shallow, stroke, canvas()), Core) + 20);
        BrushSettings crisp = dryBrush(), soft = dryBrush();
        crisp.drySoftness = 0.02;
        soft.drySoftness = 0.45;
        auto midtones = [&](const QImage &img) {
            int n = 0;
            for (int y = Core.top(); y <= Core.bottom(); ++y)
                for (int x = Core.left(); x <= Core.right(); ++x)
                    n += qAlpha(img.pixel(x, y)) > 40 && qAlpha(img.pixel(x, y)) < 215;
            return n;
        };
        const int crispMid = midtones(render(crisp, stroke, canvas())), softMid = midtones(render(soft, stroke, canvas()));
        QVERIFY2(softMid > 2 * crispMid, qPrintable(QStringLiteral("%1 vs %2").arg(softMid).arg(crispMid)));
    }

    void dryBuildsUpAcrossStrokesInTheSameTooth()
    {
        // Layered strokes darken, and the paper keeps showing: after three light strokes the peaks
        // are darker than the valleys (the tooth is in document space, not per stroke).
        BrushSettings s = dryBrush();
        s.opacity = 0.5;
        QImage img = canvas();
        double previous = 0;
        for (int i = 0; i < 3; ++i) {
            paint(img, s, line({60, 100}, {340, 100}, 40, 0.45), quint64(100 + i));
            const double now = meanAlpha(img, Core);
            QVERIFY2(now > previous + 3, qPrintable(QStringLiteral("stroke %1: %2 → %3").arg(i).arg(previous).arg(now)));
            previous = now;
            const auto [valley, peak] = valleyAndPeak(img, Core);
            QVERIFY2(peak > valley * 2, qPrintable(QStringLiteral("stroke %1: peak %2 valley %3").arg(i).arg(peak).arg(valley)));
        }
    }

    void dryTiltShadesOnlyThePeaks()
    {
        // The side of a tilted pencil touches only the peaks (penetration × tilt setting).
        BrushSettings s = dryBrush();
        s.dryTiltDepth = 0.4;
        auto upright = line({60, 100}, {340, 100}, 40, 0.9);
        auto tilted = upright;
        for (StrokeSample &t : tilted) {
            t.hasTilt = true;
            t.tiltX = 60; // the full tilt angle (default 60°)
        }
        const auto [vu, pu] = valleyAndPeak(render(s, upright, canvas()), Core);
        const auto [vt, pt] = valleyAndPeak(render(s, tilted, canvas()), Core);
        QVERIFY2(vt < vu * 0.5, qPrintable(QStringLiteral("valleys %1 → %2").arg(vu).arg(vt)));
        QVERIFY2(pt > 0.3 * pu, qPrintable(QStringLiteral("peaks %1 → %2").arg(pu).arg(pt)));
        // Without tilt data the setting has no effect (no invented data).
        BrushSettings none = s;
        none.dryTiltDepth = 1.0;
        QCOMPARE(render(s, upright, canvas()), render(none, upright, canvas()));
    }

    void dryStreaksFollowTheStrokeDirection()
    {
        // Charcoal streaks run with the stroke: along the stroke neighbouring pixels differ less
        // than across it, for a horizontal and for a vertical stroke.
        BrushSettings s = dryBrush();
        s.textureStrength = 0.0; // smooth paper: only the streaks structure the mark
        s.dryStreaks = 1.0;
        s.dryStreakLength = 10;
        s.dryDepth = 0.6;
        s.drySoftness = 0.15;
        auto roughness = [](const QImage &img, const QRect &r, int dx, int dy) {
            double sum = 0;
            int n = 0;
            for (int y = r.top(); y <= r.bottom() - dy; ++y)
                for (int x = r.left(); x <= r.right() - dx; ++x) {
                    sum += std::abs(qAlpha(img.pixel(x, y)) - qAlpha(img.pixel(x + dx, y + dy)));
                    ++n;
                }
            return sum / n;
        };
        const QImage h = render(s, line({60, 100}, {340, 100}, 40), canvas());
        const double hAlong = roughness(h, Core, 1, 0), hAcross = roughness(h, Core, 0, 1);
        QVERIFY2(hAlong < 0.6 * hAcross, qPrintable(QStringLiteral("horizontal: along %1, across %2").arg(hAlong).arg(hAcross)));
        const QImage v = render(s, line({200, 20}, {200, 380}, 50), canvas(QSize(400, 400)));
        const QRect vCore(190, 40, 21, 300);
        const double vAlong = roughness(v, vCore, 0, 1), vAcross = roughness(v, vCore, 1, 0);
        QVERIFY2(vAlong < 0.6 * vAcross, qPrintable(QStringLiteral("vertical: along %1, across %2").arg(vAlong).arg(vAcross)));
    }

    void dryEdgeBreakupCrumblesTheRim()
    {
        // With edge breakup the rim (outer quarter of the stroke) loses coverage and becomes
        // irregular, while the core keeps most of its paint.
        BrushSettings even = dryBrush();
        even.hardness = 0.3;
        BrushSettings crumbly = even;
        crumbly.dryEdgeBreakup = 1.0;
        const auto stroke = line({60, 100}, {340, 100}, 40);
        const QImage a = render(even, stroke, canvas()), b = render(crumbly, stroke, canvas());
        const QRect rim(80, 82, 240, 6), core(80, 96, 240, 9);
        QVERIFY2(meanAlpha(b, rim) < 0.7 * meanAlpha(a, rim), qPrintable(QStringLiteral("%1 vs %2").arg(meanAlpha(b, rim)).arg(meanAlpha(a, rim))));
        QVERIFY2(meanAlpha(b, core) > 0.8 * meanAlpha(a, core), qPrintable(QStringLiteral("%1 vs %2").arg(meanAlpha(b, core)).arg(meanAlpha(a, core))));
    }

    void dryGritIsSeededPerStrokeAndPaperIsFixed()
    {
        // Same seed: identical. Without grit, different seeds give identical marks (the paper is
        // fixed in document space); with grit, different seeds skip different pixels.
        BrushSettings s = dryBrush();
        const auto stroke = line({60, 100}, {340, 100}, 40, 0.7);
        QCOMPARE(render(s, stroke, canvas(), 1), render(s, stroke, canvas(), 1));
        QCOMPARE(render(s, stroke, canvas(), 1), render(s, stroke, canvas(), 2));
        s.dryGrit = 0.3;
        QCOMPARE(render(s, stroke, canvas(), 1), render(s, stroke, canvas(), 1));
        QVERIFY(differingPixels(render(s, stroke, canvas(), 1), render(s, stroke, canvas(), 2)) > 500);
    }

    void dryEraserLiftsThePeaksFirst()
    {
        // An eraser with the dry stamp at light pressure lifts paint from the peaks of the tooth and
        // leaves the valleys, as a hard eraser on grainy paper does.
        BrushSettings s = dryBrush();
        s.blend = BrushBlend::Erase;
        const QImage solid = canvas(QSize(400, 200), QColor(40, 40, 50));
        const QImage img = render(s, line({60, 100}, {340, 100}, 40, 0.3), solid);
        QVERIFY(validPremultiplied(img));
        const auto [valley, peak] = valleyAndPeak(img, Core);
        QVERIFY2(valley > peak + 60, qPrintable(QStringLiteral("remaining: valleys %1, peaks %2").arg(valley).arg(peak)));
    }

    void dryOnTransparencyAndTiles()
    {
        // Valid premultiplied pixels; only touched pixels change; identical on a tiled layer across
        // tile seams; undo restores the layer; per-dab colour works through the dry stamp.
        for (const QString &id : {QStringLiteral("builtin.graphite-hb"), QStringLiteral("builtin.vine-charcoal"),
                                  QStringLiteral("builtin.soft-pastel")}) {
            const BrushPreset *p = BrushPresets::builtIn(id);
            QVERIFY2(p, qPrintable(id));
            BrushSettings s = p->settings;
            s.hueJitter = 40; // per-dab colour through the dry deposit
            std::vector<StrokeSample> seam;
            for (int i = 0; i <= 60; ++i)
                seam.push_back(sample(20 + i * 6.0, i < 30 ? 64.0 : 128.0 - (i - 30) * 2.1, 0.3 + 0.7 * (i % 10) / 10.0, i * 4.0));
            const QImage dense = render(s, seam, canvas());
            QVERIFY(validPremultiplied(dense));
            QCOMPARE(qAlpha(dense.pixel(399, 199)), 0);
            TiledImage tiled = TiledImage::fromImage(canvas());
            TileSnapshot before;
            StrokeEngine engine(s, Ink, 7, StrokeEngine::smoothingRadius(s));
            engine.begin(tiled, seam.front(), before);
            for (size_t i = 1; i < seam.size(); ++i)
                engine.moveTo(tiled, seam[i], before);
            engine.finish(tiled, before);
            QVERIFY(tiled == dense);
            before.swapWith(tiled);
            QVERIFY(tiled == canvas());
        }
    }

    void dryStrokeStopsCleanlyWhenMemoryRunsOut()
    {
        // A tile that cannot be had stops the stroke; the snapshot restores the layer exactly. (The
        // comparison is against an image made before the limit is lowered: TiledImage::toImage()
        // itself reserves memory and would be refused under the test's limit.)
        const qint64 limit = MemoryBudget::limit();
        const auto restore = qScopeGuard([&] { MemoryBudget::setLimit(limit == MemoryBudget::defaultLimit() ? 0 : limit); });
        const QImage start = canvas(QSize(2048, 512));
        TiledImage layer = TiledImage::fromImage(start);
        MemoryBudget::setLimit(MemoryBudget::usedTotal() + 3 * 64 * 64 * 4); // three tiles
        const BrushPreset *p = BrushPresets::builtIn(QStringLiteral("builtin.compressed-charcoal"));
        TileSnapshot before;
        StrokeEngine engine(p->settings, Ink, 7, 0);
        const auto stroke = line({20, 250}, {2000, 250}, 200);
        engine.begin(layer, stroke.front(), before);
        for (size_t i = 1; i < stroke.size() && !engine.allocationFailed(); ++i)
            engine.moveTo(layer, stroke[i], before);
        QVERIFY(engine.allocationFailed());
        QVERIFY(layer.allocatedTileCount() > 0); // it did paint before running out
        before.swapWith(layer);
        QVERIFY(layer == start);
    }

    void strokesStopCleanlyAtEveryMemoryLimit()
    {
        // Swept: the stroke runs out of tiles at every point from the first tile on, for a
        // milestone 10 brush, a dry-media brush and watercolour (whose wet tiles are charged to the
        // budget too), on transparency and on paint; the snapshot
        // always restores the layer exactly.
        const qint64 limit = MemoryBudget::limit();
        const auto restore = qScopeGuard([&] { MemoryBudget::setLimit(limit == MemoryBudget::defaultLimit() ? 0 : limit); });
        const qint64 tile = 64 * 64 * 4;
        for (const QString &id : {QStringLiteral("builtin.solid-round"), QStringLiteral("builtin.compressed-charcoal"),
                                  QStringLiteral("builtin.textured-paint"), QStringLiteral("builtin.watercolor-wash")}) {
            for (const bool painted : {false, true}) {
                QImage start = canvas(QSize(1024, 256));
                if (painted) {
                    QPainter p(&start);
                    p.fillRect(QRect(0, 0, 1024, 256), QColor(200, 60, 40, 180));
                }
                for (int tiles = 0; tiles <= 24; tiles += 2) {
                    MemoryBudget::setLimit(0);
                    TiledImage layer = TiledImage::fromImage(start);
                    MemoryBudget::setLimit(MemoryBudget::usedTotal() + tiles * tile);
                    TileSnapshot before;
                    StrokeEngine engine(BrushPresets::builtIn(id)->settings, Ink, 7, 0);
                    const auto stroke = line({10, 128}, {1010, 140}, 120);
                    engine.begin(layer, stroke.front(), before);
                    for (size_t i = 1; i < stroke.size() && !engine.allocationFailed(); ++i)
                        engine.moveTo(layer, stroke[i], before);
                    if (!engine.allocationFailed())
                        engine.finish(layer, before);
                    before.swapWith(layer);
                    QVERIFY2(layer == start, qPrintable(QStringLiteral("%1, %2, %3 tiles").arg(id).arg(painted).arg(tiles)));
                }
            }
        }
    }

    // --- ink and stroke geometry (A4) ---

    void inkCurvesStayRoundWithSparseInput()
    {
        // A fast pen reports a circle of radius 150 with 16 samples. Straight segments sag inwards
        // by R(1 − cos(π/16)) ≈ 2.9 px between samples; the curve stays on the circle.
        BrushSettings s;
        s.size = 3;
        s.pressureSize = {false, 0, 1};
        s.spacing = 0.05;
        std::vector<StrokeSample> circle;
        for (int i = 0; i <= 16; ++i)
            circle.push_back(sample(200 + 150 * std::cos(2 * Pi * i / 16), 200 + 150 * std::sin(2 * Pi * i / 16), 1.0, i * 8.0));
        // Measured on the segments with neighbours on both sides: the first and last segments have
        // none on one side and are drawn as straight chords there (StrokeCurve, by design).
        auto innerRadius = [](const QImage &img) {
            double inner = 1e9;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) > 128) {
                        double a = std::atan2(y + 0.5 - 200, x + 0.5 - 200);
                        if (a < 0)
                            a += 2 * Pi;
                        if (a < 2 * Pi / 16 || a > 15 * 2 * Pi / 16)
                            continue;
                        inner = std::min(inner, std::hypot(x + 0.5 - 200, y + 0.5 - 200));
                    }
            return inner;
        };
        const double polygon = innerRadius(render(s, circle, canvas(QSize(400, 400))));
        s.curveInterpolation = true;
        const double curve = innerRadius(render(s, circle, canvas(QSize(400, 400))));
        qInfo("inner radius: straight segments %.2f, curve %.2f (circle 150, line width 3)", polygon, curve);
        QVERIFY2(curve > polygon + 2.0, qPrintable(QStringLiteral("%1 vs %2").arg(curve).arg(polygon)));
        QVERIFY2(curve > 150 - 1.5 - 0.6, qPrintable(QString::number(curve)));
    }

    void inkCornersStaySharp()
    {
        // A V drawn with a few samples per leg: with corner preservation the curve and the smoothing
        // keep the vertex (no overshoot beyond the legs and the tip reaches the corner); without it
        // the curve swings past the vertex.
        BrushSettings s;
        s.size = 4;
        s.pressureSize = {false, 0, 1};
        s.spacing = 0.05;
        s.curveInterpolation = true;
        s.smoothing = 40; // a pull string that would otherwise cut the corner
        std::vector<StrokeSample> v;
        for (int i = 0; i <= 4; ++i)
            v.push_back(sample(60 + i * 35.0, 60 + i * 60.0, 1.0, i * 8.0));
        for (int i = 1; i <= 4; ++i)
            v.push_back(sample(200 + i * 35.0, 300 - i * 60.0, 1.0, 32 + i * 8.0));
        const QPointF vertex(200, 300);
        auto reachAndOvershoot = [&](const QImage &img) {
            double nearest = 1e9;
            int below = 0; // painted pixels lower than the vertex (beyond the legs)
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) > 128) {
                        nearest = std::min(nearest, std::hypot(x + 0.5 - vertex.x(), y + 0.5 - vertex.y()));
                        below += y + 0.5 > vertex.y() + 3;
                    }
            return std::make_pair(nearest, below);
        };
        const double z = StrokeEngine::smoothingRadius(s); // document px at zoom 1
        QImage sharpImg = canvas(QSize(400, 400));
        {
            TileSnapshot before;
            StrokeEngine engine(s, Ink, 7, z);
            engine.begin(sharpImg, v.front(), before);
            for (size_t i = 1; i < v.size(); ++i)
                engine.moveTo(sharpImg, v[i], before);
            engine.finish(sharpImg, before);
        }
        const auto [reach, overshoot] = reachAndOvershoot(sharpImg);
        QVERIFY2(reach < 3.0, qPrintable(QStringLiteral("tip %1 px from the corner").arg(reach)));
        QVERIFY2(overshoot == 0, qPrintable(QString::number(overshoot)));
        // Never a corner: the smoothing cuts it and the curve rounds it.
        s.cornerAngle = 180;
        QImage roundImg = canvas(QSize(400, 400));
        {
            TileSnapshot before;
            StrokeEngine engine(s, Ink, 7, z);
            engine.begin(roundImg, v.front(), before);
            for (size_t i = 1; i < v.size(); ++i)
                engine.moveTo(roundImg, v[i], before);
            engine.finish(roundImg, before);
        }
        QVERIFY2(reachAndOvershoot(roundImg).first > reach + 5,
                 qPrintable(QStringLiteral("rounded corner reaches %1").arg(reachAndOvershoot(roundImg).first)));
    }

    void inkSlowAndFastStrokesAreContinuous()
    {
        // 1000 events (slow, 1 ms apart) and 6 events (fast) along the same line: no gaps either way.
        for (const int events : {1000, 6}) {
            BrushSettings s = BrushPresets::builtIn(QStringLiteral("builtin.fine-liner"))->settings;
            const QImage img = render(s, line({30, 100}, {370, 100}, events), canvas());
            for (int x = 50; x <= 350; ++x)
                QVERIFY2(qAlpha(img.pixel(x, 100)) > 200, qPrintable(QStringLiteral("%1 events: gap at x=%2").arg(events).arg(x)));
        }
    }

    void inkLongStrokeEndsAtThePen()
    {
        // 20 000 events in a spiral: the line ends where the pen lifted, every pixel valid.
        BrushSettings s = BrushPresets::builtIn(QStringLiteral("builtin.g-pen"))->settings;
        std::vector<StrokeSample> spiral;
        for (int i = 0; i <= 20000; ++i) {
            const double t = i / 20000.0, r = 20 + 170 * t, a = t * 40 * Pi;
            spiral.push_back(sample(200 + r * std::cos(a), 200 + r * std::sin(a), 0.6 + 0.4 * std::sin(t * 50), i * 1.0));
        }
        QElapsedTimer timer;
        timer.start();
        const QImage img = render(s, spiral, canvas(QSize(400, 400)));
        qInfo("20 000-event G-Pen spiral: %lld ms", timer.elapsed());
        QVERIFY(validPremultiplied(img));
        // The last sample is at radius 190: the tapered end still reaches within a few pixels of it.
        const QPointF end = spiral.back().pos;
        double nearest = 1e9;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 20)
                    nearest = std::min(nearest, std::hypot(x + 0.5 - end.x(), y + 0.5 - end.y()));
        QVERIFY2(nearest < 4.0, qPrintable(QString::number(nearest)));
    }

    void liveTaperHasNoLagAndMatchesTheHeldBackTaper()
    {
        // While drawing, the live taper's line reaches the pen; the milestone 9 hold-back keeps the
        // last taper-length back. After pen-up both look the same (dab positions in the tail differ
        // slightly because the tail is spaced again).
        BrushSettings held;
        held.size = 12;
        held.hardness = 1.0;
        held.spacing = 0.05;
        held.pressureSize = {false, 0, 1};
        held.taperEnd = 80;
        held.taperEndSize = 0.05;
        BrushSettings live = held;
        live.taperLive = true;
        const auto stroke = line({20, 100}, {380, 100}, 60);
        auto rightmostWhileDrawing = [&](const BrushSettings &s) {
            QImage img = canvas();
            TileSnapshot before;
            StrokeEngine engine(s, Ink, 7, 0);
            engine.begin(img, stroke.front(), before);
            for (int i = 1; i <= 40; ++i) // the pen is at x = 260
                engine.moveTo(img, stroke[size_t(i)], before);
            int right = 0;
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, 100)) > 128)
                    right = x;
            return right;
        };
        const int heldRight = rightmostWhileDrawing(held), liveRight = rightmostWhileDrawing(live);
        qInfo("while drawing (pen at x = 260): hold-back reaches %d, live %d", heldRight, liveRight);
        QVERIFY2(liveRight >= 260, qPrintable(QString::number(liveRight)));
        QVERIFY2(heldRight <= 260 - 70, qPrintable(QString::number(heldRight)));
        const QImage a = render(held, stroke, canvas()), b = render(live, stroke, canvas());
        int bigDiffs = 0;
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x)
                bigDiffs += std::abs(qAlpha(a.pixel(x, y)) - qAlpha(b.pixel(x, y))) > 60;
        QVERIFY2(bigDiffs < 40, qPrintable(QString::number(bigDiffs)));
        // The tail really is tapered: thin near the end, full width before the tail.
        auto width = [&](const QImage &img, int x) {
            int n = 0;
            for (int y = 0; y < img.height(); ++y)
                n += qAlpha(img.pixel(x, y)) > 128;
            return n;
        };
        QVERIFY2(width(b, 200) >= 11 && width(b, 372) <= 4, qPrintable(QStringLiteral("%1, %2").arg(width(b, 200)).arg(width(b, 372))));
    }

    void liveTaperIsExactOnTilesAndUndoes()
    {
        // The rebuilt tail on a tiled layer equals the dense result; undo restores the layer, on
        // paint and on transparency, also through a selection and with per-dab colour.
        BrushSettings s = BrushPresets::builtIn(QStringLiteral("builtin.brush-pen"))->settings;
        s.hueJitter = 30;
        QImage clip(400, 200, QImage::Format_Grayscale8);
        clip.fill(0);
        {
            QPainter p(&clip);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(QRectF(30, 20, 320, 160));
        }
        QImage stripes = canvas();
        {
            QPainter p(&stripes);
            for (int i = 0; i < 10; ++i)
                p.fillRect(QRect(i * 40, 0, 20, 200), QColor::fromHsv(i * 36, 200, 220));
        }
        std::vector<StrokeSample> wave;
        for (int i = 0; i <= 80; ++i)
            wave.push_back(sample(20 + i * 4.5, 100 + 50 * std::sin(i * 0.12), 0.3 + 0.7 * std::sin(Pi * i / 80), i * 6.0));
        for (const QImage &background : {canvas(), stripes}) {
            for (const QImage &mask : {QImage(), clip}) {
                QImage dense = background.copy();
                TiledImage tiled = TiledImage::fromImage(background);
                TileSnapshot denseBefore, before;
                StrokeEngine a(s, Ink, 7, 0), b(s, Ink, 7, 0);
                if (!mask.isNull()) {
                    a.setClipMask(mask);
                    b.setClipMask(mask);
                }
                a.begin(dense, wave.front(), denseBefore);
                b.begin(tiled, wave.front(), before);
                for (size_t i = 1; i < wave.size(); ++i) {
                    a.moveTo(dense, wave[i], denseBefore);
                    b.moveTo(tiled, wave[i], before);
                }
                a.finish(dense, denseBefore);
                b.finish(tiled, before);
                QVERIFY(validPremultiplied(dense));
                QVERIFY(tiled == dense);
                before.swapWith(tiled);
                QVERIFY(tiled == background);
                denseBefore.swapWith(dense);
                QCOMPARE(dense, background);
            }
        }
    }

    void inkHighFrequencyInputDrawsTheSameLine()
    {
        // The same pen path reported at 1000 Hz and at 125 Hz draws nearly the same line with curve
        // interpolation (the curve through the sparse samples follows the dense ones).
        BrushSettings s = BrushPresets::builtIn(QStringLiteral("builtin.fine-liner"))->settings;
        auto path = [](int events) {
            std::vector<StrokeSample> v;
            for (int i = 0; i <= events; ++i) {
                const double t = double(i) / events;
                v.push_back(sample(40 + 320 * t, 100 + 60 * std::sin(t * 3 * Pi), 1.0, t * 400.0));
            }
            return v;
        };
        const QImage dense = render(s, path(400), canvas()), sparse = render(s, path(50), canvas());
        int differ = 0, painted = 0;
        for (int y = 0; y < dense.height(); ++y)
            for (int x = 0; x < dense.width(); ++x) {
                painted += qAlpha(dense.pixel(x, y)) > 128;
                differ += std::abs(qAlpha(dense.pixel(x, y)) - qAlpha(sparse.pixel(x, y))) > 128;
            }
        QVERIFY2(differ < painted / 20, qPrintable(QStringLiteral("%1 of %2").arg(differ).arg(painted)));
    }

    void speedAdaptiveSmoothingFollowsFastStrokes()
    {
        // A fast zig-zag of amplitude 20 px under a long smoothing string: without adaptation the
        // string flattens it; with adaptation the line keeps most of its amplitude. A slow stroke
        // stays as smooth as before.
        BrushSettings s;
        s.size = 3;
        s.pressureSize = {false, 0, 1};
        s.smoothing = 100;
        auto zigzag = [](double msPerEvent) {
            std::vector<StrokeSample> v;
            for (int i = 0; i <= 40; ++i)
                v.push_back(sample(40 + i * 8.0, 100 + (i % 2 ? 20 : -20), 1.0, i * msPerEvent));
            return v;
        };
        auto amplitude = [](const QImage &img) {
            int top = 1000, bottom = -1;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 150; x < 250; ++x)
                    if (qAlpha(img.pixel(x, y)) > 128) {
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
            return bottom - top;
        };
        auto draw = [&](const BrushSettings &b, double msPerEvent) {
            QImage img = canvas();
            TileSnapshot before;
            StrokeEngine engine(b, Ink, 7, StrokeEngine::smoothingRadius(b));
            const auto v = zigzag(msPerEvent);
            engine.begin(img, v.front(), before);
            for (size_t i = 1; i < v.size(); ++i)
                engine.moveTo(img, v[i], before);
            engine.finish(img, before);
            return img;
        };
        BrushSettings adaptive = s;
        adaptive.smoothingSpeedAdapt = 1.0;
        const int fixedFast = amplitude(draw(s, 2.0)), adaptiveFast = amplitude(draw(adaptive, 2.0));
        const int fixedSlow = amplitude(draw(s, 200.0)), adaptiveSlow = amplitude(draw(adaptive, 200.0));
        qInfo("zig-zag amplitude: fast %d → %d with adaptation; slow %d → %d", fixedFast, adaptiveFast, fixedSlow, adaptiveSlow);
        QVERIFY2(adaptiveFast > fixedFast + 15, qPrintable(QStringLiteral("%1 vs %2").arg(adaptiveFast).arg(fixedFast)));
        QVERIFY2(std::abs(adaptiveSlow - fixedSlow) <= 2, qPrintable(QStringLiteral("%1 vs %2").arg(adaptiveSlow).arg(fixedSlow)));
        // Smoothing distances are view pixels: at 2× zoom the string in document pixels halves, so
        // the line feels the same at every zoom.
        QCOMPARE(StrokeEngine::smoothingRadius(s, 2.0), StrokeEngine::smoothingRadius(s, 1.0) / 2.0);
    }

    void inkPresetGoldenImages()
    {
        for (const QString &id : {QStringLiteral("g-pen"), QStringLiteral("fine-liner"), QStringLiteral("brush-pen"),
                                  QStringLiteral("dry-ink")}) {
            const BrushPreset *p = BrushPresets::builtIn(QStringLiteral("builtin.") + id);
            QVERIFY2(p, qPrintable(id));
            QImage img = canvas(QSize(400, 200), Qt::white);
            paint(img, p->settings, wobble());
            paint(img, p->settings, line({50, 160}, {350, 150}, 8, 0.7), 11); // sparse, fast
            checkGolden(id, img);
        }
    }

    // --- smudge and mixing (A6, research/m11-paint-simulation.md §6) ---

    void mixFourOperationsAreDistinct()
    {
        // Over 8 px red/blue stripes, with a yellow brush colour, along the same stroke:
        // - smearing moves the pixels: the stripe structure survives (high variation along x);
        // - mixing carries one colour: the stripes average out (low variation) and no yellow appears;
        // - painting deposits the brush colour: yellow, flat;
        // - wet paint deposits and picks up: yellow mixed into the stripes, still structured.
        QImage stripes = canvas(QSize(400, 200));
        {
            QPainter p(&stripes);
            for (int x = 0; x < 400; x += 16) {
                p.fillRect(QRect(x, 0, 8, 200), QColor(220, 30, 30));
                p.fillRect(QRect(x + 8, 0, 8, 200), QColor(30, 60, 220));
            }
        }
        const QColor yellow(240, 200, 20);
        const auto stroke = line({30, 100}, {370, 100}, 80);
        auto stats = [](const QImage &img) {
            // Along the centre line, x 120..280: mean colour and mean |Δ| of red between neighbours.
            double r = 0, g = 0, b = 0, variation = 0;
            int n = 0;
            for (int x = 120; x <= 280; ++x) {
                const QColor c = img.pixelColor(x, 100);
                r += c.red();
                g += c.green();
                b += c.blue();
                variation += std::abs(qRed(img.pixel(x + 1, 100)) - qRed(img.pixel(x, 100)));
                ++n;
            }
            return std::array<double, 4>{r / n, g / n, b / n, variation / n};
        };
        BrushSettings smear;
        smear.blend = BrushBlend::Smudge;
        smear.size = 30;
        smear.smudgeStrength = 0.9;
        smear.smudgePickup = 0.1;
        smear.pressureSize = {false, 0, 1};
        BrushSettings mix = smear;
        mix.smudgeMode = SmudgeMode::Mix;
        mix.smudgeLength = 0.85;
        BrushSettings paintBrush;
        paintBrush.size = 30;
        paintBrush.pressureSize = {false, 0, 1};
        BrushSettings wet = smear;
        wet.blend = BrushBlend::WetPaint;
        const auto s = stats(render(smear, stroke, stripes, 7, yellow));
        const auto m = stats(render(mix, stroke, stripes, 7, yellow));
        const auto p = stats(render(paintBrush, stroke, stripes, 7, yellow));
        const auto w = stats(render(wet, stroke, stripes, 7, yellow));
        qInfo("variation: smear %.1f, mix %.1f, paint %.1f, wet %.1f; green: smear %.0f, mix %.0f, paint %.0f, wet %.0f",
              s[3], m[3], p[3], w[3], s[1], m[1], p[1], w[1]);
        QVERIFY2(s[3] > 4 * m[3], "smearing keeps structure, mixing averages it");
        QVERIFY2(m[1] < 80, "mixing adds no brush colour without a colour rate");
        QVERIFY2(p[0] > 225 && p[1] > 185 && p[2] < 40 && p[3] < 1, "painting deposits flat brush colour");
        QVERIFY2(w[1] > s[1] + 40 && w[3] > m[3], "wet paint deposits brush colour into structured paint");
        // Mixing averages the two stripe colours (neither red nor blue dominates).
        QVERIFY2(std::abs(m[0] - m[2]) < 70, qPrintable(QStringLiteral("mix r %1 b %2").arg(m[0]).arg(m[2])));
    }

    void mixCarriesColourAsFarAsTheLengthSays()
    {
        // From red into white: a long smudge length drags red much further than a short one.
        QImage img = canvas(QSize(400, 200), Qt::white);
        {
            QPainter p(&img);
            p.fillRect(QRect(0, 0, 110, 200), QColor(210, 30, 40));
        }
        BrushSettings s;
        s.blend = BrushBlend::Smudge;
        s.smudgeMode = SmudgeMode::Mix;
        s.size = 30;
        s.smudgeStrength = 0.9;
        s.pressureSize = {false, 0, 1};
        auto redness = [](const QImage &out) {
            std::vector<double> r;
            for (int x = 140; x <= 380; x += 40) {
                const QColor c = out.pixelColor(x, 100);
                r.push_back(c.red() - c.green());
            }
            return r;
        };
        s.smudgeLength = 0.3;
        const auto shortRed = redness(render(s, line({30, 100}, {390, 100}, 90), img));
        s.smudgeLength = 0.92;
        const auto longRed = redness(render(s, line({30, 100}, {390, 100}, 90), img));
        double sumShort = 0, sumLong = 0;
        for (size_t i = 0; i < longRed.size(); ++i) {
            sumShort += shortRed[i];
            sumLong += longRed[i];
        }
        qInfo("redness beyond the red: short length %.0f, long length %.0f", sumShort, sumLong);
        QVERIFY2(sumLong > 3 * sumShort + 30, qPrintable(QStringLiteral("%1 vs %2").arg(sumLong).arg(sumShort)));
        QVERIFY2(longRed.front() > longRed.back(), "the carried colour fades with distance");
    }

    void pigmentMixingTurnsBlueAndYellowGreen()
    {
        // The mixing model on its own, then through a brush: blue paint under a yellow colour rate.
        const float blue[3] = {25, 60, 200}, yellow[3] = {245, 210, 20};
        float paint[3];
        PigmentMix::mixStraight(blue, yellow, 0.5, paint);
        qInfo("blue + yellow, pigment: %.0f %.0f %.0f (RGB average 135 135 110)", paint[0], paint[1], paint[2]);
        QVERIFY2(paint[1] > paint[0] + 15 && paint[1] > paint[2] + 15, "green dominates");
        // Transparency never darkens: mixing with a fully transparent colour keeps the other.
        float c[4] = {200, 40, 40, 255};
        const float clear[4] = {0, 0, 0, 0};
        PigmentMix::mix(c, clear, 0.5f, true);
        QVERIFY(std::abs(c[0] / c[3] * 255 - 200) < 1 && std::abs(c[3] - 127.5f) < 0.5f);

        QImage img = canvas(QSize(400, 200), QColor(25, 60, 200));
        BrushSettings s;
        s.blend = BrushBlend::Smudge;
        s.smudgeMode = SmudgeMode::Mix;
        s.size = 40;
        s.smudgeStrength = 0.95;
        s.smudgeLength = 0.5;
        s.smudgeColorRate = 0.6;
        s.pressureSize = {false, 0, 1};
        const auto stroke = line({30, 100}, {370, 100}, 80);
        const QColor rgb = render(s, stroke, img, 7, QColor(245, 210, 20)).pixelColor(250, 100);
        s.pigmentMixing = true;
        const QColor km = render(s, stroke, img, 7, QColor(245, 210, 20)).pixelColor(250, 100);
        qInfo("on the canvas: RGB mix %d %d %d, pigment mix %d %d %d", rgb.red(), rgb.green(), rgb.blue(), km.red(),
              km.green(), km.blue());
        const int greenRgb = rgb.green() - std::max(rgb.red(), rgb.blue()), greenKm = km.green() - std::max(km.red(), km.blue());
        QVERIFY2(greenKm > greenRgb + 15, qPrintable(QStringLiteral("%1 vs %2").arg(greenKm).arg(greenRgb)));
    }

    void mixIsAlphaAware()
    {
        // Carried red over transparency deposits red (partly transparent, no dark fringe); carried
        // transparency over red lifts it (erases towards the carried alpha).
        QImage half = canvas();
        {
            QPainter p(&half);
            p.fillRect(QRect(0, 0, 200, 200), QColor(210, 30, 40));
        }
        BrushSettings s;
        s.blend = BrushBlend::Smudge;
        s.smudgeMode = SmudgeMode::Mix;
        s.size = 30;
        s.smudgeStrength = 0.8;
        s.smudgeLength = 0.9;
        s.pressureSize = {false, 0, 1};
        for (const bool pigment : {false, true}) {
            s.pigmentMixing = pigment;
            const QImage out = render(s, line({40, 100}, {360, 100}, 80), half);
            QVERIFY(validPremultiplied(out));
            int checked = 0;
            for (int x = 210; x < 300; ++x) {
                const QRgb p = out.pixel(x, 100);
                if (qAlpha(p) < 20)
                    continue;
                ++checked;
                const double r = qRed(p) * 255.0 / qAlpha(p), g = qGreen(p) * 255.0 / qAlpha(p);
                QVERIFY2(r > 170 && g < 70, qPrintable(QStringLiteral("x %1: %2 %3 (alpha %4)").arg(x).arg(r).arg(g).arg(qAlpha(p))));
            }
            QVERIFY2(checked > 30, qPrintable(QString::number(checked)));
            const QImage back = render(s, line({360, 100}, {40, 100}, 80), half);
            QVERIFY2(qAlpha(back.pixel(180, 100)) < 200, qPrintable(QString::number(qAlpha(back.pixel(180, 100)))));
            QVERIFY(validPremultiplied(back));
        }
    }

    void mixDepletesAndFollowsPressure()
    {
        // Depletion weakens the carried paint with distance; the colour mixer's colour rate follows
        // pressure (a light touch blends, pressure paints).
        QImage img = canvas(QSize(400, 200), Qt::white);
        {
            QPainter p(&img);
            p.fillRect(QRect(0, 0, 60, 200), QColor(20, 30, 160));
        }
        BrushSettings s;
        s.blend = BrushBlend::Smudge;
        s.smudgeMode = SmudgeMode::Mix;
        s.size = 30;
        s.smudgeStrength = 0.9;
        s.smudgeLength = 0.97;
        s.pressureSize = {false, 0, 1};
        const auto stroke = line({20, 100}, {380, 100}, 90);
        const int plain = 255 - qRed(render(s, stroke, img).pixel(330, 100));
        s.smudgeDepletion = 0.8;
        const int depleted = 255 - qRed(render(s, stroke, img).pixel(330, 100));
        qInfo("carried blue at x 330: %d without depletion, %d with", plain, depleted);
        QVERIFY2(depleted < plain / 2, qPrintable(QStringLiteral("%1 vs %2").arg(depleted).arg(plain)));

        const BrushSettings mixer = BrushPresets::builtIn(QStringLiteral("builtin.color-mixer"))->settings;
        const QImage white = canvas(QSize(400, 200), Qt::white);
        const QColor orange(230, 120, 20);
        const QColor light = render(mixer, line({30, 100}, {370, 100}, 80, 0.1), white, 7, orange).pixelColor(250, 100);
        const QColor firm = render(mixer, line({30, 100}, {370, 100}, 80, 1.0), white, 7, orange).pixelColor(250, 100);
        qInfo("colour mixer on white: light pressure %d %d %d, firm %d %d %d", light.red(), light.green(), light.blue(),
              firm.red(), firm.green(), firm.blue());
        // A light touch adds only a trace per dab (colour rate × 0.1³); over this 340 px stroke the
        // trace accumulates to a pale tint (measured blue ≈ 184 of 255), while full pressure paints
        // (blue ≈ 25, the brush colour's 20).
        QVERIFY2(light.blue() > firm.blue() + 120, "pressure adds the brush colour, a light touch mostly blends");
        QVERIFY2(firm.blue() < 60, "full pressure paints");
        // Without pressure data the dynamics keep their base value: full colour rate.
        auto noPressure = line({30, 100}, {370, 100}, 80);
        for (StrokeSample &t : noPressure)
            t.hasPressure = false;
        QVERIFY(render(mixer, noPressure, white, 7, orange).pixelColor(250, 100).blue() < 120);
    }

    void mixOnTilesSeamsAndSelections()
    {
        // Mix and smear across tile seams through a soft selection: tiled = dense, undo exact.
        QImage clip(400, 200, QImage::Format_Grayscale8);
        clip.fill(0);
        {
            QPainter p(&clip);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(QRectF(40, 20, 300, 150));
        }
        QImage bg = canvas();
        {
            QPainter p(&bg);
            for (int i = 0; i < 12; ++i)
                p.fillRect(QRect(i * 34, 0, 17, 200), QColor::fromHsv(i * 30, 220, 230, 160 + (i % 3) * 40));
        }
        std::vector<StrokeSample> seam;
        for (int i = 0; i <= 60; ++i)
            seam.push_back(sample(20 + i * 6.0, i < 30 ? 64.0 : 128.0 - (i - 30) * 2.1, 0.3 + 0.7 * (i % 10) / 10.0, i * 4.0));
        for (const QString &id : {QStringLiteral("builtin.oil-blender"), QStringLiteral("builtin.color-mixer"),
                                  QStringLiteral("builtin.smear-knife")}) {
            const BrushSettings s = BrushPresets::builtIn(id)->settings;
            for (const QImage &mask : {QImage(), clip}) {
                QImage dense = bg.copy();
                TiledImage tiled = TiledImage::fromImage(bg);
                TileSnapshot db, tb;
                StrokeEngine a(s, Ink, 7, 0), b(s, Ink, 7, 0);
                if (!mask.isNull()) {
                    a.setClipMask(mask);
                    b.setClipMask(mask);
                }
                a.begin(dense, seam.front(), db);
                b.begin(tiled, seam.front(), tb);
                for (size_t i = 1; i < seam.size(); ++i) {
                    a.moveTo(dense, seam[i], db);
                    b.moveTo(tiled, seam[i], tb);
                }
                a.finish(dense, db);
                b.finish(tiled, tb);
                QVERIFY2(validPremultiplied(dense), qPrintable(id));
                QVERIFY2(tiled == dense, qPrintable(id));
                QVERIFY2(differingPixels(dense, bg) > 200, qPrintable(id));
                if (!mask.isNull()) // outside the selection nothing changed
                    for (int y = 0; y < 200; y += 3)
                        for (int x = 0; x < 400; x += 3)
                            if (qGray(clip.pixel(x, y)) == 0)
                                QVERIFY(dense.pixel(x, y) == bg.pixel(x, y));
                tb.swapWith(tiled);
                QVERIFY(tiled == bg);
            }
        }
    }

    void mixPresetGoldenImages()
    {
        QImage base = canvas(QSize(400, 200));
        {
            QPainter p(&base);
            p.fillRect(QRect(0, 0, 400, 200), QColor(240, 236, 228));
            p.fillRect(QRect(40, 40, 120, 120), QColor(30, 70, 190));
            p.fillRect(QRect(220, 40, 120, 120), QColor(235, 200, 30));
        }
        for (const QString &id : {QStringLiteral("oil-blender"), QStringLiteral("color-mixer"), QStringLiteral("smear-knife")}) {
            const BrushPreset *p = BrushPresets::builtIn(QStringLiteral("builtin.") + id);
            QVERIFY2(p, qPrintable(id));
            QImage img = base.copy();
            paint(img, p->settings, wobble(), 7, QColor(200, 60, 40));
            checkGolden(id, img);
        }
    }

    // --- bristles (A5) ---

    void bristleLoadedAndDryBrushesDiffer()
    {
        // A loaded brush covers the stroke densely along its whole length; a dry one (little paint,
        // fast depletion) covers less from the start and breaks up further along.
        BrushSettings loaded = bristleBrush();
        BrushSettings dry = loaded;
        dry.bristleLoad = 0.35;
        dry.bristleDepletion = 0.6;
        const auto stroke = line({20, 100}, {380, 100}, 90);
        const QImage a = render(loaded, stroke, canvas()), b = render(dry, stroke, canvas());
        QVERIFY(validPremultiplied(a) && validPremultiplied(b));
        const QRect first(40, 80, 100, 41), last(260, 80, 100, 41);
        qInfo("painted pixels, first / last third: loaded %d / %d, dry %d / %d", painted(a, first), painted(a, last),
              painted(b, first), painted(b, last));
        QVERIFY(painted(a, first) > 1.5 * painted(b, first));
        QVERIFY(std::abs(painted(a, last) - painted(a, first)) < painted(a, first) / 10); // no depletion: even
        QVERIFY(painted(b, last) < 0.6 * painted(b, first));                             // depletion: runs out
    }

    void bristleFlatAndRoundBrushesDiffer()
    {
        // A flat brush (thin footprint) is broad when dragged across its width and narrow along it;
        // a round brush is the same both ways.
        auto width = [](const QImage &img) {
            int top = 1000, bottom = -1;
            for (int y = 0; y < img.height(); ++y)
                if (qAlpha(img.pixel(200, y)) > 64) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            return bottom - top + 1;
        };
        BrushSettings flat = bristleBrush();
        flat.roundness = 0.15;
        const auto stroke = line({20, 100}, {380, 100}, 90);
        flat.angle = 90; // broad edge across the horizontal stroke
        const int across = width(render(flat, stroke, canvas()));
        flat.angle = 0; // broad edge along the stroke
        const int along = width(render(flat, stroke, canvas()));
        BrushSettings round = bristleBrush();
        const int roundA = width(render(round, stroke, canvas()));
        round.angle = 90;
        const int roundB = width(render(round, stroke, canvas()));
        qInfo("stroke width: flat across %d, flat along %d, round %d / %d", across, along, roundA, roundB);
        QVERIFY(across > 3 * along);
        QVERIFY(std::abs(roundA - roundB) <= 2);
    }

    void bristlePicksUpThePaintItCrosses()
    {
        // From red paint onto bare paper (transparency) with a blue brush: with pickup the bristles
        // carry red onto the paper (multicoloured streaks); without, the paper gets only blue.
        // (Over opaque white paint the bristles would pick up the white, as they should.)
        QImage half = canvas(QSize(400, 200));
        {
            QPainter p(&half);
            p.fillRect(QRect(0, 0, 200, 200), QColor(220, 30, 30));
        }
        BrushSettings s = bristleBrush();
        auto reddish = [](const QImage &img) {
            int n = 0;
            for (int y = 80; y <= 120; ++y)
                for (int x = 220; x <= 330; ++x) {
                    const QColor c = img.pixelColor(x, y);
                    n += c.red() > c.blue() + 40 && c.alpha() > 128 && c.green() < 200;
                }
            return n;
        };
        const auto stroke = line({20, 100}, {380, 100}, 90);
        const int without = reddish(render(s, stroke, half, 7, QColor(30, 50, 200)));
        s.bristlePickup = 0.6;
        const QImage with = render(s, stroke, half, 7, QColor(30, 50, 200));
        const int withPickup = reddish(with);
        qInfo("red-dominated pixels in the white half: %d without pickup, %d with", without, withPickup);
        QVERIFY(without == 0);
        QVERIFY(withPickup > 200);
        // Multicoloured: crossing horizontal bands of different paint, each bristle picks up the band
        // it passes through, so across the stroke on the paper the colour changes from streak to
        // streak.
        QImage bands = canvas(QSize(400, 200));
        {
            QPainter p(&bands);
            const QColor colors[] = {QColor(220, 30, 30), QColor(30, 170, 60), QColor(240, 200, 20)};
            for (int i = 0; i < 12; ++i)
                p.fillRect(QRect(0, 76 + i * 4, 200, 4), colors[i % 3]);
        }
        const QImage streaks = render(s, stroke, bands, 7, QColor(30, 50, 200));
        QSet<int> hues;
        for (int y = 82; y <= 118; ++y) {
            const QColor c = streaks.pixelColor(260, y);
            if (c.alpha() > 128 && c.saturation() > 60)
                hues.insert(c.hue() / 30);
        }
        qInfo("hue groups across the stroke after the bands: %d", int(hues.size()));
        QVERIFY2(hues.size() >= 3, qPrintable(QString::number(hues.size())));
    }

    void bristleContactFollowsPressure()
    {
        // Light pressure: few, gathered bristles (narrow, sparse); full pressure: all, spread.
        BrushSettings s = bristleBrush();
        s.bristleLengthJitter = 0.8;
        const QImage light = render(s, line({20, 100}, {380, 100}, 90, 0.25), canvas());
        const QImage firm = render(s, line({20, 100}, {380, 100}, 90, 1.0), canvas());
        const QRect core(60, 70, 280, 61);
        auto width = [](const QImage &img) {
            int top = 1000, bottom = -1;
            for (int y = 0; y < img.height(); ++y)
                if (qAlpha(img.pixel(200, y)) > 64) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            return bottom - top + 1;
        };
        qInfo("painted pixels: light %d, firm %d; width light %d, firm %d", painted(light, core, 64), painted(firm, core, 64),
              width(light), width(firm));
        QVERIFY(painted(firm, core, 64) > 1.5 * painted(light, core, 64));
        QVERIFY(width(light) < 0.75 * width(firm));
        // Without pressure data (mouse) every bristle touches, fully spread.
        auto mouse = line({20, 100}, {380, 100}, 90, 0.25);
        for (StrokeSample &t : mouse)
            t.hasPressure = false;
        QCOMPARE(render(s, mouse, canvas()), firm);
    }

    void bristleStreaksRunAlongTheStroke()
    {
        BrushSettings s = bristleBrush();
        s.bristleCount = 30;
        const QImage img = render(s, line({20, 100}, {380, 100}, 90), canvas());
        double along = 0, across = 0;
        for (int y = 85; y < 115; ++y)
            for (int x = 60; x < 340; ++x) {
                along += std::abs(qAlpha(img.pixel(x + 1, y)) - qAlpha(img.pixel(x, y)));
                across += std::abs(qAlpha(img.pixel(x, y + 1)) - qAlpha(img.pixel(x, y)));
            }
        qInfo("bristle streaks: change along %.0f, across %.0f", along, across);
        QVERIFY(along * 2.5 < across);
    }

    void bristleOnTilesSelectionsAndSeeds()
    {
        QImage clip(400, 200, QImage::Format_Grayscale8);
        clip.fill(0);
        {
            QPainter p(&clip);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(QRectF(40, 20, 300, 150));
        }
        QImage bg = canvas();
        {
            QPainter p(&bg);
            for (int i = 0; i < 12; ++i)
                p.fillRect(QRect(i * 34, 0, 17, 200), QColor::fromHsv(i * 30, 220, 230));
        }
        std::vector<StrokeSample> seam;
        for (int i = 0; i <= 60; ++i)
            seam.push_back(sample(20 + i * 6.0, i < 30 ? 64.0 : 128.0 - (i - 30) * 2.1, 0.3 + 0.7 * (i % 10) / 10.0, i * 4.0));
        for (const QString &id : {QStringLiteral("builtin.round-bristle"), QStringLiteral("builtin.flat-bristle"),
                                  QStringLiteral("builtin.dry-brush")}) {
            const BrushSettings s = BrushPresets::builtIn(id)->settings;
            for (const QImage &mask : {QImage(), clip}) {
                QImage dense = bg.copy();
                TiledImage tiled = TiledImage::fromImage(bg);
                TileSnapshot db, tb;
                StrokeEngine a(s, Ink, 7, 0), b(s, Ink, 7, 0);
                if (!mask.isNull()) {
                    a.setClipMask(mask);
                    b.setClipMask(mask);
                }
                a.begin(dense, seam.front(), db);
                b.begin(tiled, seam.front(), tb);
                for (size_t i = 1; i < seam.size(); ++i) {
                    a.moveTo(dense, seam[i], db);
                    b.moveTo(tiled, seam[i], tb);
                }
                a.finish(dense, db);
                b.finish(tiled, tb);
                QVERIFY2(validPremultiplied(dense), qPrintable(id));
                QVERIFY2(tiled == dense, qPrintable(id));
                if (!mask.isNull())
                    for (int y = 0; y < 200; y += 3)
                        for (int x = 0; x < 400; x += 3)
                            if (qGray(clip.pixel(x, y)) == 0)
                                QVERIFY(dense.pixel(x, y) == bg.pixel(x, y));
                tb.swapWith(tiled);
                QVERIFY(tiled == bg);
            }
            QCOMPARE(render(s, seam, bg, 3), render(s, seam, bg, 3));
            QVERIFY(differingPixels(render(s, seam, bg, 3), render(s, seam, bg, 4)) > 100);
        }
    }

    void bristlePresetGoldenImages()
    {
        QImage base = canvas(QSize(400, 200), QColor(244, 240, 232));
        {
            QPainter p(&base);
            p.fillRect(QRect(150, 0, 100, 200), QColor(200, 60, 40));
        }
        for (const QString &id : {QStringLiteral("round-bristle"), QStringLiteral("flat-bristle"), QStringLiteral("dry-brush")}) {
            const BrushPreset *p = BrushPresets::builtIn(QStringLiteral("builtin.") + id);
            QVERIFY2(p, qPrintable(id));
            QImage img = base.copy();
            paint(img, p->settings, wobble(), 7, QColor(40, 70, 160));
            checkGolden(id, img);
        }
    }

    void dryPresetGoldenImages()
    {
        for (const QString &id : {QStringLiteral("graphite-2h"), QStringLiteral("graphite-hb"), QStringLiteral("graphite-6b"),
                                  QStringLiteral("vine-charcoal"), QStringLiteral("compressed-charcoal"), QStringLiteral("chalk"),
                                  QStringLiteral("soft-pastel")}) {
            const BrushPreset *p = BrushPresets::builtIn(QStringLiteral("builtin.") + id);
            QVERIFY2(p, qPrintable(id));
            QImage img = canvas(QSize(400, 200), Qt::white);
            paint(img, p->settings, wobble());
            paint(img, p->settings, line({50, 150}, {350, 150}, 40, 0.5), 11); // a second, lighter stroke
            checkGolden(id, img);
        }
    }

    // --- watercolour (A7: a bounded cellular approximation, not a fluid simulation) ---

    void watercolorWetOnDryHasHardDarkEdges()
    {
        // On dry paper the paint stops where the water stops: nothing beyond the brush's reach.
        // With edge darkening water flows to the edge and evaporates there, leaving its pigment:
        // the rim dries clearly darker than the middle. Without it the rim is at most a little
        // darker (the thin rim dries first, so some pigment still flows out).
        double ratio[2] = {0, 0};
        int i = 0;
        for (const double edge : {0.0, 0.9}) {
            QImage img = canvas(QSize(400, 200), Qt::white);
            WetLayer wet(img.size());
            wetStroke(img, wet, watercolor(0.5, edge), line({60, 100}, {340, 100}, 40));
            QVERIFY(wet.isWet()); // still wet at pen-up: the stroke lasted 320 ms
            dry(img, wet);
            QVERIFY(!wet.isWet());
            QVERIFY(validPremultiplied(img));
            for (int x = 120; x <= 280; ++x) {
                for (int y = 0; y < 100 - 17; ++y)
                    QVERIFY2(img.pixel(x, y) == qRgb(255, 255, 255), qPrintable(QStringLiteral("%1, %2").arg(x).arg(y)));
                for (int y = 100 + 18; y < 200; ++y)
                    QVERIFY(img.pixel(x, y) == qRgb(255, 255, 255));
            }
            // The edge from paint to paper is a few pixels wide (anti-aliased, not a gradient).
            const int peak = std::max(darkness(img, 200, 100), darkness(img, 200, 88));
            int inside = 100, outside = 100;
            for (int y = 100; y > 70; --y) {
                if (darkness(img, 200, y) >= 0.5 * peak)
                    inside = y;
                if (darkness(img, 200, y) > 0.1 * peak)
                    outside = y;
            }
            // The rim: the darkest one-pixel band in the outer third of the stroke (both sides).
            const double centre = darknessAt(img, 120, 280, 100, 0, 4);
            double rim = 0;
            for (int d = 10; d <= 15; ++d)
                rim = std::max(rim, darknessAt(img, 120, 280, 100, d, d));
            ratio[i++] = rim / centre;
            qInfo("edge darkening %.1f: centre %.1f, rim %.1f (ratio %.2f), edge 50-10 %% %d px", edge, centre, rim,
                  rim / centre, inside - outside);
            QVERIFY(centre > 20);
            QVERIFY(inside - outside <= 4);
        }
        // Measured at this commit: 1.03 without, 1.13 with (the one-pixel band; the rim is 1-2 px).
        QVERIFY2(ratio[0] < 1.07, "no edge darkening: the rim is at most a little darker");
        QVERIFY2(ratio[1] > 1.1 && ratio[1] > ratio[0] + 0.07, "edge darkening: the rim dries darker than the middle");
    }

    void watercolorWetOnWetBleeds()
    {
        // Clear water first, then pigment across it while it is still wet: the pigment spreads
        // into the wet band, but next to the band (dry paper) it stays where the brush put it.
        QImage img = canvas(QSize(400, 200), Qt::white);
        WetLayer wet(img.size());
        BrushSettings water = watercolor(0.8, 0.3);
        water.size = 60;
        water.wcPigment = 0.0;
        water.wcDryingTime = 8;
        wetStroke(img, wet, water, line({20, 100}, {380, 100}, 30));
        QCOMPARE(img, canvas(QSize(400, 200), Qt::white)); // clear water shows nothing
        BrushSettings pigment = watercolor(0.8, 0.3);
        pigment.size = 10;
        pigment.wcWater = 0.3;
        pigment.wcPigment = 0.9;
        wetStroke(img, wet, pigment, line({200, 10}, {200, 190}, 30));
        dry(img, wet);
        auto width = [](const QImage &image, int y) {
            int n = 0;
            for (int x = 150; x <= 250; ++x)
                n += darkness(image, x, y) > 8;
            return n;
        };
        const int inBand = width(img, 100), onDry = width(img, 30);
        qInfo("pigment width in the wet band %d px, on dry paper %d px", inBand, onDry);
        QVERIFY(onDry >= 6 && onDry <= 14);
        QVERIFY2(inBand >= onDry + 8, "wet-on-wet: the pigment bleeds into the wet paper");

        // The same pigment stroke once the water has dried: no bleeding.
        QImage dryImg = canvas(QSize(400, 200), Qt::white);
        WetLayer wet2(dryImg.size());
        wetStroke(dryImg, wet2, water, line({20, 100}, {380, 100}, 30));
        dry(dryImg, wet2);
        wetStroke(dryImg, wet2, pigment, line({200, 10}, {200, 190}, 30));
        dry(dryImg, wet2);
        const int dryBand = width(dryImg, 100);
        qInfo("on the dried band %d px", dryBand);
        QVERIFY(dryBand <= onDry + 2);
    }

    void watercolorGranulatesInThePaperValleys()
    {
        // Settling is faster in the paper's valleys: with granulation the pigment collects there.
        double contrast[2] = {0, 0};
        int i = 0;
        for (const double granulation : {0.0, 1.0}) {
            BrushSettings s = watercolor(0.3, 0.0, granulation);
            s.size = 90;
            s.textureStrength = 1.0;
            s.wcDryingTime = 6;
            QImage img = canvas(QSize(400, 200), Qt::white);
            WetLayer wet(img.size());
            wetStroke(img, wet, s, line({60, 100}, {340, 100}, 40));
            dry(img, wet);
            double v = 0, p = 0;
            int nv = 0, np = 0;
            for (int y = 75; y <= 125; ++y)
                for (int x = 110; x <= 290; ++x) {
                    const double h = paperHeight(QStringLiteral("rough"), x, y);
                    if (h < 0.3) {
                        v += darkness(img, x, y);
                        ++nv;
                    } else if (h > 0.7) {
                        p += darkness(img, x, y);
                        ++np;
                    }
                }
            QVERIFY(nv > 100 && np > 100);
            contrast[i++] = v / nv - p / np;
            qInfo("granulation %.0f: valleys %.1f, peaks %.1f", granulation, v / nv, p / np);
        }
        QVERIFY2(std::abs(contrast[0]) < 3, "no granulation: the paper does not show");
        QVERIFY2(contrast[1] > 8, "granulation: the valleys are darker than the peaks");
    }

    void watercolorDriesInItsDryingTime()
    {
        int steps[2] = {0, 0};
        int i = 0;
        for (const double seconds : {1.0, 4.0}) {
            BrushSettings s = watercolor();
            s.wcDryingTime = seconds;
            QImage img = canvas(QSize(400, 200), Qt::white);
            WetLayer wet(img.size());
            wetStroke(img, wet, s, line({60, 100}, {340, 100}, 40));
            steps[i++] = dry(img, wet);
            QVERIFY(!wet.isWet());
            QCOMPARE(wet.water(200, 100), 0.0f);
            QCOMPARE(wet.suspended(200, 100), 0.0f);
            QVERIFY(wet.settled(200, 100) > 0.0f);
            // Dry paint is fixed: further steps change nothing.
            const QImage done = img.copy();
            DenseSurface surface(img);
            TileSnapshot before;
            QVERIFY(wet.step(surface, before, 50).isEmpty());
            QCOMPARE(img, done);
        }
        const double ms[2] = {steps[0] * WetLayer::StepMs, steps[1] * WetLayer::StepMs};
        qInfo("dried after %.0f ms (1 s setting) and %.0f ms (4 s setting) of simulation time", ms[0], ms[1]);
        QVERIFY(steps[1] > 2.5 * steps[0]);
        QVERIFY(ms[0] < 3000 && ms[1] < 12000);
    }

    void watercolorLiftsDriedPaintOnlyWithLifting()
    {
        for (const double lifting : {0.0, 1.0}) {
            QImage img = canvas(QSize(400, 200), Qt::white);
            WetLayer wet(img.size());
            BrushSettings wash = watercolor(0.4, 0.0);
            wash.size = 70;
            wash.wcPigment = 0.8;
            wetStroke(img, wet, wash, line({40, 100}, {360, 100}, 40));
            dry(img, wet);
            const QImage washed = img.copy();
            BrushSettings water = watercolor(0.4, 0.6);
            water.size = 24;
            water.wcPigment = 0.0;
            water.wcLifting = lifting;
            wetStroke(img, wet, water, line({100, 100}, {300, 100}, 30));
            dry(img, wet);
            const double before = darknessAt(washed, 140, 260, 100, 0, 3), after = darknessAt(img, 140, 260, 100, 0, 3);
            qInfo("lifting %.0f: centre darkness %.1f -> %.1f", lifting, before, after);
            if (lifting == 0.0)
                QCOMPARE(img, washed); // clear water over dried paint, nothing lifted: unchanged
            else
                QVERIFY2(after < 0.85 * before, "lifting takes pigment out of the stroke's middle");
        }
    }

    void watercolorGlazesBuildUpAndStayTransparent()
    {
        QImage bg = canvas(QSize(400, 200), Qt::white);
        {
            QPainter p(&bg);
            for (int i = 0; i < 8; ++i)
                p.fillRect(QRect(i * 50, 0, 25, 200), QColor(230, 200, 60));
        }
        QImage once = bg.copy();
        WetLayer wet(once.size());
        BrushSettings s = watercolor(0.4, 0.2);
        s.size = 60;
        wetStroke(once, wet, s, line({30, 100}, {370, 100}, 40));
        dry(once, wet);
        QImage twice = once.copy();
        wetStroke(twice, wet, s, line({30, 100}, {370, 100}, 40), QColor(40, 70, 160), 8);
        dry(twice, wet);
        const double d1 = darknessAt(once, 60, 340, 100, 0, 8), d2 = darknessAt(twice, 60, 340, 100, 0, 8);
        qInfo("one glaze %.1f, two glazes %.1f", d1, d2);
        QVERIFY2(d2 > d1 + 15, "a second glaze over dried paint deepens it");
        // Transparent: the stripes under the paint still show.
        const int under = std::abs(qGray(twice.pixel(110, 100)) - qGray(twice.pixel(135, 100)));
        qInfo("stripe contrast under two glazes %d", under);
        QVERIFY(under > 15);
        QVERIFY(validPremultiplied(twice));
    }

    void watercolorOnTilesSelectionsSeamsAndSeeds()
    {
        QImage clip(400, 200, QImage::Format_Grayscale8);
        clip.fill(0);
        {
            QPainter p(&clip);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(QRectF(40, 20, 300, 150));
        }
        QImage bg = canvas();
        {
            QPainter p(&bg);
            for (int i = 0; i < 12; ++i)
                p.fillRect(QRect(i * 34, 0, 17, 200), QColor::fromHsv(i * 30, 220, 230));
        }
        std::vector<StrokeSample> seam;
        for (int i = 0; i <= 60; ++i)
            seam.push_back(sample(20 + i * 6.0, i < 30 ? 64.0 : 128.0 - (i - 30) * 2.1, 0.3 + 0.7 * (i % 10) / 10.0, i * 8.0));
        for (const QString &id : {QStringLiteral("builtin.watercolor-wash"), QStringLiteral("builtin.watercolor-round")}) {
            const BrushSettings s = BrushPresets::builtIn(id)->settings;
            for (const QImage &mask : {QImage(), clip}) {
                QImage dense = bg.copy();
                TiledImage tiled = TiledImage::fromImage(bg);
                TileSnapshot db, tb;
                StrokeEngine a(s, Ink, 7, 0), b(s, Ink, 7, 0);
                if (!mask.isNull()) {
                    a.setClipMask(mask);
                    b.setClipMask(mask);
                }
                a.begin(dense, seam.front(), db);
                b.begin(tiled, seam.front(), tb);
                for (size_t i = 1; i < seam.size(); ++i) {
                    a.moveTo(dense, seam[i], db);
                    b.moveTo(tiled, seam[i], tb);
                }
                a.finish(dense, db);
                b.finish(tiled, tb);
                QVERIFY2(validPremultiplied(dense), qPrintable(id));
                QVERIFY2(tiled == dense, qPrintable(id));
                QVERIFY2(differingPixels(dense, bg) > 1000, qPrintable(id));
                if (!mask.isNull())
                    for (int y = 0; y < 200; y += 3)
                        for (int x = 0; x < 400; x += 3)
                            if (qGray(clip.pixel(x, y)) == 0)
                                QVERIFY(dense.pixel(x, y) == bg.pixel(x, y));
                tb.swapWith(tiled);
                QVERIFY(tiled == bg);
            }
            QCOMPARE(render(s, seam, bg, 3), render(s, seam, bg, 3));
        }
        // Seams: on smooth paper the same stroke moved by (37, 21) pixels, so it meets the 64-pixel
        // tile grid elsewhere, gives the same pixels moved by the same amount.
        BrushSettings smooth = watercolor(0.7, 0.8);
        smooth.size = 40;
        std::vector<StrokeSample> moved;
        for (const StrokeSample &p : seam) {
            StrokeSample q = p;
            q.pos += QPointF(37, 21);
            moved.push_back(q);
        }
        const QImage white = canvas(QSize(480, 280), Qt::white);
        const QImage at = render(smooth, seam, white), shifted = render(smooth, moved, white);
        QVERIFY(differingPixels(at, white) > 1000);
        QCOMPARE(shifted.copy(37, 21, 400, 200), at.copy(0, 0, 400, 200));
    }

    void watercolorPresetGoldenImages()
    {
        QImage base = canvas(QSize(400, 200), QColor(244, 240, 232));
        {
            QPainter p(&base);
            p.fillRect(QRect(150, 0, 100, 200), QColor(220, 180, 60));
        }
        for (const QString &id : {QStringLiteral("watercolor-wash"), QStringLiteral("watercolor-round")}) {
            const BrushPreset *p = BrushPresets::builtIn(QStringLiteral("builtin.") + id);
            QVERIFY2(p, qPrintable(id));
            QImage img = base.copy();
            paint(img, p->settings, wobble(), 7, QColor(40, 70, 160));
            checkGolden(id, img);
        }
    }

    // --- procedural engines (A8) ---

    void proceduralPresetsAreSeededAndExactOnTilesAndSelections()
    {
        QImage clip(400, 200, QImage::Format_Grayscale8);
        clip.fill(0);
        {
            QPainter p(&clip);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(QRectF(40, 20, 300, 150));
        }
        QImage bg = canvas();
        {
            QPainter p(&bg);
            for (int i = 0; i < 12; ++i)
                p.fillRect(QRect(i * 34, 0, 17, 200), QColor::fromHsv(i * 30, 220, 230));
        }
        std::vector<StrokeSample> seam;
        for (int i = 0; i <= 60; ++i)
            seam.push_back(sample(20 + i * 6.0, i < 30 ? 64.0 : 128.0 - (i - 30) * 2.1, 0.3 + 0.7 * (i % 10) / 10.0, i * 4.0));
        for (const char *name : {"hair", "fur", "grass", "foliage", "splatter", "stitches", "chevron-trim", "crosshatch-pen",
                                 "airbrush-spray"}) {
            const QString id = QStringLiteral("builtin.") + QLatin1String(name);
            const BrushPreset *preset = BrushPresets::builtIn(id);
            QVERIFY2(preset, qPrintable(id));
            BrushSettings live = preset->settings;
            live.taperEnd = 60;
            live.taperLive = true; // procedural dabs can be redrawn: the live taper rebuilds the tail
            for (const BrushSettings &s : {preset->settings, live}) {
                for (const QImage &mask : {QImage(), clip}) {
                    QImage dense = bg.copy();
                    TiledImage tiled = TiledImage::fromImage(bg);
                    TileSnapshot db, tb;
                    StrokeEngine a(s, Ink, 7, 0), b(s, Ink, 7, 0);
                    if (!mask.isNull()) {
                        a.setClipMask(mask);
                        b.setClipMask(mask);
                    }
                    a.begin(dense, seam.front(), db);
                    b.begin(tiled, seam.front(), tb);
                    for (size_t i = 1; i < seam.size(); ++i) {
                        a.moveTo(dense, seam[i], db);
                        b.moveTo(tiled, seam[i], tb);
                    }
                    a.finish(dense, db);
                    b.finish(tiled, tb);
                    QVERIFY2(validPremultiplied(dense), qPrintable(id));
                    QVERIFY2(tiled == dense, qPrintable(id));
                    QVERIFY2(differingPixels(dense, bg) > 300, qPrintable(id));
                    if (!mask.isNull())
                        for (int y = 0; y < 200; y += 3)
                            for (int x = 0; x < 400; x += 3)
                                if (qGray(clip.pixel(x, y)) == 0)
                                    QVERIFY(dense.pixel(x, y) == bg.pixel(x, y));
                    tb.swapWith(tiled);
                    QVERIFY(tiled == bg);
                }
            }
            QCOMPARE(render(preset->settings, seam, bg, 3), render(preset->settings, seam, bg, 3));
            const bool random = preset->settings.stamp != BrushStamp::Pattern && preset->settings.stamp != BrushStamp::Hatching;
            if (random)
                QVERIFY2(differingPixels(render(preset->settings, seam, bg, 3), render(preset->settings, seam, bg, 4)) > 100,
                         qPrintable(id));
        }
    }

    void proceduralStrandsPointWhereTheStrokeGoes()
    {
        BrushSettings s;
        s.stamp = BrushStamp::Strands;
        s.size = 20;
        s.spacing = 0.2;
        s.pressureSize = {false, 0, 1};
        s.procCount = 6;
        s.procLength = 1.5;
        s.procWidth = 1.5;
        s.procLengthJitter = 0;
        s.procAngleJitter = 0;
        s.procCurl = 0;
        s.procSpread = 0.5;
        s = s.sanitized();
        // Strands start at the dabs and grow forwards: past the end of the stroke, not before its start.
        const QImage right = render(s, line({150, 100}, {250, 100}, 20), canvas());
        const QImage left = render(s, line({250, 100}, {150, 100}, 20), canvas());
        const int rightAhead = painted(right, QRect(262, 0, 138, 200), 30), rightBehind = painted(right, QRect(0, 0, 138, 200), 30);
        const int leftAhead = painted(left, QRect(0, 0, 138, 200), 30), leftBehind = painted(left, QRect(262, 0, 138, 200), 30);
        qInfo("strands ahead of / behind the stroke: right %d / %d, left %d / %d", rightAhead, rightBehind, leftAhead, leftBehind);
        QVERIFY(rightAhead > 100 && leftAhead > 100);
        QVERIFY(rightBehind < rightAhead / 5 && leftBehind < leftAhead / 5);
        // Pointing up instead: above the stroke, whichever way it goes.
        s.procDirection = ProcDirection::Up;
        for (const QImage &img : {render(s, line({150, 100}, {250, 100}, 20), canvas()), render(s, line({250, 100}, {150, 100}, 20), canvas())}) {
            const int above = painted(img, QRect(0, 0, 400, 88), 30), below = painted(img, QRect(0, 112, 400, 88), 30);
            qInfo("pointing up: %d above, %d below", above, below);
            QVERIFY(above > 300 && below < above / 10);
        }
    }

    void proceduralGrassGrowsUpAndShadesFromTheRoot()
    {
        BrushSettings s = BrushPresets::builtIn(QStringLiteral("builtin.grass"))->settings;
        s.procColorJitter = 0;
        s.procAngleJitter = 0;
        s.pressureSize = {false, 0, 1};
        const QColor green(60, 170, 70);
        for (const auto &stroke : {line({60, 150}, {340, 150}, 30), line({340, 150}, {60, 150}, 30)}) {
            const QImage img = render(s, stroke, canvas(), 7, green);
            const int above = painted(img, QRect(0, 0, 400, 140), 30), below = painted(img, QRect(0, 165, 400, 35), 30);
            // Only fully covered pixels: their colour is the blade's own (no edge blending).
            auto meanValue = [&](int y0, int y1) {
                double sum = 0;
                int n = 0;
                for (int y = y0; y <= y1; ++y)
                    for (int x = 80; x < 320; ++x)
                        if (qAlpha(img.pixel(x, y)) == 255) {
                            sum += QColor::fromRgb(img.pixel(x, y)).valueF();
                            ++n;
                        }
                return n ? sum / n : -1.0;
            };
            const double root = meanValue(146, 152), tip = meanValue(100, 112);
            qInfo("grass: %d px above the stroke, %d below; value at the roots %.2f, near the tips %.2f", above, below, root, tip);
            QVERIFY(above > 2000 && below < above / 10);
            QVERIFY(root > 0 && tip > root + 0.08);
        }
    }

    void proceduralDensityPressureAndColourVariation()
    {
        BrushSettings fur = BrushPresets::builtIn(QStringLiteral("builtin.fur"))->settings;
        const auto stroke = line({60, 100}, {340, 100}, 30);
        BrushSettings few = fur, many = fur;
        few.procCount = 3;
        many.procCount = 12;
        const int nFew = painted(render(few, stroke, canvas()), QRect(0, 0, 400, 200), 30);
        const int nMany = painted(render(many, stroke, canvas()), QRect(0, 0, 400, 200), 30);
        qInfo("fur: %d px with 3 strands per dab, %d with 12", nFew, nMany);
        QVERIFY(nMany > 2 * nFew);
        // Pressure → size scales the strands (simulated pressure).
        const BrushSettings hair = BrushPresets::builtIn(QStringLiteral("builtin.hair"))->settings;
        const int light = painted(render(hair, line({60, 100}, {340, 100}, 30, 0.3), canvas()), QRect(0, 0, 400, 200), 30);
        const int firm = painted(render(hair, line({60, 100}, {340, 100}, 30, 1.0), canvas()), QRect(0, 0, 400, 200), 30);
        qInfo("hair: %d px at pressure 0.3, %d at 1", light, firm);
        QVERIFY(light < 0.85 * firm);
        // Colour variation: every leaf its own shade.
        BrushSettings leaves = BrushPresets::builtIn(QStringLiteral("builtin.foliage"))->settings;
        auto shades = [&](double jitter) {
            leaves.procColorJitter = jitter;
            const QImage img = render(leaves, stroke, canvas(), 7, QColor(60, 140, 60));
            QSet<QRgb> set;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) == 255)
                        set.insert(img.pixel(x, y));
            return int(set.size());
        };
        const int plain = shades(0.0), varied = shades(0.6);
        qInfo("foliage: %d opaque colours without variation, %d with", plain, varied);
        QCOMPARE(plain, 1);
        QVERIFY(varied > 20);
    }

    void proceduralSplatterSizesAndStretch()
    {
        BrushSettings s = BrushPresets::builtIn(QStringLiteral("builtin.splatter"))->settings;
        s.procDirection = ProcDirection::Angle; // dab angle 0: along x
        s.procAngleJitter = 0;
        s.procCount = 40;
        s.size = 120;
        s.pressureSize = {false, 0, 1};
        const std::vector<StrokeSample> dab{sample(200, 100)};
        // Power-law sizes: more small drops (less paint) as the variation rises.
        BrushSettings even = s, skewed = s;
        even.procLengthJitter = 0;
        skewed.procLengthJitter = 1;
        const int nEven = painted(render(even, dab, canvas()), QRect(0, 0, 400, 200), 30);
        const int nSkewed = painted(render(skewed, dab, canvas()), QRect(0, 0, 400, 200), 30);
        qInfo("splatter: %d px with equal drops, %d with power-law sizes", nEven, nSkewed);
        QVERIFY(nSkewed < nEven / 2);
        // Stretch: drop edges run along x (gradients across them point along y).
        auto anisotropy = [&](double stretch) {
            BrushSettings t = s;
            t.procStretch = stretch;
            t.procLengthJitter = 0.3;
            const QImage img = render(t, dab, canvas());
            double gx = 0, gy = 0;
            for (int y = 1; y < img.height() - 1; ++y)
                for (int x = 1; x < img.width() - 1; ++x) {
                    const double dx = qAlpha(img.pixel(x + 1, y)) - qAlpha(img.pixel(x - 1, y));
                    const double dy = qAlpha(img.pixel(x, y + 1)) - qAlpha(img.pixel(x, y - 1));
                    gx += dx * dx;
                    gy += dy * dy;
                }
            return gy / std::max(1.0, gx);
        };
        const double round = anisotropy(0), stretched = anisotropy(3);
        qInfo("splatter edge anisotropy (across / along): %.2f round, %.2f stretched", round, stretched);
        QVERIFY(round > 0.7 && round < 1.4);
        QVERIFY(stretched > 1.8);
    }

    void proceduralPatternsJoinAndFollowTheStroke()
    {
        BrushSettings s;
        s.stamp = BrushStamp::Pattern;
        s.size = 20;
        s.spacing = 1.0;
        s.procWidth = 2.0;
        s.pressureSize = {false, 0, 1};
        auto columnsPainted = [](const QImage &img) {
            int n = 0;
            for (int x = 80; x <= 320; ++x) {
                bool any = false;
                for (int y = 80; y <= 120 && !any; ++y)
                    any = qAlpha(img.pixel(x, y)) > 100;
                n += any;
            }
            return n;
        };
        const auto stroke = line({40, 100}, {360, 100}, 40);
        // Zigzag: as long as the spacing, so it joins into one continuous line.
        s.patternShape = PatternShape::Zigzag;
        const QImage zig = render(s, stroke, canvas());
        // Dashes shorter than the spacing leave gaps.
        s.patternShape = PatternShape::Dashes;
        s.procLength = 0.5;
        const QImage dash = render(s, stroke, canvas());
        qInfo("columns with paint (of 241): zigzag %d, dashes %d", columnsPainted(zig), columnsPainted(dash));
        QCOMPARE(columnsPainted(zig), 241);
        QVERIFY(columnsPainted(dash) < 180);
        // Chevrons point the way the stroke goes: a stroke to the left is the mirror image of one to
        // the right, and not the same as it.
        s.patternShape = PatternShape::Chevrons;
        s.procLength = 0.6;
        const QImage right = render(s, line({40, 100}, {360, 100}, 40), canvas());
        const QImage left = render(s, line({360, 100}, {40, 100}, 40), canvas()).mirrored(true, false);
        const int total = painted(right, QRect(0, 0, 400, 200), 100);
        const int mirrorDiff = differingPixelsAbove(right, left, 100), selfDiff = differingPixelsAbove(right, right.mirrored(true, false), 100);
        qInfo("chevrons: %d px; left stroke mirrored differs in %d, the right stroke mirrored in %d", total, mirrorDiff, selfDiff);
        QVERIFY(mirrorDiff * 5 < selfDiff); // what differs is the dabs' phase, not the shape's direction
        QVERIFY(selfDiff > total);
    }

    void proceduralCrossHatchingAddsPassesWithPressure()
    {
        BrushSettings s = BrushPresets::builtIn(QStringLiteral("builtin.crosshatch-pen"))->settings;
        BrushSettings plain = s;
        plain.hatchCrossPasses = 0;
        const auto at = [&](const BrushSettings &b, double pressure) {
            return render(b, line({60, 100}, {340, 100}, 30, pressure), canvas());
        };
        // Pass 1 joins at pressure 1/3, pass 2 at 2/3.
        QCOMPARE(at(s, 0.2), at(plain, 0.2));
        const int one = painted(at(s, 0.2), QRect(0, 0, 400, 200), 100);
        const int two = painted(at(s, 0.5), QRect(0, 0, 400, 200), 100);
        const int three = painted(at(s, 1.0), QRect(0, 0, 400, 200), 100);
        qInfo("crosshatch pen: %d px at pressure 0.2 (one direction), %d at 0.5, %d at 1", one, two, three);
        QVERIFY(two > one * 1.4 && three > two * 1.2);
    }

    void proceduralSprayDistributionWeightsTheCentre()
    {
        BrushSettings s;
        s.stamp = BrushStamp::Spray;
        s.size = 100;
        s.sprayDensity = 400;
        s.sprayParticleSize = 1.0;
        s.pressureSize = {false, 0, 1};
        const std::vector<StrokeSample> dab{sample(200, 100)};
        int outside = 0; // paint beyond the dab's radius (+ a particle)
        auto innerShare = [&](SprayDistribution d) {
            s.sprayDistribution = d;
            const QImage img = render(s, dab, canvas());
            double inner = 0, all = 0;
            for (int y = 0; y < 200; ++y)
                for (int x = 0; x < 400; ++x) {
                    const double a = qAlpha(img.pixel(x, y));
                    const double r = std::hypot(x + 0.5 - 200, y + 0.5 - 100);
                    outside += a > 0 && r > 52;
                    all += a;
                    if (r < 25)
                        inner += a;
                }
            return inner / all;
        };
        const double uniform = innerShare(SprayDistribution::Uniform), gaussian = innerShare(SprayDistribution::Gaussian);
        qInfo("spray paint within half the radius: %.2f even (area share 0.25), %.2f centre-weighted (expected ~0.57)", uniform,
              gaussian);
        QCOMPARE(outside, 0);
        QVERIFY(uniform > 0.18 && uniform < 0.33);
        QVERIFY(gaussian > 0.45);
    }

    void proceduralPresetGoldenImages()
    {
        for (const char *name : {"hair", "fur", "grass", "foliage", "splatter", "stitches", "chevron-trim", "crosshatch-pen",
                                 "airbrush-spray"}) {
            const BrushPreset *p = BrushPresets::builtIn(QStringLiteral("builtin.") + QLatin1String(name));
            QVERIFY2(p, name);
            QImage img = canvas(QSize(400, 200), Qt::white);
            paint(img, p->settings, wobble(), 7, QColor(50, 90, 60));
            checkGolden(QLatin1String(name), img);
        }
    }
};

QTEST_GUILESS_MAIN(TestBrushMedia)
#include "tst_brushmedia.moc"

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
#include "StrokeEngine.h"
#include "TiledImage.h"
#include "TileSnapshot.h"

#include <QFile>
#include <QPainter>
#include <QtTest>
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
        // milestone 10 brush and a dry-media brush, on transparency and on paint; the snapshot
        // always restores the layer exactly.
        const qint64 limit = MemoryBudget::limit();
        const auto restore = qScopeGuard([&] { MemoryBudget::setLimit(limit == MemoryBudget::defaultLimit() ? 0 : limit); });
        const qint64 tile = 64 * 64 * 4;
        for (const QString &id : {QStringLiteral("builtin.solid-round"), QStringLiteral("builtin.compressed-charcoal"),
                                  QStringLiteral("builtin.textured-paint")}) {
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
};

QTEST_GUILESS_MAIN(TestBrushMedia)
#include "tst_brushmedia.moc"

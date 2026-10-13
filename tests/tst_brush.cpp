#include "BrushPresetStore.h"
#include "BrushPresets.h"
#include "BrushSettings.h"
#include "BrushTips.h"
#include "StrokeEngine.h"
#include "TileSnapshot.h"

#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

namespace {

QImage transparent(int w, int h)
{
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

QImage filled(int w, int h, const QColor &color)
{
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

// Samples along the polyline, `parts` events per segment, with constant pressure.
std::vector<StrokeSample> line(const std::vector<QPointF> &points, int parts = 8, qreal pressure = 1)
{
    std::vector<StrokeSample> out;
    StrokeSample s;
    s.pos = points.front();
    s.pressure = pressure;
    out.push_back(s);
    for (size_t i = 1; i < points.size(); ++i)
        for (int k = 1; k <= parts; ++k) {
            s.pos = points[i - 1] + (points[i] - points[i - 1]) * (qreal(k) / parts);
            out.push_back(s);
        }
    return out;
}

StrokeSample at(qreal x, qreal y, qreal pressure = 1, double timeMs = -1)
{
    StrokeSample s;
    s.pos = QPointF(x, y);
    s.pressure = pressure;
    s.timeMs = timeMs;
    return s;
}

BrushSettings roundBrush(double size, double opacity = 1.0)
{
    BrushSettings s;
    s.size = size;
    s.opacity = opacity;
    return s;
}

bool premultipliedValid(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = image.pixel(x, y);
            if (qRed(p) > qAlpha(p) || qGreen(p) > qAlpha(p) || qBlue(p) > qAlpha(p))
                return false;
        }
    return true;
}

int maxAlpha(const QImage &image)
{
    int m = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            m = std::max(m, qAlpha(image.pixel(x, y)));
    return m;
}

// Number of pixels in column x (or row y) with alpha above the threshold.
int columnExtent(const QImage &image, int x, int threshold = 127)
{
    int n = 0;
    for (int y = 0; y < image.height(); ++y)
        n += qAlpha(image.pixel(x, y)) > threshold;
    return n;
}

int rowExtent(const QImage &image, int y, int threshold = 127)
{
    int n = 0;
    for (int x = 0; x < image.width(); ++x)
        n += qAlpha(image.pixel(x, y)) > threshold;
    return n;
}

int maxChannelDifference(const QImage &a, const QImage &b)
{
    int d = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            d = std::max({d, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                          std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
        }
    return d;
}

} // namespace

class TestBrush : public QObject
{
    Q_OBJECT

private slots:
    // --- settings and preset data ---
    void settingsJsonRoundTrip()
    {
        BrushSettings s;
        s.name = QStringLiteral("Test");
        s.blend = BrushBlend::Smudge;
        s.tip = QStringLiteral("bristle");
        s.size = 33;
        s.hardness = 0.25;
        s.roundness = 0.5;
        s.angle = 30;
        s.followDirection = true;
        s.angleJitter = 12;
        s.spacing = 0.07;
        s.opacity = 0.75;
        s.flow = 0.4;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {true, 0.3, 2.0};
        s.pressureOpacity = {true, 0.1, 0.5};
        s.pressureFlow = {false, 0.2, 1.0};
        s.tiltSize = 1.5;
        s.tiltOpacity = 0.5;
        s.texture = QStringLiteral("paper");
        s.textureScale = 2;
        s.textureStrength = 0.6;
        s.textureMode = TextureMode::Subtract;
        s.airbrush = true;
        s.airbrushRate = 45;
        s.smudgeStrength = 0.3;
        s.smudgePickup = 0.7;
        s.smoothing = 25;
        const QByteArray json = QJsonDocument(s.toJson()).toJson();
        BrushSettings back;
        QString error;
        QVERIFY(BrushSettings::fromJson(QJsonDocument::fromJson(json).object(), &back, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(back == s);
    }

    void settingsValidation()
    {
        BrushSettings out;
        QString error;
        QVERIFY(!BrushSettings::fromJson(QJsonObject{{"size", "big"}}, &out, &error));
        QVERIFY(error.contains("size"));
        QVERIFY(!BrushSettings::fromJson(QJsonObject{{"blend", "glaze"}}, &out, &error));
        QVERIFY(!BrushSettings::fromJson(QJsonObject{{"pressureSize", 3}}, &out, &error));
        QVERIFY(!BrushSettings::fromJson(QJsonObject{{"pressureSize", QJsonObject{{"gamma", "x"}}}}, &out, &error));
        // Out of range is clamped, missing keys keep defaults, unknown keys are ignored.
        QVERIFY(BrushSettings::fromJson(QJsonObject{{"size", 99999}, {"opacity", -3}, {"spacing", 0}, {"future", 1}},
                                        &out, &error));
        QCOMPARE(out.size, BrushSettings::MaxSize);
        QCOMPARE(out.opacity, 0.01);
        QCOMPARE(out.spacing, 0.02);
        QCOMPARE(out.hardness, BrushSettings().hardness);
        BrushSettings nan;
        nan.size = std::nan("");
        QCOMPARE(nan.sanitized().size, BrushSettings().size);
    }

    void pressureMapping()
    {
        const PressureMapping off;
        QCOMPARE(off.apply(0.0), 1.0);
        const PressureMapping linear{true, 0.2, 1.0};
        QCOMPARE(linear.apply(0.0), 0.2);
        QCOMPARE(linear.apply(1.0), 1.0);
        QVERIFY(std::abs(linear.apply(0.5) - 0.6) < 1e-12);
        const PressureMapping firm{true, 0.0, 3.0}, soft{true, 0.0, 1.0 / 3.0};
        QVERIFY(firm.apply(0.5) < linear.apply(0.5) && soft.apply(0.5) > 0.5);
        QCOMPARE(linear.apply(std::nan("")), 1.0); // garbage pressure: full
    }

    void generatedTipsAndTextures()
    {
        const BrushTips::Tip *bristle = BrushTips::tip(QStringLiteral("bristle"));
        QVERIFY(bristle);
        QCOMPARE(bristle->mips.front().size(), QSize(128, 128));
        QCOMPARE(bristle->mips.back().size(), QSize(1, 1));
        QVERIFY(!BrushTips::tip(QStringLiteral("nope")));
        for (const QString &id : BrushTips::textureIds()) {
            const QImage &t = BrushTips::texture(id)->image;
            QCOMPARE(t.format(), QImage::Format_Grayscale8);
            qint64 sum = 0;
            for (int y = 0; y < t.height(); ++y)
                for (int x = 0; x < t.width(); ++x)
                    sum += t.constScanLine(y)[x];
            const double mean = double(sum) / (t.width() * t.height()) / 255.0;
            QVERIFY2(mean > 0.3 && mean < 0.75, qPrintable(id));
        }
        QCOMPARE(BrushTips::hash01(3, 4, 5), BrushTips::hash01(3, 4, 5));
        QVERIFY(BrushTips::hash01(3, 4, 5) != BrushTips::hash01(4, 3, 5));
    }

    // --- opacity and flow ---
    void uniformNeverDarkensAndOpacityAppliesOnce()
    {
        BrushSettings s = roundBrush(24, 0.5);
        s.flow = 0.3; // ignored by Uniform: opacity is reached in one pass
        QImage image = transparent(300, 300);
        StrokeEngine::render(image, s, Qt::black, line({{40, 40}, {260, 260}, {260, 40}, {40, 260}, {150, 150}}, 4));
        QCOMPARE(qAlpha(image.pixel(150, 150)), 128); // self-crossing
        QCOMPARE(maxAlpha(image), 128);
        QVERIFY(premultipliedValid(image));
    }

    void buildUpOnePassDepositsFlow()
    {
        for (double spacing : {0.05, 0.15, 0.25}) {
            BrushSettings s = roundBrush(20, 0.8);
            s.accumulation = Accumulation::BuildUp;
            s.flow = 0.3;
            s.spacing = spacing;
            QImage image = transparent(300, 60);
            StrokeEngine::render(image, s, Qt::black, line({{20, 30}, {280, 30}}, 20));
            // One pass: about flow × ceiling (0.3 × 204 ≈ 61) whatever the spacing.
            const int a = qAlpha(image.pixel(150, 30));
            QVERIFY2(a >= 50 && a <= 72, qPrintable(QString("spacing %1: alpha %2").arg(spacing).arg(a)));
        }
    }

    void buildUpApproachesButNeverPassesCeiling()
    {
        BrushSettings s = roundBrush(20, 0.6); // ceiling 153
        s.accumulation = Accumulation::BuildUp;
        s.flow = 0.5;
        std::vector<QPointF> scribble;
        for (int i = 0; i < 30; ++i)
            scribble.push_back(QPointF(i % 2 ? 180 : 20, 50));
        QImage image = transparent(200, 100);
        StrokeEngine::render(image, s, Qt::black, line(scribble, 6));
        QVERIFY(maxAlpha(image) <= 153);
        QVERIFY(qAlpha(image.pixel(100, 50)) >= 148);

        QImage once = transparent(200, 100);
        StrokeEngine::render(once, s, Qt::black, line({{20, 50}, {180, 50}}, 6));
        QVERIFY(qAlpha(once.pixel(100, 50)) < qAlpha(image.pixel(100, 50)) - 40);
    }

    void dotIsOneFullPass()
    {
        BrushSettings s = roundBrush(20, 1.0);
        s.accumulation = Accumulation::BuildUp;
        s.flow = 0.5;
        QImage image = transparent(60, 60);
        StrokeEngine::render(image, s, Qt::black, {at(30, 30)});
        const int a = qAlpha(image.pixel(30, 30));
        QVERIFY2(a >= 125 && a <= 130, qPrintable(QString::number(a)));

        // Uniform dot: full ceiling, exact.
        QImage uniform = transparent(60, 60);
        StrokeEngine::render(uniform, roundBrush(20, 0.5), Qt::black, {at(30, 30)});
        QCOMPARE(qAlpha(uniform.pixel(30, 30)), 128);
    }

    void airbrushDependsOnElapsedTimeNotEvents()
    {
        BrushSettings s = roundBrush(30, 1.0);
        s.hardness = 0;
        s.airbrush = true;
        s.airbrushRate = 50;
        s.flow = 0.2;
        auto hold = [&](int events, double ms) {
            QImage image = transparent(80, 80);
            TileSnapshot before;
            StrokeEngine engine(s, Qt::black, 3);
            engine.begin(image, at(40, 40, 1, 0), before);
            for (int i = 1; i <= events; ++i)
                engine.moveTo(image, at(40, 40, 1, ms * i / events), before);
            engine.finish(image, before);
            return std::make_pair(image, engine.dabCount());
        };
        const auto few = hold(10, 1000), many = hold(100, 1000);
        QCOMPARE(few.second, many.second);
        QCOMPARE(few.second, qint64(51));
        QCOMPARE(few.first, many.first);
        const auto shortHold = hold(10, 200);
        QVERIFY(qAlpha(shortHold.first.pixel(40, 40)) < qAlpha(few.first.pixel(40, 40)) - 20);
    }

    void airbrushAdvancesWithoutInputAndCapsStalls()
    {
        BrushSettings s = roundBrush(30);
        s.airbrush = true;
        s.airbrushRate = 50;
        s.flow = 0.2;
        QImage image = transparent(80, 80);
        TileSnapshot before;
        StrokeEngine engine(s, Qt::black, 1);
        QVERIFY(engine.usesTime());
        engine.begin(image, at(40, 40, 1, 0), before);
        QVERIFY(!engine.advanceTime(image, 100, before).isEmpty());
        QCOMPARE(engine.dabCount(), qint64(1 + 5));
        engine.advanceTime(image, 5000, before); // a long stall: at most MaxTimeDabsPerCall
        QCOMPARE(engine.dabCount(), qint64(1 + 5 + StrokeEngine::MaxTimeDabsPerCall));
        engine.advanceTime(image, 5010, before);
        QCOMPARE(engine.dabCount(), qint64(1 + 5 + StrokeEngine::MaxTimeDabsPerCall));

        BrushSettings plain = roundBrush(30);
        StrokeEngine still(plain, Qt::black, 1);
        QVERIFY(!still.usesTime());
        still.begin(image, at(40, 40, 1, 0), before);
        QVERIFY(still.advanceTime(image, 1000, before).isEmpty());
    }

    // --- spacing, pressure, interpolation ---
    void splittingInputGivesSamePixels()
    {
        BrushSettings s = roundBrush(18, 0.9);
        s.accumulation = Accumulation::BuildUp;
        s.flow = 0.4;
        s.texture = QStringLiteral("paper");
        s.textureStrength = 0.5;
        QImage a = transparent(300, 100), b = transparent(300, 100);
        StrokeEngine::render(a, s, Qt::black, line({{10, 30}, {290, 70}}, 1), 5);
        StrokeEngine::render(b, s, Qt::black, line({{10, 30}, {290, 70}}, 57), 5);
        QVERIFY(maxChannelDifference(a, b) <= 2);
    }

    void pressureRampTapersSize()
    {
        BrushSettings s = roundBrush(30);
        s.pressureSize = {true, 0.1, 1.0};
        std::vector<StrokeSample> ramp;
        for (int i = 0; i <= 50; ++i)
            ramp.push_back(at(20 + i * 5, 50, i / 50.0));
        QImage image = transparent(300, 100);
        StrokeEngine::render(image, s, Qt::black, ramp);
        const int thin = columnExtent(image, 40), thick = columnExtent(image, 260);
        QVERIFY2(thin < 10 && thick >= 26, qPrintable(QString("%1 %2").arg(thin).arg(thick)));

        // No pressure data arrives as 1.0 (mouse, touch): full size.
        QImage mouse = transparent(300, 100);
        StrokeEngine::render(mouse, s, Qt::black, line({{20, 50}, {270, 50}}));
        QVERIFY(columnExtent(mouse, 40) >= 29);
    }

    void pressureOpacityLightensWithoutDoubleOpacity()
    {
        BrushSettings s = roundBrush(20, 0.8);
        s.pressureOpacity = {true, 0.0, 1.0};
        QImage image = transparent(300, 60);
        StrokeEngine::render(image, s, Qt::black, line({{20, 30}, {280, 30}}, 8, 0.5));
        QCOMPARE(qAlpha(image.pixel(150, 30)), 102); // 0.8 × 0.5 × 255, once
    }

    void smallTipsAreAreaSampled()
    {
        for (double size : {1.0, 2.0, 3.0, 5.0}) {
            QImage image = transparent(20, 20);
            StrokeEngine::render(image, roundBrush(size), Qt::black, {at(10.3, 9.8)});
            double total = 0;
            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 20; ++x)
                    total += qAlpha(image.pixel(x, y)) / 255.0;
            const double area = M_PI * size * size / 4;
            QVERIFY2(std::abs(total - area) < 0.25 * area + 0.3, qPrintable(QString("%1: %2 vs %3").arg(size).arg(total).arg(area)));
        }
    }

    void noGapsOnFastStrokes()
    {
        QImage image = transparent(1000, 40);
        StrokeEngine::render(image, roundBrush(4), Qt::black, {at(5, 20), at(995, 20)}); // one 990 px event
        for (int x = 5; x <= 995; ++x)
            QVERIFY2(qAlpha(image.pixel(x, 20)) == 255, qPrintable(QString("gap at %1").arg(x)));
    }

    // --- direction and tilt ---
    void tipFollowsDirectionOrTilt()
    {
        BrushSettings s = roundBrush(40);
        s.roundness = 0.25;
        s.followDirection = true;
        QImage h = transparent(200, 200), v = transparent(200, 200);
        StrokeEngine::render(h, s, Qt::black, line({{30, 100}, {170, 100}}));
        StrokeEngine::render(v, s, Qt::black, line({{100, 30}, {100, 170}}));
        QVERIFY2(columnExtent(h, 100) <= 13, qPrintable(QString::number(columnExtent(h, 100))));
        QVERIFY(rowExtent(v, 100) <= 13);

        s.followDirection = false; // fixed angle 0: wide across a vertical stroke
        QImage fixed = transparent(200, 200);
        StrokeEngine::render(fixed, s, Qt::black, line({{100, 30}, {100, 170}}));
        QVERIFY(rowExtent(fixed, 100) >= 37);

        // A reported tilt orients the tip by the pen's azimuth instead of the travel direction.
        s.followDirection = true;
        std::vector<StrokeSample> tilted = line({{30, 100}, {170, 100}});
        for (StrokeSample &t : tilted) {
            t.hasTilt = true;
            t.tiltX = 0;
            t.tiltY = 45;
        }
        QImage byTilt = transparent(200, 200);
        StrokeEngine::render(byTilt, s, Qt::black, tilted);
        QVERIFY(columnExtent(byTilt, 100) >= 37);
    }

    void tiltScalesOnlyWhenReported()
    {
        BrushSettings s = roundBrush(20);
        s.tiltSize = 2.0;
        StrokeSample upright = at(50, 50), flat = at(50, 50), unknown = at(50, 50);
        upright.hasTilt = true;
        flat.hasTilt = true;
        flat.tiltX = 60;
        unknown.tiltX = 60; // hasTilt false: ignored
        for (const auto &[sample, width] : {std::pair{upright, 20}, std::pair{flat, 40}, std::pair{unknown, 20}}) {
            QImage image = transparent(100, 100);
            StrokeEngine::render(image, s, Qt::black, {sample});
            QVERIFY(std::abs(rowExtent(image, 50) - width) <= 1);
        }
    }

    void directionalDotStillDraws()
    {
        BrushSettings s = roundBrush(20);
        s.tip = QStringLiteral("bristle");
        s.followDirection = true;
        QImage image = transparent(60, 60);
        QVERIFY(!StrokeEngine::render(image, s, Qt::black, {at(30, 30)}).isEmpty());
        QVERIFY(maxAlpha(image) > 0);
    }

    // --- texture ---
    void textureIsCoherentInDocumentSpace()
    {
        BrushSettings s = roundBrush(40);
        s.texture = QStringLiteral("paper");
        s.textureStrength = 1.0;
        QImage h = transparent(200, 200), v = transparent(200, 200);
        StrokeEngine::render(h, s, Qt::black, line({{20, 100}, {180, 100}}), 1);
        StrokeEngine::render(v, s, Qt::black, line({{100, 20}, {100, 180}}), 2);
        for (int y = 92; y <= 108; ++y)
            for (int x = 92; x <= 108; ++x)
                QCOMPARE(h.pixel(x, y), v.pixel(x, y));
        // The grain shows: the interior is not flat.
        int lo = 255, hi = 0;
        for (int x = 92; x <= 108; ++x) {
            lo = std::min(lo, qAlpha(h.pixel(x, 100)));
            hi = std::max(hi, qAlpha(h.pixel(x, 100)));
        }
        QVERIFY(hi - lo > 40);
    }

    void subtractTextureFillsWithPressure()
    {
        BrushSettings s = roundBrush(30);
        s.texture = QStringLiteral("paper");
        s.textureStrength = 0.7;
        s.textureMode = TextureMode::Subtract;
        s.pressureOpacity = {true, 0.0, 1.0};
        auto emptyShare = [&](qreal pressure) {
            QImage image = transparent(300, 60);
            StrokeEngine::render(image, s, Qt::black, line({{20, 30}, {280, 30}}, 8, pressure));
            int empty = 0, total = 0;
            for (int y = 25; y <= 35; ++y)
                for (int x = 60; x <= 240; ++x, ++total)
                    empty += qAlpha(image.pixel(x, y)) == 0;
            return double(empty) / total;
        };
        const double light = emptyShare(0.35), heavy = emptyShare(1.0);
        QVERIFY2(light > 0.2 && light < 0.98, qPrintable(QString::number(light)));
        QVERIFY2(heavy < light / 2, qPrintable(QString::number(heavy)));
    }

    // --- erase, smudge, selection ---
    void eraseNearTransparencyAndEdges()
    {
        QImage image = transparent(100, 100);
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x) {
                const int a = (x * 255) / 99;
                image.setPixel(x, y, qPremultiply(qRgba(30, 120, 200, a)));
            }
        const QImage original = image.copy();
        BrushSettings s = roundBrush(30, 0.7);
        s.blend = BrushBlend::Erase;
        s.hardness = 0.3;
        StrokeEngine::render(image, s, Qt::black, line({{-10, 0}, {100, 100}, {0, 100}}));
        bool lowered = false;
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x) {
                QVERIFY(qAlpha(image.pixel(x, y)) <= qAlpha(original.pixel(x, y)));
                lowered |= qAlpha(image.pixel(x, y)) < qAlpha(original.pixel(x, y));
            }
        QVERIFY(lowered);
        QVERIFY(premultipliedValid(image));
    }

    void smudgeCarriesColour()
    {
        BrushSettings s = roundBrush(20);
        s.blend = BrushBlend::Smudge;
        s.smudgeStrength = 0.8;
        s.smudgePickup = 0.2;
        s.spacing = 0.1;
        QImage image = filled(200, 100, QColor(0, 0, 255));
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x)
                image.setPixel(x, y, qRgb(255, 0, 0));
        StrokeEngine::render(image, s, Qt::black, line({{60, 50}, {150, 50}}));
        QVERIFY(qRed(image.pixel(110, 50)) > 60);  // red dragged into blue
        QVERIFY(qBlue(image.pixel(110, 50)) > 30); // mixed, not replaced
        QCOMPARE(image.pixel(110, 90), qRgb(0, 0, 255));
        QVERIFY(premultipliedValid(image));
    }

    void smudgeTransparencyRules()
    {
        BrushSettings s = roundBrush(20);
        s.blend = BrushBlend::Smudge;
        s.smudgeStrength = 0.8;
        s.smudgePickup = 0.2;
        s.spacing = 0.1;

        // Paint dragged into transparency keeps its own colour; transparency adds none.
        QImage out = transparent(200, 100);
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x)
                out.setPixel(x, y, qRgb(200, 0, 0));
        StrokeEngine::render(out, s, Qt::black, line({{60, 50}, {150, 50}}));
        const QRgb spread = out.pixel(112, 50);
        QVERIFY(qAlpha(spread) > 20);
        QCOMPARE(qGreen(spread), 0);
        QCOMPARE(qBlue(spread), 0);
        QVERIFY(std::abs(qRed(spread) - qAlpha(spread) * 200 / 255) <= 2);

        // Transparency dragged into paint thins it.
        QImage in = transparent(200, 100);
        for (int y = 0; y < 100; ++y)
            for (int x = 100; x < 200; ++x)
                in.setPixel(x, y, qRgb(200, 0, 0));
        StrokeEngine::render(in, s, Qt::black, line({{60, 50}, {150, 50}}));
        QVERIFY(qAlpha(in.pixel(110, 50)) < 200);
        QVERIFY(premultipliedValid(in));

        // At the layer edge nothing comes in from outside: a flat colour stays exactly flat.
        QImage edge = filled(100, 100, QColor(10, 150, 90));
        const QImage flat = edge.copy();
        StrokeEngine::render(edge, s, Qt::black, line({{0, 0}, {0, 99}, {99, 99}}));
        QCOMPARE(edge, flat);

        // A click without movement changes nothing.
        QImage click = out.copy();
        StrokeEngine::render(click, s, Qt::black, {at(100, 50)});
        QCOMPARE(click, out);
    }

    void clipMaskLimitsTheStroke()
    {
        QImage mask(200, 60, QImage::Format_Grayscale8);
        mask.fill(0);
        for (int y = 0; y < 60; ++y)
            for (int x = 0; x < 100; ++x)
                mask.scanLine(y)[x] = x < 50 ? 255 : 128;
        QImage image = transparent(200, 60);
        TileSnapshot before;
        StrokeEngine engine(roundBrush(20), Qt::black, 1);
        engine.setClipMask(mask);
        engine.begin(image, at(10, 30), before);
        engine.moveTo(image, at(190, 30), before);
        engine.finish(image, before);
        QCOMPARE(qAlpha(image.pixel(30, 30)), 255);
        QVERIFY(std::abs(qAlpha(image.pixel(75, 30)) - 128) <= 1);
        for (int x = 100; x < 200; ++x)
            QCOMPARE(qAlpha(image.pixel(x, 30)), 0);
    }

    // --- stroke life cycle ---
    void deterministicReplay()
    {
        BrushSettings s = roundBrush(26, 0.9);
        s.tip = QStringLiteral("bristle");
        s.followDirection = true;
        s.angleJitter = 25;
        s.accumulation = Accumulation::BuildUp;
        s.flow = 0.5;
        s.texture = QStringLiteral("canvas");
        s.textureStrength = 0.4;
        std::vector<StrokeSample> curve;
        for (int i = 0; i <= 80; ++i)
            curve.push_back(at(100 + 70 * std::cos(i * 0.1), 100 + 60 * std::sin(i * 0.13), 0.5 + 0.5 * std::sin(i * 0.2)));
        QImage a = transparent(200, 200), b = transparent(200, 200), c = transparent(200, 200);
        StrokeEngine::render(a, s, QColor(40, 80, 20), curve, 7);
        StrokeEngine::render(b, s, QColor(40, 80, 20), curve, 7);
        StrokeEngine::render(c, s, QColor(40, 80, 20), curve, 8);
        QCOMPARE(a, b);
        QVERIFY(a != c);
    }

    void cancelRestoresExactly()
    {
        QImage image = filled(300, 300, QColor(10, 200, 30));
        const QImage original = image.copy();
        BrushSettings s = roundBrush(30, 0.35);
        s.accumulation = Accumulation::BuildUp;
        s.flow = 0.6;
        TileSnapshot before;
        StrokeEngine::render(image, s, Qt::black, line({{60, 60}, {70, 250}, {250, 128}}, 9), 1, &before);
        QVERIFY(image != original);
        before.swapWith(image);
        QCOMPARE(image, original);
    }

    void buffersStayBounded()
    {
        QImage image = transparent(2048, 2048);
        TileSnapshot before;
        StrokeEngine engine(roundBrush(12), Qt::black, 1);
        engine.begin(image, at(5, 5), before);
        for (int i = 1; i <= 200; ++i)
            engine.moveTo(image, at(5 + i * 10, 5 + i * 10), before);
        engine.finish(image, before);
        // Only tiles along the diagonal: never the whole canvas.
        QVERIFY2(engine.coverageTileCount() <= 100, qPrintable(QString::number(engine.coverageTileCount())));
        QCOMPARE(engine.bufferBytes(), qsizetype(engine.coverageTileCount()) * 64 * 64 * 2);
        QVERIFY(before.tileCount() <= 100);
    }

    // --- artefacts the brush sheet is inspected for ---
    void noSeamsAtCoverageTileBorders()
    {
        // The same soft stroke at two alignments to the 64-pixel tiles: only the 8-bit dither
        // (which is tied to the pixel grid) may differ, by at most one level.
        BrushSettings s = roundBrush(70, 0.8);
        s.hardness = 0.2;
        QImage a = transparent(400, 300), b = transparent(400, 300);
        StrokeEngine::render(a, s, Qt::black, line({{40, 60}, {300, 200}, {120, 240}}, 9));
        StrokeEngine::render(b, s, Qt::black, line({{72, 92}, {332, 232}, {152, 272}}, 9));
        int worst = 0;
        for (int y = 0; y < 268; ++y)
            for (int x = 0; x < 368; ++x)
                worst = std::max(worst, std::abs(qAlpha(a.pixel(x, y)) - qAlpha(b.pixel(x + 32, y + 32))));
        QVERIFY2(worst <= 1, qPrintable(QString::number(worst)));
    }

    void noHalosOnTransparency()
    {
        // Paint on a transparent layer keeps its own colour at every coverage: no dark or light
        // fringes when the layer is composited over anything.
        // Milestone 11 procedural presets vary their colour on purpose: per-element variation is
        // turned off here, and grass blades must lie on their own shade line (root 0.55 × colour to
        // tip 1 × colour), so a fringe still fails.
        for (const BrushPreset &p : BrushPresets::builtIns()) {
            if (p.settings.blend != BrushBlend::Paint)
                continue;
            BrushSettings settings = p.settings;
            settings.procColorJitter = 0;
            const bool grass = settings.stamp == BrushStamp::Grass;
            for (const QColor &c : {QColor(255, 255, 255), QColor(200, 30, 30), QColor(0, 0, 0)}) {
                QImage image = transparent(260, 120);
                StrokeEngine::render(image, settings, c, line({{30, 60}, {130, 40}, {230, 70}}, 10, 0.7));
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        const QRgb px = image.pixel(x, y);
                        if (qAlpha(px) < 16)
                            continue;
                        const QRgb straight = qUnpremultiply(px);
                        double k = 1.0;
                        const double cc = c.red() * c.red() + c.green() * c.green() + c.blue() * c.blue();
                        if (grass && cc > 0)
                            k = std::clamp((qRed(straight) * c.red() + qGreen(straight) * c.green() + qBlue(straight) * c.blue()) / cc,
                                           0.55, 1.0);
                        const int d = int(std::lround(std::max({std::abs(qRed(straight) - k * c.red()), std::abs(qGreen(straight) - k * c.green()),
                                                                std::abs(qBlue(straight) - k * c.blue())})));
                        QVERIFY2(d <= 8, qPrintable(QString("%1 at %2,%3: %4").arg(p.id).arg(x).arg(y).arg(d)));
                    }
            }
        }
    }

    void noStampBandsAlongAStroke()
    {
        // Along the centre of a straight stroke, coverage is even: no periodic dab pattern.
        for (const char *id : {"builtin.soft-airbrush", "builtin.solid-round", "builtin.clean-ink", "builtin.soft-eraser"}) {
            BrushSettings s = BrushPresets::builtIn(QString::fromLatin1(id))->settings;
            s.airbrush = false; // the event timing of this synthetic line is not what is being tested
            s.blend = BrushBlend::Paint;
            s.size = std::min(s.size, 60.0);
            QImage image = transparent(500, 100);
            StrokeEngine::render(image, s, Qt::black, line({{20, 50}, {480, 50}}, 40));
            int lo = 255, hi = 0;
            for (int x = 120; x <= 380; ++x) {
                lo = std::min(lo, qAlpha(image.pixel(x, 50)));
                hi = std::max(hi, qAlpha(image.pixel(x, 50)));
            }
            QVERIFY2(hi - lo <= 3, qPrintable(QString("%1: %2..%3").arg(id).arg(lo).arg(hi)));
        }
    }

    // --- built-in presets ---
    void builtInPresetsAreValidAndDistinct()
    {
        const auto &presets = BrushPresets::builtIns();
        QVERIFY(presets.size() >= 8);
        QSet<QString> ids, names;
        for (const BrushPreset &p : presets) {
            QVERIFY(p.builtIn);
            QVERIFY(p.id.startsWith("builtin."));
            QVERIFY(!p.settings.name.isEmpty());
            ids.insert(p.id);
            names.insert(p.settings.name);
            QVERIFY2(p.settings == p.settings.sanitized(), qPrintable(p.id)); // stored values are in range
            BrushSettings back;
            QString error;
            QVERIFY(BrushSettings::fromJson(p.settings.toJson(), &back, &error));
            QVERIFY(back == p.settings);
            if (!p.settings.tip.isEmpty())
                QVERIFY(BrushTips::tip(p.settings.tip));
            if (!p.settings.texture.isEmpty())
                QVERIFY(BrushTips::texture(p.settings.texture));
        }
        QCOMPARE(ids.size(), int(presets.size()));
        QCOMPARE(names.size(), int(presets.size()));
        QVERIFY(BrushPresets::builtIn(BrushPresets::defaultPaintId()));
        QCOMPARE(BrushPresets::builtIn(BrushPresets::defaultEraserId())->settings.blend, BrushBlend::Erase);
        // The default paint brush is the milestone 5 brush.
        const BrushSettings &solid = BrushPresets::builtIn(BrushPresets::defaultPaintId())->settings;
        QCOMPARE(solid.size, 12.0);
        QCOMPARE(solid.hardness, 1.0);
        QCOMPARE(solid.accumulation, Accumulation::Uniform);
        QVERIFY(solid.pressureSize.enabled && solid.pressureSize.minimum == 0.1);
    }

    void everyPresetDrawsWithAndWithoutPressure()
    {
        for (const BrushPreset &p : BrushPresets::builtIns()) {
            for (qreal pressure : {0.3, 1.0}) {
                QImage image = p.settings.blend == BrushBlend::Paint ? transparent(300, 160)
                                                                       : filled(300, 160, QColor(200, 40, 40));
                for (int x = 150; x < 300; ++x) // a second colour for the smudge to move
                    for (int y = 0; y < 160; ++y)
                        image.setPixel(x, y, qRgb(30, 60, 220));
                const QImage original = image.copy();
                const QRect changed = StrokeEngine::render(image, p.settings, Qt::black,
                                                           line({{40, 80}, {150, 60}, {260, 90}}, 12, pressure), 3);
                QVERIFY2(!changed.isEmpty() && image != original, qPrintable(p.id));
                QVERIFY2(premultipliedValid(image), qPrintable(p.id));
            }
        }
    }

    // --- preset storage ---
    void presetStoreRoundTrip()
    {
        BrushPresetStore::Data data;
        BrushSettings mine = BrushPresets::builtIn("builtin.clean-ink")->settings;
        mine.name = QStringLiteral("My ink");
        mine.size = 3;
        data.custom.push_back({QStringLiteral("custom.1"), mine, false});
        BrushSettings changed = BrushPresets::builtIn("builtin.sketch-pencil")->settings;
        changed.opacity = 0.5;
        data.overrides[QStringLiteral("builtin.sketch-pencil")] = changed;
        data.favorites = {QStringLiteral("custom.1"), QStringLiteral("builtin.soft-airbrush")};
        data.paintId = QStringLiteral("custom.1");
        data.eraserId = QStringLiteral("builtin.soft-eraser");

        BrushPresetStore::Data back;
        QString error;
        QStringList warnings;
        QVERIFY(BrushPresetStore::decode(BrushPresetStore::encode(data), &back, &error, &warnings));
        QVERIFY(warnings.isEmpty());
        QCOMPARE(int(back.custom.size()), 1);
        QVERIFY(back.custom[0].settings == mine);
        QVERIFY(back.overrides.at(QStringLiteral("builtin.sketch-pencil")) == changed);
        QCOMPARE(back.favorites, data.favorites);
        QCOMPARE(back.paintId, data.paintId);
        QCOMPARE(back.eraserId, data.eraserId);

        QTemporaryDir dir;
        const QString path = dir.filePath("sub/brushes.json");
        QCOMPARE(BrushPresetStore::save(path, data), QString());
        QVERIFY(BrushPresetStore::load(path, &back, &error, &warnings));
        QCOMPARE(back.favorites, data.favorites);
        QVERIFY(BrushPresetStore::load(dir.filePath("absent.json"), &back, &error, &warnings));
        QVERIFY(back.custom.empty());
    }

    void presetStoreValidation()
    {
        BrushPresetStore::Data out;
        QString error;
        QStringList warnings;
        QVERIFY(!BrushPresetStore::decode("not json", &out, &error, &warnings));
        QVERIFY(!BrushPresetStore::decode(R"({"format":"other","version":1})", &out, &error, &warnings));
        error.clear();
        QVERIFY(!BrushPresetStore::decode(R"({"format":"drawapp-brushes","version":3})", &out, &error, &warnings));
        QVERIFY(error.contains("newer"));
        QVERIFY(BrushPresetStore::decode(R"({"format":"drawapp-brushes","version":2})", &out, &error, &warnings));
        QVERIFY(!BrushPresetStore::decode(R"({"format":"drawapp-brushes","version":"1"})", &out, &error, &warnings));

        // One bad entry is skipped; the rest loads.
        const QByteArray mixed = R"({"format":"drawapp-brushes","version":1,
            "custom":[{"id":"custom.1","settings":{"name":"Good","size":20}},
                      {"id":"custom.2","settings":{"size":"huge"}},
                      {"id":"../evil","settings":{}},
                      {"id":"custom.1","settings":{"name":"Again"}}],
            "overrides":{"builtin.nope":{},"builtin.clean-ink":{"size":2,"name":"Renamed"}},
            "favorites":["custom.1","custom.2","builtin.clean-ink",7],
            "selected":{"paint":"custom.9","eraser":"builtin.soft-eraser"}})";
        warnings.clear();
        QVERIFY(BrushPresetStore::decode(mixed, &out, &error, &warnings));
        QCOMPARE(int(out.custom.size()), 1);
        QCOMPARE(out.custom[0].settings.name, QStringLiteral("Good"));
        QCOMPARE(out.custom[0].settings.size, 20.0);
        QCOMPARE(warnings.size(), 4);
        QCOMPARE(out.overrides.size(), size_t(1));
        QCOMPARE(out.overrides.at("builtin.clean-ink").name, QStringLiteral("Clean Ink")); // built-ins keep their names
        QCOMPARE(out.favorites, (QStringList{"custom.1", "builtin.clean-ink"}));
        QVERIFY(out.paintId.isEmpty()); // unknown selection: the caller falls back to the default
        QCOMPARE(out.eraserId, QStringLiteral("builtin.soft-eraser"));
    }

    void dabsOutsideTheLayerAreIgnored()
    {
        QImage image = transparent(50, 50);
        TileSnapshot before;
        QVERIFY(StrokeEngine::render(image, roundBrush(10), Qt::black, {at(-100, -100), at(-200, -50)}, 1, &before).isEmpty());
        QVERIFY(before.isEmpty());
        const QRect corner = StrokeEngine::render(image, roundBrush(10), Qt::black, {at(0, 0)});
        QCOMPARE(corner.topLeft(), QPoint(0, 0));
        QVERIFY(image.rect().contains(corner));
    }
};

QTEST_GUILESS_MAIN(TestBrush)
#include "tst_brush.moc"

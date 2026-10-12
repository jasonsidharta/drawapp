#include "Pressure.h"
#include "RasterDocument.h"
#include "StrokeEngine.h"
#include "StrokeSmoother.h"
#include "StrokeSpacer.h"
#include "TileSnapshot.h"
#include "ViewRenderer.h"
#include "ViewTransform.h"

#include <QPainter>
#include <QtTest>
#include <cmath>
#include <limits>

namespace {

bool near(const QPointF &a, const QPointF &b, qreal eps = 1e-9)
{
    return std::abs(a.x() - b.x()) <= eps && std::abs(a.y() - b.y()) <= eps;
}

std::vector<Dab> strokeDabs(const std::vector<StrokeSample> &samples, qreal size)
{
    StrokeSpacer spacer(size);
    std::vector<Dab> dabs;
    spacer.begin(samples.front(), dabs);
    for (size_t i = 1; i < samples.size(); ++i)
        spacer.moveTo(samples[i], dabs);
    return dabs;
}

std::vector<StrokeSample> smoothed(const std::vector<StrokeSample> &input, qreal radius)
{
    StrokeSmoother smoother(radius);
    std::vector<StrokeSample> out;
    smoother.begin(input.front(), out);
    for (size_t i = 1; i < input.size(); ++i)
        smoother.moveTo(input[i], out);
    smoother.finish(out);
    return out;
}

// The polyline through points, with each segment split into `parts` equal events.
std::vector<StrokeSample> sampled(const std::vector<QPointF> &points, int parts)
{
    std::vector<StrokeSample> out{{points.front(), 1}};
    for (size_t i = 1; i < points.size(); ++i)
        for (int k = 1; k <= parts; ++k)
            out.push_back({points[i - 1] + (points[i] - points[i - 1]) * (qreal(k) / parts), 1});
    return out;
}

// One complete stroke of a solid round brush on image (StrokeEngine); returns its undo snapshot.
TileSnapshot paintStroke(QImage &image, const std::vector<StrokeSample> &input, qreal size, qreal opacity,
                         BrushMode mode = BrushMode::Paint, qreal radius = 0, const QColor &color = Qt::black)
{
    BrushSettings settings;
    settings.size = size;
    settings.opacity = opacity;
    settings.blend = mode == BrushMode::Erase ? BrushBlend::Erase : BrushBlend::Paint;
    StrokeEngine engine(settings, color, 1, radius);
    TileSnapshot before;
    engine.begin(image, input.front(), before);
    for (size_t i = 1; i < input.size(); ++i)
        engine.moveTo(image, input[i], before);
    engine.finish(image, before);
    return before;
}

QImage transparent(QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

qreal distanceToPolyline(const QPointF &p, const std::vector<QPointF> &line)
{
    qreal best = std::numeric_limits<qreal>::max();
    for (size_t i = 1; i < line.size(); ++i) {
        const QPointF a = line[i - 1], b = line[i], ab = b - a;
        const qreal len2 = QPointF::dotProduct(ab, ab);
        const qreal t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
        best = std::min(best, QLineF(p, a + ab * t).length());
    }
    return best;
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

} // namespace

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    // --- view transform ---
    void viewRoundTrip()
    {
        ViewTransform view;
        view.setPan(QPointF(37.5, -12.25));
        for (qreal zoom : {0.05, 0.33, 1.0, 2.0, 7.5, 32.0}) {
            view.setZoomAt(zoom, QPointF(0, 0));
            for (const QPointF doc : {QPointF(0, 0), QPointF(10.5, 20.25), QPointF(2047, 1535)})
                QVERIFY(near(view.viewToDoc(view.docToView(doc)), doc, 1e-9));
        }
    }

    void zoomKeepsAnchorFixed()
    {
        ViewTransform view;
        view.setPan(QPointF(100, 50));
        const QPointF anchor(412.3, 287.9);
        const QPointF docUnderAnchor = view.viewToDoc(anchor);
        for (qreal factor : {1.25, 1.25, 3.0, 0.1, 0.8}) {
            view.zoomByAt(factor, anchor);
            QVERIFY(near(view.docToView(docUnderAnchor), anchor, 1e-9));
        }
    }

    void zoomIsClamped()
    {
        ViewTransform view;
        view.setZoomAt(1000, QPointF());
        QCOMPARE(view.zoom(), ViewTransform::MaxZoom);
        view.setZoomAt(0.0001, QPointF());
        QCOMPARE(view.zoom(), ViewTransform::MinZoom);
        view.setZoomAt(std::numeric_limits<qreal>::quiet_NaN(), QPointF());
        QCOMPARE(view.zoom(), 1.0);
    }

    void fitCentersDocument()
    {
        ViewTransform view;
        view.fit(QSizeF(2048, 1536), QSizeF(1072, 816), 24);
        QCOMPARE(view.zoom(), 0.5);
        const QRectF r = view.docToView(QRectF(0, 0, 2048, 1536));
        QVERIFY(near(r.center(), QPointF(536, 408)));
        view.fit(QSizeF(100, 100), QSizeF(1000, 1000));
        QCOMPARE(view.zoom(), 1.0); // never magnifies to fit
    }

    void panDoesNotChangeZoom()
    {
        ViewTransform view;
        view.setZoomAt(2.0, QPointF());
        const QPointF doc = view.viewToDoc(QPointF(300, 200));
        view.panBy(QPointF(-40, 15));
        QCOMPARE(view.zoom(), 2.0);
        QVERIFY(near(view.docToView(doc), QPointF(260, 215)));
    }

    // --- pressure ---
    void pressurePolicy()
    {
        QCOMPARE(effectivePressure(PointerKind::Mouse, false, 0.0), 1.0);
        QCOMPARE(effectivePressure(PointerKind::Mouse, true, 0.2), 1.0);
        QCOMPARE(effectivePressure(PointerKind::Touch, true, 0.3), 1.0); // touch is never pen pressure
        QCOMPARE(effectivePressure(PointerKind::Pen, false, 0.3), 1.0);
        QCOMPARE(effectivePressure(PointerKind::Pen, true, 0.3), 0.3);
        QCOMPARE(effectivePressure(PointerKind::Pen, true, 1.7), 1.0);
        QCOMPARE(effectivePressure(PointerKind::Pen, true, -0.5), 1.0);
        QCOMPARE(effectivePressure(PointerKind::Pen, true, std::numeric_limits<qreal>::quiet_NaN()), 1.0);
    }

    void pressureScalesDiameter()
    {
        StrokeSpacer spacer(40);
        QCOMPARE(spacer.diameterFor(1.0), 40.0);
        QCOMPARE(spacer.diameterFor(0.0), 40.0 * StrokeSpacer::MinPressureFraction);
        QVERIFY(spacer.diameterFor(0.5) > spacer.diameterFor(0.25));
        QCOMPARE(StrokeSpacer(2).diameterFor(0.0), StrokeSpacer::MinDiameter);
    }

    void pressureResponseCurve()
    {
        PressureResponse linear;
        QCOMPARE(PressureResponse::gammaForFirmness(0), 1.0);
        QCOMPARE(linear.sizeFactor(0.0), 0.1);
        QCOMPARE(linear.sizeFactor(1.0), 1.0);
        QVERIFY(std::abs(linear.sizeFactor(0.5) - 0.55) < 1e-12);
        const PressureResponse soft{0.1, PressureResponse::gammaForFirmness(-1)};
        const PressureResponse firm{0.1, PressureResponse::gammaForFirmness(1)};
        QVERIFY(soft.sizeFactor(0.5) > linear.sizeFactor(0.5));
        QVERIFY(firm.sizeFactor(0.5) < linear.sizeFactor(0.5));
        for (const PressureResponse &r : {linear, soft, firm}) {
            QCOMPARE(r.sizeFactor(0.0), 0.1);
            QCOMPARE(r.sizeFactor(1.0), 1.0);
            for (int i = 1; i <= 100; ++i)
                QVERIFY(r.sizeFactor(i / 100.0) >= r.sizeFactor((i - 1) / 100.0)); // never shrinks with pressure
        }
        const PressureResponse off{1.0, 1.0}; // pressure does not change size
        QCOMPARE(off.sizeFactor(0.0), 1.0);
        QCOMPARE(StrokeSpacer(40, StrokeSpacer::DefaultSpacing, off).diameterFor(0.2), 40.0);
        QCOMPARE(StrokeSpacer(40, StrokeSpacer::DefaultSpacing, firm).diameterFor(1.0), 40.0); // mouse/touch: full size
    }

    // --- smoothing ---
    void smoothingOffPassesInputThrough()
    {
        const std::vector<StrokeSample> input{{QPointF(1, 2), 0.3}, {QPointF(40, 7), 0.5}, {QPointF(41, 90), 0.9}};
        const std::vector<StrokeSample> out = smoothed(input, 0);
        QCOMPARE(out.size(), input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            QVERIFY(near(out[i].pos, input[i].pos));
            QCOMPARE(out[i].pressure, input[i].pressure);
        }
    }

    void smoothingEndsAtPenUpPosition()
    {
        // The brush lags behind, but finish() completes the line to where the pen was lifted.
        const std::vector<StrokeSample> out = smoothed(sampled({{10, 10}, {200, 10}, {200, 150}}, 7), 25);
        QVERIFY(near(out.back().pos, QPointF(200, 150)));
        // A dot (press and release in place) is one sample.
        QCOMPARE(int(smoothed({{QPointF(5, 5), 1}, {QPointF(5, 5), 1}}, 25).size()), 1);
    }

    void smoothingStaysWithinTheStringOfThePath()
    {
        // A sharp 90° turn: the brush cuts the corner but never strays farther than the string
        // length from the drawn path, and the end is exact.
        const std::vector<QPointF> path{{20, 20}, {300, 20}, {300, 300}};
        const qreal radius = 30;
        const std::vector<StrokeSample> out = smoothed(sampled(path, 5), radius);
        for (const StrokeSample &s : out)
            QVERIFY2(distanceToPolyline(s.pos, path) <= radius + 1e-6, qPrintable(QString::number(distanceToPolyline(s.pos, path))));
        QVERIFY(near(out.back().pos, QPointF(300, 300)));
    }

    void smoothingRemovesJitter()
    {
        // A horizontal line drawn with ±3 px zigzag every 6 px.
        std::vector<StrokeSample> input;
        for (int i = 0; i <= 100; ++i)
            input.push_back({QPointF(10 + 6 * i, 100 + (i % 2 ? 3 : -3)), 1});
        const std::vector<StrokeSample> out = smoothed(input, 12);
        qreal worst = 0;
        for (const StrokeSample &s : out)
            if (s.pos.x() > 60 && s.pos.x() < 560)
                worst = std::max(worst, std::abs(s.pos.y() - 100));
        QVERIFY2(worst < 1.0, qPrintable(QString::number(worst))); // raw deviation is 3 px
    }

    void smoothingDoesNotDependOnEventRate()
    {
        // The same path as 3, 30 and 300 events per segment gives (nearly) the same stroke.
        const std::vector<QPointF> path{{30, 200}, {180, 60}, {330, 220}, {420, 90}, {560, 260}};
        QImage coarse = transparent(QSize(600, 320)), medium = coarse, fine = coarse;
        paintStroke(coarse, sampled(path, 3), 10, 1.0, BrushMode::Paint, 20);
        paintStroke(medium, sampled(path, 30), 10, 1.0, BrushMode::Paint, 20);
        paintStroke(fine, sampled(path, 300), 10, 1.0, BrushMode::Paint, 20);
        for (const QImage *other : {&medium, &fine}) {
            int differing = 0, painted = 0;
            for (int y = 0; y < coarse.height(); ++y)
                for (int x = 0; x < coarse.width(); ++x) {
                    const int a = qAlpha(coarse.pixel(x, y)), b = qAlpha(other->pixel(x, y));
                    painted += a > 0;
                    differing += std::abs(a - b) > 64;
                }
            QVERIFY(painted > 3000);
            QVERIFY2(differing < painted / 200, qPrintable(QStringLiteral("%1 of %2").arg(differing).arg(painted)));
        }
    }

    void smoothingKeepsPressureOfThePullingPointer()
    {
        std::vector<StrokeSample> input;
        for (int i = 0; i <= 50; ++i)
            input.push_back({QPointF(10 + 4 * i, 50), i / 50.0});
        const std::vector<StrokeSample> out = smoothed(input, 15);
        for (size_t i = 1; i < out.size(); ++i)
            QVERIFY(out[i].pressure >= out[i - 1].pressure);
        QCOMPARE(out.back().pressure, 1.0);
    }

    // --- uniform stroke opacity ---
    void strokeOpacityIsUniformWithinAStroke()
    {
        // Heavily overlapping dabs and a self-crossing at 50 %: no darker spots inside the stroke.
        QImage image = transparent(QSize(300, 300));
        paintStroke(image, sampled({{40, 40}, {260, 260}, {260, 40}, {40, 260}}, 4), 24, 0.5);
        QCOMPARE(qAlpha(image.pixel(150, 150)), 128); // the crossing
        QCOMPARE(qAlpha(image.pixel(100, 100)), 128);
        QCOMPARE(qAlpha(image.pixel(260, 150)), 128);
        int maxAlpha = 0;
        for (int y = 0; y < 300; ++y)
            for (int x = 0; x < 300; ++x)
                maxAlpha = std::max(maxAlpha, qAlpha(image.pixel(x, y)));
        QCOMPARE(maxAlpha, 128);
        QVERIFY(premultipliedValid(image));
    }

    void separateStrokesBuildUpOpacity()
    {
        QImage image = transparent(QSize(200, 100));
        paintStroke(image, sampled({{20, 50}, {180, 50}}, 10), 20, 0.5);
        paintStroke(image, sampled({{20, 50}, {180, 50}}, 10), 20, 0.5);
        QCOMPARE(qAlpha(image.pixel(100, 50)), 192); // 1 − 0.5 × 0.5, rounded
    }

    void opacityOnAPaintedLayer()
    {
        QImage image(QSize(200, 100), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        paintStroke(image, sampled({{20, 50}, {180, 50}}, 10), 20, 0.5, BrushMode::Paint, 0, Qt::black);
        const QRgb mid = image.pixel(100, 50);
        QCOMPARE(qAlpha(mid), 255);
        QVERIFY(qRed(mid) >= 126 && qRed(mid) <= 128);
        QCOMPARE(image.pixel(100, 10), qRgb(255, 255, 255)); // outside the stroke: untouched

        // A translucent eraser halves the alpha, also uniformly where its dabs overlap.
        QImage red(QSize(200, 100), QImage::Format_ARGB32_Premultiplied);
        red.fill(QColor(200, 0, 0));
        paintStroke(red, sampled({{20, 50}, {180, 50}, {20, 52}}, 10), 20, 0.5, BrushMode::Erase);
        QCOMPARE(qAlpha(red.pixel(100, 50)), 127);
        QCOMPARE(qRed(red.pixel(100, 50)), 100);
        QVERIFY(premultipliedValid(red));
    }

    void fullOpacityCoversCompletely()
    {
        QImage image = transparent(QSize(200, 100));
        paintStroke(image, sampled({{20, 50}, {180, 50}}, 3), 12, 1.0, BrushMode::Paint, 0, QColor(220, 0, 0));
        for (int x = 20; x <= 180; ++x)
            QCOMPARE(image.pixel(x, 50), qRgb(220, 0, 0));
        QCOMPARE(qAlpha(image.pixel(100, 60)), 0);
    }

    void compositorUndoRestoresExactly()
    {
        // Strokes cross tile borders (64 px); swapping the undo snapshot restores every pixel.
        QImage image(QSize(300, 300), QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(10, 200, 30));
        const QImage original = image.copy();
        TileSnapshot before = paintStroke(image, sampled({{60, 60}, {70, 250}, {250, 128}}, 9), 30, 0.35);
        QVERIFY(image != original);
        before.dropUnchanged(image);
        before.swapWith(image);
        QCOMPARE(image, original);
    }

    // --- stroke spacing ---
    void dabsAreEvenlySpaced()
    {
        const std::vector<Dab> dabs = strokeDabs({{QPointF(10, 10), 1}, {QPointF(110, 10), 1}}, 10);
        const qreal step = 10 * StrokeSpacer::DefaultSpacing;
        QCOMPARE(int(dabs.size()), int(std::floor(100 / step)) + 1);
        for (size_t i = 1; i < dabs.size(); ++i)
            QVERIFY(std::abs((dabs[i].center.x() - dabs[i - 1].center.x()) - step) < 1e-9);
    }

    void splittingInputGivesSameDabs()
    {
        const std::vector<Dab> one = strokeDabs({{QPointF(0, 0), 1}, {QPointF(200, 50), 1}}, 16);
        std::vector<StrokeSample> many;
        for (int i = 0; i <= 173; ++i)
            many.push_back({QPointF(200.0 * i / 173, 50.0 * i / 173), 1});
        const std::vector<Dab> split = strokeDabs(many, 16);
        QCOMPARE(split.size(), one.size());
        for (size_t i = 0; i < one.size(); ++i)
            QVERIFY(near(split[i].center, one[i].center, 1e-6));
    }

    void noGapsWithSparseInput()
    {
        // A fast flick: two samples 900 px apart must still produce overlapping dabs.
        const std::vector<Dab> dabs = strokeDabs({{QPointF(0, 0), 1}, {QPointF(900, 0), 1}}, 4);
        for (size_t i = 1; i < dabs.size(); ++i) {
            const qreal gap = dabs[i].center.x() - dabs[i - 1].center.x();
            QVERIFY(gap <= dabs[i].diameter / 2);
        }
    }

    void stationaryInputAddsNoDabs()
    {
        StrokeSpacer spacer(10);
        std::vector<Dab> dabs;
        spacer.begin({QPointF(5, 5), 1}, dabs);
        for (int i = 0; i < 50; ++i)
            spacer.moveTo({QPointF(5, 5), 0.5}, dabs);
        QCOMPARE(int(dabs.size()), 1);
    }

    void pressureInterpolatesAlongSegment()
    {
        const std::vector<Dab> dabs = strokeDabs({{QPointF(0, 0), 0.0}, {QPointF(300, 0), 1.0}}, 20);
        QVERIFY(dabs.size() > 10);
        for (size_t i = 1; i < dabs.size(); ++i)
            QVERIFY(dabs[i].diameter >= dabs[i - 1].diameter);
        QCOMPARE(dabs.front().diameter, 2.0);
    }

    // --- raster operations ---
    void paintSetsPixels()
    {
        RasterDocument doc(QSize(64, 64));
        const QRect dirty = doc.stampDabs({{QPointF(32, 32), 10}}, QColor(255, 0, 0), BrushMode::Paint);
        QCOMPARE(doc.image().pixel(32, 32), qRgba(255, 0, 0, 255));
        QCOMPARE(qAlpha(doc.image().pixel(32, 40)), 0);
        QCOMPARE(qAlpha(doc.image().pixel(0, 0)), 0);
        QVERIFY(dirty.contains(QRect(27, 27, 10, 10)));
        QVERIFY(dirty.width() <= 14);
    }

    void eraseMakesTransparent()
    {
        RasterDocument doc(QSize(64, 64));
        doc.stampDabs({{QPointF(32, 32), 30}}, QColor(0, 0, 255), BrushMode::Paint);
        doc.stampDabs({{QPointF(32, 32), 8}}, QColor(0, 0, 255), BrushMode::Erase);
        QCOMPARE(qAlpha(doc.image().pixel(32, 32)), 0);
        QCOMPARE(doc.image().pixel(32, 44), qRgba(0, 0, 255, 255)); // outside the eraser, untouched
    }

    void dabsOutsideDocumentAreIgnored()
    {
        RasterDocument doc(QSize(32, 32));
        QVERIFY(doc.stampDabs({{QPointF(-100, -100), 10}}, Qt::black, BrushMode::Paint).isEmpty());
        QVERIFY(doc.stampDabs({}, Qt::black, BrushMode::Paint).isEmpty());
        const QRect edge = doc.stampDabs({{QPointF(0, 0), 10}}, Qt::black, BrushMode::Paint);
        QCOMPARE(edge.topLeft(), QPoint(0, 0));
        QVERIFY(doc.bounds().contains(edge));
        QCOMPARE(doc.size(), QSize(32, 32));
    }

    void clearRemovesEverything()
    {
        RasterDocument doc(QSize(40, 30));
        doc.stampDabs({{QPointF(20, 15), 25}}, Qt::black, BrushMode::Paint);
        QCOMPARE(doc.clear(), QRect(0, 0, 40, 30));
        for (int y = 0; y < 30; ++y)
            for (int x = 0; x < 40; ++x)
                QCOMPARE(qAlpha(doc.image().pixel(x, y)), 0);
    }

    void strokeIsContinuousInRaster()
    {
        RasterDocument doc(QSize(400, 200));
        doc.stampDabs(strokeDabs({{QPointF(100, 100), 1}, {QPointF(300, 100), 1}}, 8), Qt::black, BrushMode::Paint);
        for (int x = 100; x <= 300; ++x)
            QVERIFY2(qAlpha(doc.image().pixel(x, 100)) == 255, qPrintable(QString("gap at x=%1").arg(x)));
        QCOMPARE(qAlpha(doc.image().pixel(100, 110)), 0);
    }

    void defaultDocumentSize()
    {
        RasterDocument doc;
        QCOMPARE(doc.size(), QSize(2048, 1536));
        QCOMPARE(doc.image().format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(doc.image().sizeInBytes(), qsizetype(2048) * 1536 * 4);
    }

    // --- view rendering ---
    void renderMapsDocumentPixelsThroughView()
    {
        RasterDocument doc(QSize(100, 100));
        doc.stampDabs({{QPointF(50.5, 50.5), 3}}, QColor(255, 0, 0), BrushMode::Paint);
        ViewTransform view;
        view.setPan(QPointF(20, 10));
        view.setZoomAt(4.0, QPointF(20, 10));
        QImage out(600, 500, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&out);
        ViewRenderer::render(p, doc.image(), view, out.rect());
        p.end();
        const QPoint expected = view.docToView(QPointF(50.5, 50.5)).toPoint();
        QCOMPARE(out.pixel(expected), qRgb(255, 0, 0));
        QCOMPARE(out.pixel(5, 5), ViewRenderer::WorkspaceColor);       // outside the document
        const QRgb paper = out.pixel(view.docToView(QPointF(5, 5)).toPoint());
        QVERIFY(paper == ViewRenderer::CheckerLight || paper == ViewRenderer::CheckerDark); // transparent shows checkerboard
    }

    void renderOnlyTouchesExposedRect()
    {
        RasterDocument doc(QSize(100, 100));
        ViewTransform view;
        QImage out(200, 200, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::magenta);
        QPainter p(&out);
        ViewRenderer::render(p, doc.image(), view, QRect(10, 10, 20, 20));
        p.end();
        QCOMPARE(out.pixel(5, 5), qRgb(255, 0, 255));
        QCOMPARE(out.pixel(40, 40), qRgb(255, 0, 255));
        QVERIFY(out.pixel(15, 15) != qRgb(255, 0, 255));
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"

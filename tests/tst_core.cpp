#include "Pressure.h"
#include "RasterDocument.h"
#include "StrokeSpacer.h"
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

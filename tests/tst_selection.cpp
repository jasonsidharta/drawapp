#include "BrushPresets.h"
#include "LayeredDocument.h"
#include "Selection.h"
#include "StrokeEngine.h"
#include "TileSnapshot.h"
#include "TransformSession.h"
#include "UndoHistory.h"

#include <QPainter>
#include <QtTest>
#include <cmath>

namespace {

const QSize DocSize(400, 300);

QImage shape(const QPainterPath &path, bool antialias = true)
{
    return Selection::rasterize(DocSize, path, antialias);
}

QPainterPath rectPath(const QRectF &r)
{
    QPainterPath p;
    p.addRect(r);
    return p;
}

QPainterPath ellipsePath(const QRectF &r)
{
    QPainterPath p;
    p.addEllipse(r);
    return p;
}

int countAbove(const QImage &mask, int threshold)
{
    int n = 0;
    for (int y = 0; y < mask.height(); ++y)
        for (int x = 0; x < mask.width(); ++x)
            n += mask.constScanLine(y)[x] > threshold;
    return n;
}

double coverageSum(const QImage &mask)
{
    double s = 0;
    for (int y = 0; y < mask.height(); ++y)
        for (int x = 0; x < mask.width(); ++x)
            s += mask.constScanLine(y)[x] / 255.0;
    return s;
}

// A layer with a few shapes: an opaque red square, a soft blue disc and a pencil-like line.
QImage artwork()
{
    QImage img(DocSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(QRect(60, 50, 80, 70), QColor(200, 30, 30));
    QRadialGradient g(QPointF(250, 150), 60);
    g.setColorAt(0, QColor(30, 60, 220, 255));
    g.setColorAt(1, QColor(30, 60, 220, 0));
    p.setBrush(g);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(250, 150), 60, 60);
    p.setPen(QPen(QColor(20, 20, 20), 3));
    p.drawLine(QPointF(40, 250), QPointF(360, 210));
    return img;
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

QImage rendered(const TransformSession &t, TransformSession::Quality q = TransformSession::Quality::Final)
{
    QImage out(DocSize, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    t.render(out, out.rect(), q);
    return out;
}

StrokeSample at(qreal x, qreal y)
{
    StrokeSample s;
    s.pos = QPointF(x, y);
    return s;
}

} // namespace

class TestSelection : public QObject
{
    Q_OBJECT

private slots:
    // --- shapes ---
    void pixelAlignedRectangleIsExact()
    {
        const QImage m = shape(rectPath(QRectF(10, 20, 30, 40)));
        QCOMPARE(countAbove(m, 0), 30 * 40);
        QCOMPARE(countAbove(m, 254), 30 * 40);
        QCOMPARE(int(m.constScanLine(20)[10]), 255);
        QCOMPARE(int(m.constScanLine(19)[10]), 0);
    }

    void ellipseEdgesAreAntialiased()
    {
        const QImage aa = shape(ellipsePath(QRectF(100, 50, 160, 120)));
        const QImage hard = shape(ellipsePath(QRectF(100, 50, 160, 120)), false);
        const double area = M_PI * 80 * 60;
        QVERIFY(std::abs(coverageSum(aa) - area) < area * 0.01);
        const int partial = countAbove(aa, 0) - countAbove(aa, 254);
        QVERIFY2(partial > 200, qPrintable(QString::number(partial))); // soft boundary
        QCOMPARE(countAbove(hard, 0) - countAbove(hard, 254), 0);       // no antialiasing: hard
    }

    void lassoThatCrossesItselfSelectsEverythingEnclosed()
    {
        // A figure eight: both loops are inside (nonzero winding), also where the path turns back.
        QPainterPath p;
        p.moveTo(50, 50);
        p.lineTo(150, 150);
        p.lineTo(150, 50);
        p.lineTo(50, 150);
        p.closeSubpath();
        const QImage m = shape(p);
        QCOMPARE(int(m.constScanLine(100)[70]), 255);
        QCOMPARE(int(m.constScanLine(100)[130]), 255);
        QCOMPARE(int(m.constScanLine(60)[100]), 0); // outside both triangles
        // A loop drawn twice around the same area stays fully selected, not "inverted".
        QPainterPath twice;
        twice.addEllipse(QRectF(200, 50, 100, 100));
        twice.addEllipse(QRectF(210, 60, 80, 80));
        QCOMPARE(int(shape(twice).constScanLine(100)[250]), 255);
    }

    // --- modes, none and empty ---
    void modesCombineCoverage()
    {
        const QImage a = shape(rectPath(QRectF(0, 0, 100, 100)));
        const QImage b = shape(rectPath(QRectF(50, 0, 100, 100)));
        const Selection base = Selection().combined(a, SelectionMode::Replace);
        const Selection add = base.combined(b, SelectionMode::Add);
        QCOMPARE(add.bounds(), QRect(0, 0, 150, 100));
        const Selection sub = base.combined(b, SelectionMode::Subtract);
        QCOMPARE(sub.bounds(), QRect(0, 0, 50, 100));
        const Selection inter = base.combined(b, SelectionMode::Intersect);
        QCOMPARE(inter.bounds(), QRect(50, 0, 50, 100));
        const Selection rep = base.combined(b, SelectionMode::Replace);
        QCOMPARE(rep.bounds(), QRect(50, 0, 100, 100));
    }

    void addingShapesThatShareAnEdgeLeavesNoSeam()
    {
        // Two rectangles meeting at x = 100.5: the boundary column is half covered by each.
        const QImage left = shape(rectPath(QRectF(20, 20, 80.5, 50)));
        const QImage right = shape(rectPath(QRectF(100.5, 20, 80, 50)));
        QVERIFY(left.constScanLine(40)[100] > 100 && left.constScanLine(40)[100] < 155);
        const Selection both = Selection().combined(left, SelectionMode::Replace).combined(right, SelectionMode::Add);
        QVERIFY2(both.coverageAt(100, 40) >= 254, qPrintable(QString::number(both.coverageAt(100, 40))));
    }

    void noSelectionAndEmptySelectionAreDifferent()
    {
        const Selection none;
        QVERIFY(!none.isActive());
        QCOMPARE(none.coverageAt(5, 5), 255); // everything editable
        const QImage a = shape(rectPath(QRectF(10, 10, 50, 50)));
        // Subtracting everything leaves an *empty* selection, not "no selection".
        const Selection empty = Selection().combined(a, SelectionMode::Replace).combined(a, SelectionMode::Subtract);
        QVERIFY(empty.isActive());
        QVERIFY(empty.isEmpty());
        QCOMPARE(empty.coverageAt(20, 20), 0);
        // "None" combines as nothing selected.
        QCOMPARE(none.combined(a, SelectionMode::Add).bounds(), QRect(10, 10, 50, 50));
        QVERIFY(none.combined(a, SelectionMode::Subtract).isEmpty());
        QVERIFY(none.combined(a, SelectionMode::Intersect).isEmpty());
        // A replace with a shape that covers nothing is empty too.
        QVERIFY(none.combined(shape(QPainterPath()), SelectionMode::Replace).isEmpty());
    }

    void invertAndSelectAll()
    {
        const Selection all = Selection::all(DocSize);
        QCOMPARE(all.bounds(), QRect(QPoint(0, 0), DocSize));
        QVERIFY(all.inverted().isEmpty());
        QVERIFY(!Selection().inverted().isActive());
        const Selection soft = Selection().combined(shape(ellipsePath(QRectF(10, 10, 99, 77))), SelectionMode::Replace);
        QVERIFY(soft.inverted().inverted() == soft);
        QCOMPARE(soft.inverted().coverageAt(0, 0), 255);
    }

    void outlineFollowsTheHalfCoverageBoundary()
    {
        const Selection r = Selection().combined(shape(rectPath(QRectF(10, 20, 30, 40))), SelectionMode::Replace);
        QCOMPARE(r.outline().boundingRect(), QRectF(10, 20, 30, 40));
        QVERIFY(Selection().outline().isEmpty());
        QVERIFY(r.combined(shape(rectPath(QRectF(0, 0, 400, 300))), SelectionMode::Subtract).outline().isEmpty());
        const Selection all = Selection::all(DocSize);
        QCOMPARE(all.outline().boundingRect(), QRectF(0, 0, 400, 300));
    }

    void selectionUndoDoesNotMakeTheDocumentUnsaved()
    {
        LayeredDocument doc(DocSize);
        UndoHistory history;
        const quint64 saved = history.stateId();
        const Selection before = doc.selection();
        doc.setSelection(Selection::all(DocSize));
        history.push(UndoCommand::selection(before, "Select All"));
        QCOMPARE(history.stateId(), saved); // selections are not saved: not an unsaved change
        QVERIFY(history.canUndo());
        const UndoCommand::Result r = history.undo(doc);
        QVERIFY(r.selection);
        QVERIFY(!doc.selection().isActive());
        history.redo(doc);
        QVERIFY(doc.selection().isActive());
        // Artwork changes still count, and a selection change after them does not hide them.
        TileSnapshot tiles;
        tiles.capture(doc.pixels(0), QRect(0, 0, 10, 10));
        doc.pixels(0).fill(Qt::red);
        history.push(UndoCommand::pixels(doc.layer(0).id, std::move(tiles), "Fill"));
        const quint64 afterFill = history.stateId();
        QVERIFY(afterFill != saved);
        doc.setSelection(Selection());
        history.push(UndoCommand::selection(Selection::all(DocSize), "Deselect"));
        QCOMPARE(history.stateId(), afterFill);
    }

    void selectionChangesNeverTouchArtwork()
    {
        LayeredDocument doc(DocSize);
        doc.pixels(0) = artwork();
        doc.refreshComposite();
        const QImage pixels = doc.layer(0).image.copy();
        doc.setSelection(Selection::all(DocSize).inverted());
        doc.setSelection(Selection().combined(shape(ellipsePath(QRectF(0, 0, 200, 200))), SelectionMode::Replace));
        QCOMPARE(doc.layer(0).image, pixels);
        QVERIFY(doc.snapshot().layers.size() == 1); // the snapshot (files, recovery) has no selection
    }

    // --- brushes through a selection ---
    void paintAndEraseApplyCoverageOnce()
    {
        // 50 % coverage everywhere in the selection's soft band.
        QImage half(DocSize, QImage::Format_Grayscale8);
        half.fill(128);
        BrushSettings solid = BrushPresets::builtIn("builtin.solid-round")->settings;
        solid.size = 30;
        QImage layer(DocSize, QImage::Format_ARGB32_Premultiplied);
        layer.fill(Qt::transparent);
        TileSnapshot before;
        StrokeEngine engine(solid, Qt::black, 1);
        engine.setClipMask(half);
        engine.begin(layer, at(50, 100), before);
        for (int i = 1; i <= 30; ++i) // back and forth over the same pixels
            engine.moveTo(layer, at(i % 2 ? 350 : 50, 100), before);
        engine.finish(layer, before);
        QCOMPARE(qAlpha(layer.pixel(200, 100)), 128); // not 64, and not darker after many passes

        QImage red(DocSize, QImage::Format_ARGB32_Premultiplied);
        red.fill(QColor(200, 0, 0));
        BrushSettings eraser = BrushPresets::builtIn("builtin.hard-eraser")->settings;
        eraser.size = 30;
        TileSnapshot b2;
        StrokeEngine erase(eraser, Qt::black, 1);
        erase.setClipMask(half);
        erase.begin(red, at(50, 100), b2);
        for (int i = 1; i <= 30; ++i)
            erase.moveTo(red, at(i % 2 ? 350 : 50, 100), b2);
        erase.finish(red, b2);
        QVERIFY(std::abs(qAlpha(red.pixel(200, 100)) - 127) <= 1);
    }

    void smudgeWritesOnlyInsideAndAppliesTheEdgeOnce()
    {
        BrushSettings s = BrushPresets::builtIn("builtin.blend-smudge")->settings;
        s.size = 30;
        s.pressureOpacity.enabled = false;
        // Left half red, right half blue; the selection is the right half, 50 % in a soft band.
        QImage base(DocSize, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(30, 60, 220));
        for (int y = 0; y < base.height(); ++y)
            for (int x = 0; x < 200; ++x)
                base.setPixel(x, y, qRgb(220, 30, 30));
        QImage mask(DocSize, QImage::Format_Grayscale8);
        mask.fill(0);
        for (int y = 0; y < mask.height(); ++y)
            for (int x = 200; x < 400; ++x)
                mask.scanLine(y)[x] = x < 240 ? 128 : 255;

        auto smudged = [&](const QImage *clip) {
            QImage img = base.copy();
            TileSnapshot before;
            StrokeEngine engine(s, Qt::black, 1);
            if (clip)
                engine.setClipMask(*clip);
            engine.begin(img, at(150, 150), before);
            for (int x = 155; x <= 330; x += 5)
                engine.moveTo(img, at(x, 150), before);
            engine.finish(img, before);
            return img;
        };
        const QImage free = smudged(nullptr);
        const QImage clipped = smudged(&mask);
        // Outside the selection nothing changes, although the smudge started there.
        for (int x = 0; x < 200; ++x)
            QCOMPARE(clipped.pixel(x, 150), base.pixel(x, 150));
        // It carried red picked up outside the selection into it.
        QVERIFY(qRed(clipped.pixel(260, 150)) > 60);
        // In the 50 % band: halfway between the original and the unclipped smudge, once.
        for (int x = 205; x < 238; x += 4) {
            const QRgb o = base.pixel(x, 150), f = free.pixel(x, 150), c = clipped.pixel(x, 150);
            QVERIFY2(std::abs(qRed(c) - (qRed(o) + (qRed(f) - qRed(o)) / 2)) <= 2,
                     qPrintable(QString("x %1: %2 %3 %4").arg(x).arg(qRed(o)).arg(qRed(f)).arg(qRed(c))));
        }
        // Fully selected: the same as without a selection.
        QCOMPARE(clipped.pixel(300, 150), free.pixel(300, 150));
        QVERIFY(premultipliedValid(clipped));
    }

    // --- transforms ---
    void transformNeedsSomethingToTransform()
    {
        TransformSession t;
        QImage blank(DocSize, QImage::Format_ARGB32_Premultiplied);
        blank.fill(Qt::transparent);
        QVERIFY(!t.begin(blank, Selection()));
        QVERIFY(!t.begin(artwork(), Selection::all(DocSize).inverted())); // empty selection
        QVERIFY(t.begin(artwork(), Selection()));
        // The layer's content: the square, the disc and the line.
        QVERIFY(t.sourceRect().contains(QRect(60, 50, 80, 70)));
        QVERIFY(t.sourceRect().contains(QRect(190, 90, 120, 120)));
        QVERIFY(t.sourceRect().left() <= 40 && t.sourceRect().right() >= 359);
    }

    void identityAndCancelAreExact()
    {
        const QImage layer = artwork();
        const QImage copy = layer.copy();
        for (bool soft : {false, true}) {
            TransformSession t;
            const Selection sel = soft ? Selection().combined(shape(ellipsePath(QRectF(40, 40, 260, 180))), SelectionMode::Replace)
                                       : Selection();
            QVERIFY(t.begin(layer, sel));
            QVERIFY(t.isIdentity());
            QCOMPARE(rendered(t), layer); // source + base recombine exactly
            QCOMPARE(rendered(t, TransformSession::Quality::Fast), layer);
            t.moveBy(QPointF(37, -12));
            t.setRotation(33);
            t.setSize(120, 90);
            (void)rendered(t);
            t.end(); // cancel
            QCOMPARE(layer, copy); // the live layer was never touched
        }
    }

    void movesAndFlipsStaySharp()
    {
        const QImage layer = artwork();
        TransformSession t;
        QVERIFY(t.begin(layer, Selection()));
        t.moveBy(QPointF(10.4, -4.6)); // snaps to whole pixels
        const QImage moved = rendered(t);
        for (int y = 60; y < 110; y += 7)
            for (int x = 70; x < 130; x += 7)
                QCOMPARE(moved.pixel(x + 10, y - 5), layer.pixel(x, y));
        t.reset();
        t.flipHorizontal();
        const QImage flipped = rendered(t);
        const QRect r = t.sourceRect();
        for (int y = r.top(); y <= r.bottom(); y += 9)
            for (int x = r.left(); x <= r.right(); x += 9)
                QCOMPARE(flipped.pixel(r.left() + r.right() - x, y), layer.pixel(x, y));
        t.flipHorizontal();
        QVERIFY(t.isIdentity());
        QCOMPARE(rendered(t), layer);
    }

    void scalingFromACornerKeepsTheOppositeCorner()
    {
        TransformSession t;
        QVERIFY(t.begin(artwork(), Selection().combined(shape(rectPath(QRectF(60, 50, 80, 70))), SelectionMode::Replace)));
        const QPointF anchor = t.handlePosition(TransformSession::Handle::TopLeft, 0);
        t.dragScaleHandle(TransformSession::Handle::BottomRight, QPointF(60 + 160, 50 + 200), true);
        QVERIFY(std::abs(t.width() / t.height() - 80.0 / 70.0) < 1e-6); // proportional
        // The pointer (160, 200) from the anchor, projected on the diagonal (80, 70).
        const double k = (160.0 * 80 + 200.0 * 70) / (80.0 * 80 + 70.0 * 70);
        QVERIFY(std::abs(t.width() - 80 * k) < 1e-6);
        const QPointF stays = t.handlePosition(TransformSession::Handle::TopLeft, 0);
        QVERIFY(std::abs(stays.x() - anchor.x()) < 1e-9 && std::abs(stays.y() - anchor.y()) < 1e-9);
        t.dragScaleHandle(TransformSession::Handle::Right, QPointF(400, 100), false); // one axis
        QVERIFY(std::abs(t.width() - (400 - 60)) < 1e-6);
        // From the centre (Alt): the centre stays put and both sides grow.
        const QPointF centre = t.center();
        t.dragScaleHandle(TransformSession::Handle::BottomRight, t.handlePosition(TransformSession::Handle::BottomRight, 0) + QPointF(30, 30), true, true);
        QVERIFY(std::abs(t.center().x() - centre.x()) < 1e-6 && std::abs(t.center().y() - centre.y()) < 1e-6);
        // Dragging past the opposite side stops at one pixel instead of flipping.
        t.dragScaleHandle(TransformSession::Handle::Right, QPointF(-500, 100), false);
        QVERIFY(t.width() >= 1.0 - 1e-9 && t.scaleX() > 0);
    }

    void rotationTurnsAroundThePivot()
    {
        TransformSession t;
        QVERIFY(t.begin(artwork(), Selection()));
        const QPointF pivot = t.pivot();
        t.setRotation(90);
        QCOMPARE(t.pivot(), pivot);
        t.setPivot(QPointF(60, 50));
        t.setRotation(180);
        QVERIFY(std::abs(t.pivot().x() - 60) < 1e-6 && std::abs(t.pivot().y() - 50) < 1e-6);
        t.setRotation(-170 - 360 * 3);
        QVERIFY(std::abs(t.rotation() - -170) < 1e-9);
    }

    void transparentEdgesKeepTheirColour()
    {
        // Red on transparency, scaled and rotated: every visible pixel is still red (no dark or
        // coloured fringe from resampling unpremultiplied colour).
        QImage layer(DocSize, QImage::Format_ARGB32_Premultiplied);
        layer.fill(Qt::transparent);
        QPainter(&layer).fillRect(QRect(100, 100, 60, 40), QColor(230, 20, 20));
        TransformSession t;
        QVERIFY(t.begin(layer, Selection()));
        t.setSize(60 * 1.37, 40 * 1.37);
        t.setRotation(23);
        for (auto quality : {TransformSession::Quality::Fast, TransformSession::Quality::Final}) {
            const QImage out = rendered(t, quality);
            QVERIFY(premultipliedValid(out));
            int visible = 0;
            for (int y = 0; y < out.height(); ++y)
                for (int x = 0; x < out.width(); ++x) {
                    const QRgb p = out.pixel(x, y);
                    if (qAlpha(p) < 24)
                        continue;
                    ++visible;
                    const QRgb c = qUnpremultiply(p);
                    QVERIFY2(std::abs(qRed(c) - 230) <= 12 && qGreen(c) <= 32 && qBlue(c) <= 32,
                             qPrintable(QString("q%6 %1,%2: %3 %4 %5 premult %7 %8 %9 %10").arg(x).arg(y).arg(qRed(c)).arg(qGreen(c)).arg(qBlue(c))
                                        .arg(int(quality)).arg(qRed(p)).arg(qGreen(p)).arg(qBlue(p)).arg(qAlpha(p))));
                }
            QVERIFY(visible > 3000);
        }
    }

    void scaleIsClampedAndWorkIsBoundedByTheCanvas()
    {
        TransformSession t;
        QVERIFY(t.begin(artwork(), Selection()));
        t.dragScaleHandle(TransformSession::Handle::BottomRight, QPointF(1e12, 1e12), true);
        QVERIFY(std::abs(t.scaleX()) <= TransformSession::MaxScale + 1e-9);
        t.setSize(std::nan(""), 5);
        t.setRotation(std::numeric_limits<double>::infinity());
        t.moveBy(QPointF(std::nan(""), 0));
        QVERIFY(std::isfinite(t.center().x()) && std::isfinite(t.rotation()));
        QVERIFY(t.changedRect().width() <= DocSize.width() && t.changedRect().height() <= DocSize.height());
        QVERIFY(premultipliedValid(rendered(t)));
        t.setSize(0, 0); // never below a pixel
        QVERIFY(t.width() >= 1 - 1e-9 && t.height() >= 1 - 1e-9);
    }

    void selectionFollowsTheTransformedPixels()
    {
        TransformSession t;
        const Selection sel = Selection().combined(shape(rectPath(QRectF(60, 50, 80, 70))), SelectionMode::Replace);
        QVERIFY(t.begin(artwork(), sel));
        t.moveBy(QPointF(100, 30));
        QCOMPARE(t.transformedSelection().bounds(), QRect(160, 80, 80, 70));
        TransformSession none;
        QVERIFY(none.begin(artwork(), Selection()));
        QVERIFY(!none.transformedSelection().isActive());
    }

    void pixelsMovedOffTheCanvasAreClipped()
    {
        TransformSession t;
        QVERIFY(t.begin(artwork(), Selection().combined(shape(rectPath(QRectF(60, 50, 80, 70))), SelectionMode::Replace)));
        t.moveBy(QPointF(300, 0)); // the square ends at x = 440, beyond the 400 px canvas
        QCOMPARE(t.changedRect().right(), DocSize.width() - 1);
        const QImage out = rendered(t);
        QCOMPARE(qAlpha(out.pixel(395, 80)), 255);
        // Moving back while the preview is open loses nothing: the source is kept whole.
        t.moveBy(QPointF(-300, 0));
        QCOMPARE(rendered(t), artwork());
    }

    // --- multi-layer transforms ---
    void multiLayerTransformKeepsLayersAligned()
    {
        // Ink (a ring) and colour (a fill inside it) with different content bounds, plus a
        // shared registration dot at (200, 150) on both.
        QImage ink(DocSize, QImage::Format_ARGB32_Premultiplied), colour(DocSize, QImage::Format_ARGB32_Premultiplied);
        ink.fill(Qt::transparent);
        colour.fill(Qt::transparent);
        {
            QPainter p(&ink);
            p.setPen(QPen(Qt::black, 4));
            p.drawEllipse(QPointF(200, 150), 80, 70);
            p.fillRect(QRect(199, 149, 2, 2), Qt::black);
            QPainter c(&colour);
            c.fillRect(QRect(150, 110, 100, 80), QColor(240, 200, 170));
            c.fillRect(QRect(199, 149, 2, 2), QColor(255, 0, 0));
        }
        TransformSession t;
        QVERIFY(t.begin({ink, colour}, Selection()));
        QCOMPARE(t.partCount(), 2);
        QVERIFY(t.sourceRect().contains(QRect(118, 78, 164, 144))); // the union of both layers
        t.setSize(t.width() * 1.4, t.height() * 1.4);
        t.setRotation(25);
        t.moveBy(QPointF(30, 10));
        QImage outInk(DocSize, QImage::Format_ARGB32_Premultiplied), outColour(DocSize, QImage::Format_ARGB32_Premultiplied);
        t.render(0, outInk, outInk.rect(), TransformSession::Quality::Final);
        t.render(1, outColour, outColour.rect(), TransformSession::Quality::Final);
        // The registration dot lands at the same place on both layers.
        const QPointF dot = t.mapToDocument(QPointF(200, 150));
        QVERIFY(qAlpha(outInk.pixel(dot.toPoint())) > 100);
        QVERIFY(qRed(outColour.pixel(dot.toPoint())) > 150 && qGreen(outColour.pixel(dot.toPoint())) < 120);
        // renderRegion gives the same pixels as render.
        const QRect area = t.changedRect();
        const QImage region = t.renderRegion(1, area, TransformSession::Quality::Final);
        QCOMPARE(region, outColour.copy(area));
    }

    void multiLayerCachesStayWithinTheSourceRectangle()
    {
        const QSize big(2048, 1536);
        QImage a(big, QImage::Format_ARGB32_Premultiplied), b(big, QImage::Format_ARGB32_Premultiplied);
        a.fill(Qt::red);
        b.fill(Qt::blue);
        QPainterPath r;
        r.addRect(QRectF(100, 100, 200, 100));
        TransformSession t;
        QVERIFY(t.begin({a, b}, Selection().combined(Selection::rasterize(big, r), SelectionMode::Replace)));
        // Per layer: base and source over 200 × 100 plus reductions, never a full-canvas copy.
        QVERIFY2(t.byteSize() < 2 * 200 * 100 * 4 * 3 + 200 * 100 + 4096, qPrintable(QString::number(t.byteSize())));
        QCOMPARE(rendered(t).isNull(), false);
        QVERIFY(!t.begin({a, QImage(QSize(10, 10), QImage::Format_ARGB32_Premultiplied)}, Selection())); // sizes differ
    }

    void multiLayerIdentityIsExactForEveryLayer()
    {
        QImage a = artwork(), b(DocSize, QImage::Format_ARGB32_Premultiplied);
        b.fill(QColor(10, 120, 60, 128));
        const Selection soft = Selection().combined(Selection::rasterize(DocSize, ellipsePath(QRectF(40, 40, 260, 180))), SelectionMode::Replace);
        TransformSession t;
        QVERIFY(t.begin({a, b}, soft));
        QImage outA(DocSize, QImage::Format_ARGB32_Premultiplied), outB(DocSize, QImage::Format_ARGB32_Premultiplied);
        t.render(0, outA, outA.rect(), TransformSession::Quality::Final);
        t.render(1, outB, outB.rect(), TransformSession::Quality::Final);
        QCOMPARE(outA, a);
        QCOMPARE(outB, b);
    }

    void groupUndoIsOneStep()
    {
        LayeredDocument doc(DocSize);
        LayerData second;
        second.id = doc.allocateId();
        second.name = "Two";
        second.image = QImage(DocSize, QImage::Format_ARGB32_Premultiplied);
        second.image.fill(Qt::transparent);
        doc.insertLayer(1, second);
        const QImage a0 = doc.layer(0).image.copy(), b0 = doc.layer(1).image.copy();
        std::vector<UndoCommand> children;
        for (int i = 0; i < 2; ++i) {
            TileSnapshot tiles;
            tiles.capture(doc.pixels(i), QRect(0, 0, 100, 100));
            doc.pixels(i).fill(i ? Qt::blue : Qt::red);
            children.push_back(UndoCommand::pixels(doc.layer(i).id, std::move(tiles), "Fill"));
        }
        const Selection before = doc.selection();
        doc.setSelection(Selection::all(DocSize));
        UndoHistory history;
        history.push(UndoCommand::group(std::move(children), "Transform").withSelection(before));
        QCOMPARE(history.undoCount(), 1);
        const QImage a1 = doc.layer(0).image.copy(), b1 = doc.layer(1).image.copy();
        const UndoCommand::Result r = history.undo(doc);
        QVERIFY(r.selection);
        QCOMPARE(doc.layer(0).image.copy(0, 0, 100, 100), a0.copy(0, 0, 100, 100));
        QCOMPARE(doc.layer(1).image.copy(0, 0, 100, 100), b0.copy(0, 0, 100, 100));
        QVERIFY(!doc.selection().isActive());
        history.redo(doc);
        QCOMPARE(doc.layer(0).image, a1);
        QCOMPARE(doc.layer(1).image, b1);
        QVERIFY(doc.selection().isActive());
    }

    void handlesAreHitWhereTheyAreDrawn()
    {
        TransformSession t;
        QVERIFY(t.begin(artwork(), Selection().combined(shape(rectPath(QRectF(100, 100, 100, 80))), SelectionMode::Replace)));
        using H = TransformSession::Handle;
        QCOMPARE(t.hitTest(QPointF(101, 99), 8, 30), H::TopLeft);
        QCOMPARE(t.hitTest(QPointF(150, 181), 8, 30), H::Bottom);
        QCOMPARE(t.hitTest(QPointF(150, 70), 8, 30), H::Rotate); // the knob above the top edge
        QCOMPARE(t.hitTest(QPointF(150, 140), 8, 30), H::Pivot);
        QCOMPARE(t.hitTest(QPointF(154, 140), 8, 30), H::Move); // the pivot's target is small
        QCOMPARE(t.hitTest(QPointF(120, 160), 8, 30), H::Move);
        QCOMPARE(t.hitTest(QPointF(350, 250), 8, 30), H::Rotate); // outside rotates
    }
};

QTEST_GUILESS_MAIN(TestSelection)
#include "tst_selection.moc"

// End-to-end checks of CanvasItem in a real, rendered window. Input is synthetic: mouse and
// touch through QTest, pen through constructed QTabletEvents. Pen hardware is not involved.

#include "CanvasItem.h"
#include "ViewRenderer.h"

#include <QDir>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QTabletEvent>
#include <QtTest>
#include <memory>

class TestCanvas : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QQuickWindow> m_window;
    CanvasItem *m_canvas = nullptr;
    std::unique_ptr<QPointingDevice> m_pen;
    std::unique_ptr<QPointingDevice> m_penNoPressure;
    std::unique_ptr<QPointingDevice> m_eraserTip;
    QPointingDevice *m_touch = nullptr;

    const QImage &doc() const { return m_canvas->document().image(); }
    QPointF toView(const QPointF &docPt) const { return m_canvas->viewTransform().docToView(docPt); }
    QPointF toDoc(const QPointF &viewPt) const { return m_canvas->viewTransform().viewToDoc(viewPt); }
    int alphaAt(const QPointF &docPt) const { return qAlpha(doc().pixel(docPt.toPoint())); }

    // Opaque run length through docPt along the y axis.
    int verticalExtent(const QPointF &docPt) const
    {
        const int x = int(docPt.x());
        if (qAlpha(doc().pixel(x, int(docPt.y()))) <= 127)
            return 0;
        int top = int(docPt.y()), bottom = int(docPt.y());
        while (top > 0 && qAlpha(doc().pixel(x, top - 1)) > 127) --top;
        while (bottom < doc().height() - 1 && qAlpha(doc().pixel(x, bottom + 1)) > 127) ++bottom;
        return bottom - top + 1;
    }

    void mouseStroke(const QList<QPoint> &points, Qt::MouseButton button = Qt::LeftButton)
    {
        QTest::mousePress(m_window.get(), button, Qt::NoModifier, points.first());
        for (qsizetype i = 1; i < points.size(); ++i)
            QTest::mouseMove(m_window.get(), points[i]);
        QTest::mouseRelease(m_window.get(), button, Qt::NoModifier, points.last());
    }

    void sendTablet(QEvent::Type type, const QPointingDevice *device, const QPointF &pos, qreal pressure,
                    Qt::MouseButton button = Qt::LeftButton)
    {
        const Qt::MouseButtons buttons = type == QEvent::TabletRelease ? Qt::NoButton : Qt::MouseButtons(button);
        QTabletEvent event(type, device, pos, m_window->mapToGlobal(pos), pressure, 0, 0, 0, 0, 0,
                           Qt::NoModifier, button, buttons);
        QCoreApplication::sendEvent(m_window.get(), &event);
    }

    void sendWheel(const QPointF &pos, int angleY)
    {
        QWheelEvent event(pos, m_window->mapToGlobal(pos), QPoint(), QPoint(0, angleY), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(m_window.get(), &event);
    }

    QImage screenshot(const QString &name)
    {
        QCoreApplication::processEvents();
        const QImage shot = m_window->grabWindow();
        shot.save(QDir::current().filePath(QStringLiteral("tst_canvas_%1.png").arg(name)));
        return shot;
    }

private slots:
    void initTestCase()
    {
        m_pen = std::make_unique<QPointingDevice>(
            "synthetic pen", 1001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        m_penNoPressure = std::make_unique<QPointingDevice>(
            "synthetic pen without pressure", 1002, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position, 1, 3);
        m_eraserTip = std::make_unique<QPointingDevice>(
            "synthetic pen eraser", 1003, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Eraser,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        m_touch = QTest::createTouchDevice();
    }

    void init()
    {
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(900, 640);
        m_canvas = new CanvasItem(m_window->contentItem());
        m_canvas->setSize(m_window->size());
        connect(m_window.get(), &QWindow::widthChanged, m_canvas, [this](int w) { m_canvas->setWidth(w); });
        connect(m_window.get(), &QWindow::heightChanged, m_canvas, [this](int h) { m_canvas->setHeight(h); });
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        m_canvas->zoomToActualSize();
    }

    void cleanup()
    {
        m_window.reset();
        m_canvas = nullptr;
    }

    void mouseStrokeIsContinuous()
    {
        m_canvas->fitToView(); // zoom < 1: each view pixel spans several document pixels
        QVERIFY(m_canvas->zoom() < 1.0);
        // Deliberately sparse input: three move events across 400 view pixels.
        const QList<QPoint> pts{{250, 300}, {380, 305}, {520, 300}, {650, 300}};
        mouseStroke(pts);
        // Check every document pixel along each input segment.
        for (qsizetype s = 1; s < pts.size(); ++s) {
            const QPointF p0 = toDoc(QPointF(pts[s - 1])), p1 = toDoc(QPointF(pts[s]));
            const int steps = int(std::ceil(QLineF(p0, p1).length()));
            for (int i = 0; i <= steps; ++i)
                QVERIFY2(alphaAt(p0 + (p1 - p0) * (qreal(i) / steps)) == 255, "gap in stroke");
        }
        // Mouse has no pressure: the mark is the full brush size (12 px) wide.
        const int extent = verticalExtent(toDoc(QPointF(450, 302.5)));
        QVERIFY2(extent >= 11 && extent <= 14, qPrintable(QString::number(extent)));
        screenshot("mouse_stroke");
    }

    void sizeAndColorAffectNextStroke()
    {
        mouseStroke({{100, 100}, {300, 100}});
        m_canvas->setBrushSize(40);
        m_canvas->setBrushColor(QColor(220, 0, 0));
        mouseStroke({{100, 300}, {300, 300}});

        const QPointF first = toDoc(QPointF(200, 100)), second = toDoc(QPointF(200, 300));
        QCOMPARE(doc().pixel(first.toPoint()), qRgb(20, 20, 20));
        QCOMPARE(doc().pixel(second.toPoint()), qRgb(220, 0, 0));
        QCOMPARE(doc().pixel((second + QPointF(0, 15)).toPoint()), qRgb(220, 0, 0));
        QCOMPARE(qAlpha(doc().pixel((first + QPointF(0, 15)).toPoint())), 0);
        const int wide = verticalExtent(second);
        QVERIFY2(wide >= 39 && wide <= 42, qPrintable(QString::number(wide)));
    }

    void eraserRevealsTransparency()
    {
        m_canvas->setBrushSize(60);
        mouseStroke({{200, 300}, {600, 300}});
        m_canvas->setTool(CanvasItem::Eraser);
        m_canvas->setBrushSize(20);
        mouseStroke({{400, 200}, {400, 400}});

        QCOMPARE(alphaAt(toDoc(QPointF(400, 300))), 0);
        QCOMPARE(alphaAt(toDoc(QPointF(250, 300))), 255);
        const QImage shot = screenshot("eraser");
        const QRgb seen = shot.pixel(QPoint(400, 300) * shot.devicePixelRatio());
        QVERIFY2(seen == ViewRenderer::CheckerLight || seen == ViewRenderer::CheckerDark,
                 "erased area should show the transparency checkerboard");
        QCOMPARE(shot.pixel(QPoint(250, 300) * shot.devicePixelRatio()), qRgb(20, 20, 20));
    }

    void zoomAndPanPreserveCoordinates()
    {
        mouseStroke({{300, 200}, {500, 200}});
        const QImage before = doc().copy();

        // Wheel zoom: the document point under the cursor stays under the cursor.
        const QPointF cursor(437, 251);
        const QPointF docUnderCursor = toDoc(cursor);
        const qreal zoomBefore = m_canvas->zoom();
        sendWheel(cursor, 240);
        QVERIFY(m_canvas->zoom() > zoomBefore * 1.5);
        QVERIFY(QLineF(toDoc(cursor), docUnderCursor).length() < 1e-6);

        // Middle-drag pan: every document point moves by the drag delta; zoom unchanged.
        const QPointF probe(1000, 700);
        const QPointF probeView = toView(probe);
        const qreal zoomAfterWheel = m_canvas->zoom();
        mouseStroke({{600, 400}, {550, 380}, {480, 350}}, Qt::MiddleButton);
        QCOMPARE(m_canvas->zoom(), zoomAfterWheel);
        QVERIFY(QLineF(toView(probe), probeView + QPointF(-120, -50)).length() < 1e-6);

        // Zoom and pan never touch document pixels.
        QCOMPARE(doc(), before);

        // Drawing after zoom + pan lands where the user sees the cursor.
        m_canvas->setBrushColor(QColor(0, 0, 230));
        mouseStroke({{200, 450}, {260, 450}});
        QCOMPARE(doc().pixel(toDoc(QPointF(230, 450)).toPoint()), qRgb(0, 0, 230));
        const QImage shot = screenshot("zoom_pan");
        QCOMPARE(shot.pixel(QPoint(230, 450) * shot.devicePixelRatio()), qRgb(0, 0, 230));
    }

    void resizePreservesArtwork()
    {
        m_canvas->setBrushSize(30);
        mouseStroke({{150, 150}, {450, 350}});
        const QImage before = doc().copy();
        const QPointF mark = toDoc(QPointF(300, 250));

        for (const QSize size : {QSize(420, 360), QSize(1200, 860), QSize(900, 640)}) {
            m_window->resize(size);
            QTRY_COMPARE(m_canvas->size(), QSizeF(size));
            QCOMPARE(m_canvas->documentSize(), QSize(2048, 1536));
            QCOMPARE(doc(), before);
        }
        const QImage shot = screenshot("after_resize");
        const QPoint seen = toView(mark).toPoint();
        QCOMPARE(shot.pixel(seen * shot.devicePixelRatio()), qRgb(20, 20, 20));
    }

    void clearRemovesArtwork()
    {
        mouseStroke({{100, 100}, {700, 500}});
        QCOMPARE(alphaAt(toDoc(QPointF(400, 300))), 255);
        m_canvas->clearCanvas();
        QImage empty(doc().size(), doc().format());
        empty.fill(Qt::transparent);
        QCOMPARE(doc(), empty);
    }

    void penPressureControlsSize()
    {
        m_canvas->setBrushSize(40);
        sendTablet(QEvent::TabletPress, m_pen.get(), QPointF(100, 300), 0.1);
        for (int i = 1; i <= 20; ++i)
            sendTablet(QEvent::TabletMove, m_pen.get(), QPointF(100 + 30 * i, 300), 0.1 + 0.045 * i);
        sendTablet(QEvent::TabletRelease, m_pen.get(), QPointF(700, 300), 1.0);

        const int thin = verticalExtent(toDoc(QPointF(130, 300)));
        const int thick = verticalExtent(toDoc(QPointF(680, 300)));
        QVERIFY2(thin < 15, qPrintable(QString::number(thin)));
        QVERIFY2(thick > 34, qPrintable(QString::number(thick)));
        QVERIFY(m_canvas->inputStatus().contains("pressure"));
        screenshot("pen_pressure");
    }

    void penWithoutPressureUsesFullSize()
    {
        m_canvas->setBrushSize(30);
        sendTablet(QEvent::TabletPress, m_penNoPressure.get(), QPointF(100, 300), 0.0);
        sendTablet(QEvent::TabletMove, m_penNoPressure.get(), QPointF(500, 300), 0.0);
        sendTablet(QEvent::TabletRelease, m_penNoPressure.get(), QPointF(500, 300), 0.0);
        const int extent = verticalExtent(toDoc(QPointF(300, 300)));
        QVERIFY2(extent >= 29 && extent <= 32, qPrintable(QString::number(extent)));
        QVERIFY(m_canvas->inputStatus().contains("no pressure"));
    }

    void penEraserTipErases()
    {
        m_canvas->setBrushSize(50);
        mouseStroke({{100, 300}, {700, 300}});
        QTest::qWait(450);
        sendTablet(QEvent::TabletPress, m_eraserTip.get(), QPointF(400, 250), 1.0);
        sendTablet(QEvent::TabletMove, m_eraserTip.get(), QPointF(400, 350), 1.0);
        sendTablet(QEvent::TabletRelease, m_eraserTip.get(), QPointF(400, 350), 1.0);
        QCOMPARE(alphaAt(toDoc(QPointF(400, 300))), 0);
        QCOMPARE(m_canvas->tool(), CanvasItem::Brush); // the selected tool is unchanged
    }

    void mouseRightAfterPenIsIgnored()
    {
        sendTablet(QEvent::TabletPress, m_pen.get(), QPointF(100, 100), 0.5);
        sendTablet(QEvent::TabletRelease, m_pen.get(), QPointF(100, 100), 0.5);
        // Stand-in for the OS-synthesized mouse events some platforms send after pen input.
        mouseStroke({{100, 400}, {600, 400}});
        QCOMPARE(alphaAt(toDoc(QPointF(350, 400))), 0);
        QTest::qWait(450);
        mouseStroke({{100, 400}, {600, 400}});
        QCOMPARE(alphaAt(toDoc(QPointF(350, 400))), 255);
    }

    void touchDrawsWithoutPressureAndPinchZooms()
    {
        m_canvas->setBrushSize(20);
        QTest::touchEvent(m_window.get(), m_touch).press(0, QPoint(100, 300));
        QTest::touchEvent(m_window.get(), m_touch).move(0, QPoint(300, 300));
        QTest::touchEvent(m_window.get(), m_touch).move(0, QPoint(500, 300));
        QTest::touchEvent(m_window.get(), m_touch).release(0, QPoint(500, 300));
        QCOMPARE(alphaAt(toDoc(QPointF(400, 300))), 255);
        const int extent = verticalExtent(toDoc(QPointF(400, 300)));
        QVERIFY2(extent >= 19 && extent <= 22, qPrintable(QString::number(extent)));

        const QImage before = doc().copy();
        const qreal zoomBefore = m_canvas->zoom();
        QTest::touchEvent(m_window.get(), m_touch).press(0, QPoint(400, 450)).press(1, QPoint(500, 450));
        QTest::touchEvent(m_window.get(), m_touch).move(0, QPoint(350, 450)).move(1, QPoint(550, 450));
        QTest::touchEvent(m_window.get(), m_touch).release(0, QPoint(350, 450)).release(1, QPoint(550, 450));
        QVERIFY(m_canvas->zoom() > zoomBefore * 1.9);
        QCOMPARE(doc(), before); // the pinch drew nothing
    }
};

QTEST_MAIN(TestCanvas)
#include "tst_canvas.moc"

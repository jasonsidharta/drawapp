// End-to-end checks of CanvasItem in a real, rendered window. Input is synthetic: mouse and
// touch through QTest, pen through constructed QTabletEvents. Pen hardware is not involved.

#include "CanvasItem.h"
#include "ProjectFile.h"
#include "ViewRenderer.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
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

    // --- undo / redo through the canvas ---
    void strokeEraserAndClearUndoRedo()
    {
        const QImage empty = doc().copy();
        mouseStroke({{100, 300}, {600, 300}});
        const QImage painted = doc().copy();
        QVERIFY(m_canvas->canUndo());
        QCOMPARE(m_canvas->undoText(), QStringLiteral("Brush stroke"));

        m_canvas->setTool(CanvasItem::Eraser);
        m_canvas->setBrushSize(30);
        mouseStroke({{350, 200}, {350, 400}});
        const QImage erased = doc().copy();
        QCOMPARE(m_canvas->undoText(), QStringLiteral("Eraser stroke"));

        m_canvas->clearCanvas();
        QCOMPARE(doc(), empty);
        QCOMPARE(m_canvas->history().undoCount(), 3);

        m_canvas->undo();
        QCOMPARE(doc(), erased);
        m_canvas->undo();
        QCOMPARE(doc(), painted);
        m_canvas->undo();
        QCOMPARE(doc(), empty);
        QVERIFY(!m_canvas->canUndo());
        m_canvas->redo();
        QCOMPARE(doc(), painted);
        m_canvas->redo();
        QCOMPARE(doc(), erased);
        m_canvas->redo();
        QCOMPARE(doc(), empty);
        QVERIFY(!m_canvas->canRedo());
        const QImage shot = screenshot("after_redo_clear");
        const QRgb seen = shot.pixel(QPoint(200, 300) * shot.devicePixelRatio());
        QVERIFY(seen == ViewRenderer::CheckerLight || seen == ViewRenderer::CheckerDark);
    }

    void newStrokeAfterUndoDropsRedo()
    {
        mouseStroke({{100, 200}, {300, 200}});
        mouseStroke({{100, 400}, {300, 400}});
        m_canvas->undo();
        QVERIFY(m_canvas->canRedo());
        mouseStroke({{500, 300}, {700, 300}});
        QVERIFY(!m_canvas->canRedo());
        QCOMPARE(m_canvas->history().undoCount(), 2);
        QCOMPARE(alphaAt(toDoc(QPointF(200, 400))), 0);
    }

    void viewChangesAreNotUndoable()
    {
        sendWheel(QPointF(400, 300), 240);
        mouseStroke({{600, 400}, {500, 350}}, Qt::MiddleButton);
        m_window->resize(700, 500);
        QTRY_COMPARE(m_canvas->size(), QSizeF(700, 500));
        QVERIFY(!m_canvas->canUndo());
        QVERIFY(!m_canvas->isModified());
        const qreal zoom = m_canvas->zoom();
        m_canvas->undo(); // nothing to undo: the view stays where it is
        QCOMPARE(m_canvas->zoom(), zoom);
    }

    void interruptedStrokesBecomeOneStep()
    {
        // A one-finger stroke interrupted by a second finger.
        m_canvas->setBrushSize(20);
        QTest::touchEvent(m_window.get(), m_touch).press(0, QPoint(100, 300));
        QTest::touchEvent(m_window.get(), m_touch).move(0, QPoint(300, 300));
        QTest::touchEvent(m_window.get(), m_touch).stationary(0).press(1, QPoint(500, 450));
        QTest::touchEvent(m_window.get(), m_touch).release(0, QPoint(300, 300)).release(1, QPoint(500, 450));
        QCOMPARE(m_canvas->history().undoCount(), 1);
        QCOMPARE(alphaAt(toDoc(QPointF(200, 300))), 255);
        m_canvas->undo();
        QCOMPARE(alphaAt(toDoc(QPointF(200, 300))), 0);

        // Undo pressed while the mouse is still drawing removes the whole stroke and stops it.
        QTest::mousePress(m_window.get(), Qt::LeftButton, Qt::NoModifier, QPoint(100, 500));
        QTest::mouseMove(m_window.get(), QPoint(300, 500));
        m_canvas->undo();
        QCOMPARE(alphaAt(toDoc(QPointF(200, 500))), 0);
        QTest::mouseMove(m_window.get(), QPoint(500, 500));
        QTest::mouseRelease(m_window.get(), Qt::LeftButton, Qt::NoModifier, QPoint(500, 500));
        QCOMPARE(alphaAt(toDoc(QPointF(400, 500))), 0);
        QVERIFY(m_canvas->canRedo());
    }

    void erasingNothingIsNotAnEdit()
    {
        m_canvas->setTool(CanvasItem::Eraser);
        mouseStroke({{100, 100}, {400, 100}});
        m_canvas->clearCanvas(); // already empty
        QVERIFY(!m_canvas->canUndo());
        QVERIFY(!m_canvas->isModified());
    }

    // --- save / open / new ---
    void saveOpenAndUnsavedState()
    {
        QTemporaryDir dir;
        QVERIFY(!m_canvas->isModified());
        QCOMPARE(m_canvas->documentTitle(), QStringLiteral("Untitled"));
        mouseStroke({{100, 300}, {600, 300}});
        QVERIFY(m_canvas->isModified());
        QCOMPARE(m_canvas->saveFile(), QStringLiteral("Choose a file name first (Save As)."));

        // Save As without an extension adds ".drawapp".
        QCOMPARE(m_canvas->saveFileAs(QUrl::fromLocalFile(dir.filePath("first"))), QString());
        const QString path = dir.filePath("first.drawapp");
        QVERIFY(QFile::exists(path));
        QVERIFY(!m_canvas->isModified());
        QCOMPARE(m_canvas->documentTitle(), QStringLiteral("first.drawapp"));

        mouseStroke({{100, 450}, {600, 450}});
        QVERIFY(m_canvas->isModified());
        m_canvas->undo();
        QVERIFY(!m_canvas->isModified()); // back at the saved pixels
        m_canvas->redo();
        QVERIFY(m_canvas->isModified());
        QCOMPARE(m_canvas->saveFile(), QString());
        QVERIFY(!m_canvas->isModified());
        const QImage savedSecond = doc().copy();

        m_canvas->newDocument();
        QCOMPARE(m_canvas->documentTitle(), QStringLiteral("Untitled"));
        QVERIFY(!m_canvas->hasFile());
        QVERIFY(!m_canvas->canUndo());
        QCOMPARE(alphaAt(toDoc(QPointF(300, 300))), 0);

        QCOMPARE(m_canvas->openFile(QUrl::fromLocalFile(path)), QString());
        QCOMPARE(doc(), savedSecond);
        QCOMPARE(m_canvas->documentSize(), QSize(2048, 1536));
        QVERIFY(!m_canvas->isModified());
        QVERIFY(!m_canvas->canUndo()); // history does not survive reopening
    }

    void failedOpenKeepsCurrentDocument()
    {
        QTemporaryDir dir;
        QCOMPARE(m_canvas->saveFileAs(QUrl::fromLocalFile(dir.filePath("current.drawapp"))), QString());
        mouseStroke({{100, 300}, {600, 300}});
        const QImage before = doc().copy();
        const int undoSteps = m_canvas->history().undoCount();

        const QString damaged = dir.filePath("damaged.drawapp");
        {
            QByteArray bytes = ProjectFile::encode(doc());
            bytes.chop(100);
            QFile f(damaged);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        }
        const QString error = m_canvas->openFile(QUrl::fromLocalFile(damaged));
        QVERIFY2(error.contains("damaged"), qPrintable(error));
        QVERIFY(!m_canvas->openFile(QUrl::fromLocalFile(dir.filePath("missing.drawapp"))).isEmpty());

        QCOMPARE(doc(), before);
        QVERIFY(m_canvas->isModified());
        QCOMPARE(m_canvas->history().undoCount(), undoSteps);
        QCOMPARE(m_canvas->documentTitle(), QStringLiteral("current.drawapp"));
    }

    void failedSaveKeepsDocumentUnsaved()
    {
        QTemporaryDir dir;
        mouseStroke({{100, 300}, {600, 300}});
        const QString error = m_canvas->saveFileAs(QUrl::fromLocalFile(dir.filePath("missing/folder/x.drawapp")));
        QVERIFY(!error.isEmpty());
        QVERIFY(m_canvas->isModified());
        QVERIFY(!m_canvas->hasFile());
        QVERIFY(m_canvas->canUndo());
    }

    void openedDocumentKeepsItsSize()
    {
        QTemporaryDir dir;
        RasterDocument small(QSize(300, 200));
        small.stampDabs({{QPointF(150, 100), 50}}, QColor(200, 0, 0), BrushMode::Paint);
        const QString path = dir.filePath("small.drawapp");
        QCOMPARE(ProjectFile::save(small.image(), path), QString());
        QCOMPARE(m_canvas->openFile(QUrl::fromLocalFile(path)), QString());
        QCOMPARE(m_canvas->documentSize(), QSize(300, 200));
        QCOMPARE(doc(), small.image());
        // Drawing still maps through the (refitted) view onto the opened canvas.
        mouseStroke({toView(QPointF(20, 20)).toPoint(), toView(QPointF(80, 20)).toPoint()});
        QCOMPARE(alphaAt(QPointF(50, 20)), 255);
    }

    // --- PNG export ---
    void exportIgnoresViewAndKeepsProjectState()
    {
        QTemporaryDir dir;
        mouseStroke({{100, 300}, {600, 300}});
        m_canvas->setTool(CanvasItem::Eraser);
        mouseStroke({{350, 250}, {350, 350}});
        sendWheel(QPointF(300, 300), 360);
        mouseStroke({{600, 400}, {450, 330}}, Qt::MiddleButton);

        const QString png = dir.filePath("export"); // ".png" gets added
        QCOMPARE(m_canvas->exportPng(QUrl::fromLocalFile(png)), QString());
        QImage exported(png + ".png");
        QVERIFY(!exported.isNull());
        QCOMPARE(exported.size(), QSize(2048, 1536));
        QCOMPARE(exported.convertToFormat(QImage::Format_ARGB32), doc().convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(qAlpha(exported.pixel(0, 0)), 0); // transparency kept, no checkerboard
        QVERIFY(m_canvas->isModified());           // exporting is not saving
        QVERIFY(!m_canvas->hasFile());
    }
};

QTEST_MAIN(TestCanvas)
#include "tst_canvas.moc"

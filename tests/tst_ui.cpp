// Drives the real Main.qml (toolbar, layer panel, rename dialog, notice banner) with synthetic
// mouse and keyboard input. Builds a small illustration with Background, Sketch, Color and
// Line art layers and checks the document after each panel action.

#include "CanvasItem.h"
#include "ProjectFile.h"

#include <QDir>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(DrawAppPlugin)

class TestUi : public QObject
{
    Q_OBJECT

    QQmlApplicationEngine *m_engine = nullptr;
    QQuickWindow *m_window = nullptr;
    CanvasItem *m_canvas = nullptr;

    QQuickItem *find(const QString &name, QQuickItem *root = nullptr)
    {
        if (!root)
            root = m_window->contentItem()->parentItem() ? m_window->contentItem()->parentItem() : m_window->contentItem();
        if (root->objectName() == name && root->isVisible())
            return root;
        for (QQuickItem *child : root->childItems()) {
            if (QQuickItem *hit = find(name, child))
                return hit;
        }
        return nullptr;
    }

    QQuickItem *waitFor(const QString &name)
    {
        QQuickItem *item = nullptr;
        return QTest::qWaitFor([&] { return (item = find(name)) != nullptr; }, 3000) ? item : nullptr;
    }

    QPoint centerOf(QQuickItem *item, qreal fx = 0.5, qreal fy = 0.5)
    {
        return item->mapToScene(QPointF(item->width() * fx, item->height() * fy)).toPoint();
    }

    void click(const QString &name)
    {
        QQuickItem *item = waitFor(name);
        QVERIFY2(item, qPrintable("not found: " + name));
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centerOf(item));
        QTest::qWait(60);
    }

    QPoint docToScene(QPointF doc)
    {
        return m_canvas->mapToScene(m_canvas->viewTransform().docToView(doc)).toPoint();
    }

    void draw(const QList<QPointF> &docPoints)
    {
        QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, docToScene(docPoints.first()));
        for (qsizetype i = 1; i < docPoints.size(); ++i) {
            const QPointF a = docPoints[i - 1], b = docPoints[i];
            for (int s = 1; s <= 8; ++s)
                QTest::mouseMove(m_window, docToScene(a + (b - a) * (s / 8.0)));
        }
        QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, docToScene(docPoints.last()));
    }

    void renameActive(const QString &name)
    {
        click("renameLayerButton");
        QQuickItem *field = waitFor("renameField");
        QVERIFY(field);
        QTRY_VERIFY(field->hasActiveFocus());
        QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
        for (const QChar ch : name)
            QTest::keyClick(m_window, ch.toLatin1());
        QTest::keyClick(m_window, Qt::Key_Return);
        QTRY_COMPARE(activeLayer().name, name);
    }

    const LayerData &activeLayer() const { return m_canvas->document().layer(m_canvas->document().activeIndex()); }
    QStringList namesBottomToTop() const
    {
        QStringList names;
        for (const LayerData &l : m_canvas->document().layers())
            names << l.name;
        return names;
    }

    void snap(const QString &name)
    {
        QTest::qWait(100);
        m_window->grabWindow().save(QDir::current().filePath("tst_ui_" + name + ".png"));
    }

private slots:
    void initTestCase()
    {
        QQuickStyle::setStyle(QStringLiteral("Fusion"));
        m_engine = new QQmlApplicationEngine(this);
        m_engine->loadFromModule("DrawApp", "Main");
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        m_window->resize(1400, 900);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_canvas = qobject_cast<CanvasItem *>(find("canvas"));
        QVERIFY(m_canvas);
        QTRY_VERIFY(m_canvas->width() > 600);
        m_canvas->fitToView();
    }

    void buildIllustrationWithThePanel()
    {
        const QColor paper(245, 236, 214);
        // Background
        renameActive("Background");
        m_canvas->setBrushSize(256);
        m_canvas->setBrushColor(paper);
        for (int y = 150; y <= 1450; y += 180)
            draw({{40, qreal(y)}, {2010, qreal(y)}});

        // Sketch: a light blue house outline, then the opacity slider dragged to about 40 %.
        click("addLayerButton");
        QCOMPARE(m_canvas->layerCount(), 2);
        renameActive("Sketch");
        m_canvas->setBrushSize(6);
        m_canvas->setBrushColor(QColor(90, 150, 230));
        draw({{600, 700}, {1400, 700}, {1400, 1200}, {600, 1200}, {600, 700}});
        draw({{600, 700}, {1000, 400}, {1400, 700}});
        const int stepsBeforeDrag = m_canvas->history().undoCount();
        QQuickItem *slider = waitFor("opacitySlider");
        QVERIFY(slider);
        const qreal left = slider->property("leftPadding").toReal();
        const qreal avail = slider->property("availableWidth").toReal();
        const QPoint start = slider->mapToScene(QPointF(left + avail - 2, slider->height() / 2)).toPoint();
        QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, start);
        for (int i = 1; i <= 12; ++i)
            QTest::mouseMove(m_window, slider->mapToScene(QPointF(left + avail * (1.0 - 0.05 * i), slider->height() / 2)).toPoint());
        QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier,
                            slider->mapToScene(QPointF(left + avail * 0.4, slider->height() / 2)).toPoint());
        QTRY_VERIFY(std::abs(activeLayer().opacity - 0.4) < 0.06);
        QCOMPARE(m_canvas->history().undoCount(), stepsBeforeDrag + 1); // the whole drag is one step

        // Color
        click("addLayerButton");
        renameActive("Color");
        m_canvas->setBrushSize(60);
        m_canvas->setBrushColor(QColor(200, 60, 50));
        for (int y = 520; y <= 680; y += 40)
            draw({{qreal(1000 - (y - 400) * 1.2), qreal(y)}, {qreal(1000 + (y - 400) * 1.2), qreal(y)}});
        m_canvas->setBrushColor(QColor(80, 160, 70));
        draw({{100, 1320}, {1950, 1320}});

        // Line art
        click("addLayerButton");
        renameActive("Line art");
        m_canvas->setBrushSize(10);
        m_canvas->setBrushColor(QColor(25, 25, 25));
        draw({{600, 700}, {1400, 700}, {1400, 1200}, {600, 1200}, {600, 700}});
        draw({{600, 700}, {1000, 400}, {1400, 700}});
        QCOMPARE(namesBottomToTop(), (QStringList{"Background", "Sketch", "Color", "Line art"}));
        snap("illustration_built");

        // Move the sketch to the top with the Up button, then hide it.
        click("layerRow_Sketch");
        QCOMPARE(activeLayer().name, QStringLiteral("Sketch"));
        click("layerUpButton");
        click("layerUpButton");
        QCOMPARE(namesBottomToTop(), (QStringList{"Background", "Color", "Line art", "Sketch"}));
        click("visible_Sketch");
        QVERIFY(!activeLayer().visible);

        // Painting on the hidden sketch is refused with a visible explanation.
        draw({{300, 300}, {500, 300}});
        QQuickItem *banner = waitFor("noticeBanner");
        QVERIFY(banner);
        QVERIFY(find("noticeLabel")->property("text").toString().contains("hidden"));
        snap("hidden_layer_notice");

        // Lock the line art; painting there is refused too.
        click("lock_Line art");
        click("layerRow_Line art");
        QCOMPARE(activeLayer().name, QStringLiteral("Line art"));
        QVERIFY(activeLayer().locked);
        const QImage lineBefore = activeLayer().image;
        draw({{300, 1000}, {500, 1000}});
        QCOMPARE(activeLayer().image, lineBefore);
        QTRY_VERIFY(find("noticeLabel")->property("text").toString().contains("locked"));

        // Erase part of the roof on the Color layer: the background shows through.
        click("layerRow_Color");
        m_canvas->setTool(CanvasItem::Eraser);
        m_canvas->setBrushSize(80);
        const QPoint roof(1000, 600);
        QCOMPARE(m_canvas->document().composite().pixel(roof), qRgb(200, 60, 50));
        draw({{900, 600}, {1100, 600}});
        QCOMPARE(m_canvas->document().composite().pixel(roof), paper.rgb());
        snap("erased_color_reveals_background");

        // Undo and redo from the toolbar.
        click("undoButton");
        QCOMPARE(m_canvas->document().composite().pixel(roof), qRgb(200, 60, 50));
        click("redoButton");
        QCOMPARE(m_canvas->document().composite().pixel(roof), paper.rgb());

        // Duplicate and delete from the panel, undone again.
        click("duplicateLayerButton");
        QCOMPARE(activeLayer().name, QStringLiteral("Color copy"));
        click("deleteLayerButton");
        QCOMPARE(m_canvas->layerCount(), 4);
        click("undoButton");
        click("undoButton");
        QCOMPARE(namesBottomToTop(), (QStringList{"Background", "Color", "Line art", "Sketch"}));
        snap("final_ui");
    }

    void saveReopenAndExport()
    {
        QTemporaryDir dir;
        const DocumentData before = m_canvas->document().snapshot();
        const QImage composite = m_canvas->document().composite().copy();
        QCOMPARE(m_canvas->saveFileAs(QUrl::fromLocalFile(dir.filePath("house.drawapp"))), QString());
        QCOMPARE(m_canvas->exportPng(QUrl::fromLocalFile(dir.filePath("house.png"))), QString());
        m_canvas->newDocument();
        QCOMPARE(m_canvas->layerCount(), 1);
        QCOMPARE(m_canvas->openFile(QUrl::fromLocalFile(dir.filePath("house.drawapp"))), QString());
        QCOMPARE(namesBottomToTop(), (QStringList{"Background", "Color", "Line art", "Sketch"}));
        QCOMPARE(m_canvas->document().composite(), composite);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(m_canvas->document().layer(i).image, before.layers[size_t(i)].image);
        QVERIFY(!m_canvas->document().layer(3).visible);
        QVERIFY(m_canvas->document().layer(2).locked);
        QImage png(dir.filePath("house.png"));
        QCOMPARE(png.convertToFormat(QImage::Format_ARGB32), composite.convertToFormat(QImage::Format_ARGB32));
        // The panel shows the reopened layers (top first).
        QTRY_VERIFY(find("layerRow_Sketch") && find("layerRow_Background"));
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"

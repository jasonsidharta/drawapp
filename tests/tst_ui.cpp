// Drives the real Main.qml (toolbar, layer panel, rename dialog, notice banner) with synthetic
// mouse and keyboard input. Builds a small illustration with Background, Sketch, Color and
// Line art layers and checks the document after each panel action.

#include "BrushPresets.h"
#include "BrushScratchpad.h"
#include "BrushThumbnails.h"
#include "CanvasItem.h"
#include "FakeProvider.h"
#include "ProjectFile.h"
#include "RasterDocument.h"
#include "RecoveryManager.h"
#include "Selection.h"
#include "TransformSession.h"

#include <QDir>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QQmlApplicationEngine>
#include <QSemaphore>
#include <QSignalSpy>
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

    // A visible button (e.g. in a dialog's button box) whose text is exactly text.
    QQuickItem *findButton(const QString &text, QQuickItem *root = nullptr)
    {
        if (!root)
            root = m_window->contentItem()->parentItem() ? m_window->contentItem()->parentItem() : m_window->contentItem();
        if (root->isVisible() && QString::fromLatin1(root->metaObject()->className()).contains("Button")
            && root->property("text").toString() == text)
            return root;
        for (QQuickItem *child : root->childItems()) {
            if (QQuickItem *hit = findButton(text, child))
                return hit;
        }
        return nullptr;
    }

    QQuickItem *waitForButton(const QString &text)
    {
        QQuickItem *item = nullptr;
        return QTest::qWaitFor([&] { return (item = findButton(text)) != nullptr; }, 3000) ? item : nullptr;
    }

    int alphaAtDoc(QPointF doc) const { return qAlpha(m_canvas->document().composite().pixel(doc.toPoint())); }

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

    template <typename Start>
    QString waitFor(Start start, bool isSave)
    {
        QSignalSpy spy(m_canvas, isSave ? &CanvasItem::saveFinished : &CanvasItem::openFinished);
        const QString refused = start();
        if (!refused.isEmpty())
            return refused;
        if (!QTest::qWaitFor([&] { return spy.count() > 0; }, 15000))
            return QStringLiteral("timed out");
        return spy.first().at(0).toBool() ? QString() : spy.first().at(1).toString();
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
        m_engine->addImageProvider(QStringLiteral("brushthumb"), new BrushThumbnails);
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
        QCOMPARE(waitFor([&] { return m_canvas->saveFileAs(QUrl::fromLocalFile(dir.filePath("house.drawapp"))); }, true), QString());
        QSignalSpy exported(m_canvas, &CanvasItem::exportFinished);
        QCOMPARE(m_canvas->exportPng(QUrl::fromLocalFile(dir.filePath("house.png"))), QString());
        QVERIFY(QTest::qWaitFor([&] { return exported.count() == 1; }, 15000));
        QVERIFY(exported.first().at(0).toBool());
        m_canvas->newDocument();
        QCOMPARE(m_canvas->layerCount(), 1);
        QCOMPARE(waitFor([&] { return m_canvas->openFile(QUrl::fromLocalFile(dir.filePath("house.drawapp"))); }, false), QString());
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

    void newWaitsForASuccessfulSave()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("waits.drawapp");
        QCOMPARE(waitFor([&] { return m_canvas->saveFileAs(QUrl::fromLocalFile(path)); }, true), QString());
        const int layers = m_canvas->layerCount();
        m_canvas->setActiveLayer(int(m_canvas->document().layer(1).id)); // Color (visible, unlocked)
        m_canvas->setTool(CanvasItem::Brush);
        draw({{200, 200}, {400, 200}});
        QVERIFY(m_canvas->isModified());

        QSemaphore gate;
        m_canvas->projectIo().setBeforeFinishHookForTesting([&] { gate.acquire(); });
        QTest::keyClick(m_window, Qt::Key_N, Qt::ControlModifier);
        QQuickItem *saveButton = waitForButton("Save");
        QVERIFY(saveButton);
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centerOf(saveButton));
        QTRY_VERIFY(m_canvas->fileBusy());
        QTest::qWait(150);
        QCOMPARE(m_canvas->layerCount(), layers); // New has not happened yet
        QVERIFY(find("fileBusyIndicator"));
        snap("saving_busy");
        gate.release();
        QTRY_COMPARE(m_canvas->layerCount(), 1); // New ran after the save succeeded
        QCOMPARE(m_canvas->documentTitle(), QStringLiteral("Untitled"));
        m_canvas->projectIo().setBeforeFinishHookForTesting({});
        QCOMPARE(ProjectFile::load(path).document.layers.size(), size_t(layers));
    }

    void failedSaveCancelsNew()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("readonly.drawapp");
        QCOMPARE(waitFor([&] { return m_canvas->saveFileAs(QUrl::fromLocalFile(path)); }, true), QString());
        draw({{200, 300}, {400, 300}});
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser));
        QTest::keyClick(m_window, Qt::Key_N, Qt::ControlModifier);
        QQuickItem *saveButton = waitForButton("Save");
        QVERIFY(saveButton);
        QSignalSpy finished(m_canvas, &CanvasItem::saveFinished);
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centerOf(saveButton));
        QVERIFY(QTest::qWaitFor([&] { return finished.count() == 1; }, 10000));
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QVERIFY(waitForButton("OK")); // the error dialog
        snap("save_failed_error");
        QVERIFY(m_canvas->isModified());
        QCOMPARE(alphaAtDoc(QPointF(300, 300)), 255); // the drawing is still there
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centerOf(waitForButton("OK")));
    }

    // --- brush settings and overwrite confirmation (milestone 5) ---

    void brushPanel()
    {
        m_canvas->newDocument();
        m_canvas->setTool(CanvasItem::Brush);
        BrushLibrary *lib = m_canvas->brushes();
        QVERIFY(lib->select(BrushPresets::defaultPaintId()));
        m_canvas->setBrushSize(30);
        const auto restore = qScopeGuard([&] {
            if (QObject *popup = m_window->findChild<QObject *>("brushPopup"))
                QMetaObject::invokeMethod(popup, "close");
            QTRY_VERIFY(!find("brushPanel"));
            lib->select(BrushPresets::defaultPaintId());
            lib->resetCurrent();
            lib->select(QStringLiteral("builtin.clean-ink"));
            lib->resetCurrent();
            if (lib->currentFavorite())
                lib->toggleFavorite(lib->currentId());
            lib->select(BrushPresets::defaultPaintId());
            m_canvas->setTool(CanvasItem::Brush);
        });
        // The active brush is always visible in the toolbar.
        QQuickItem *activeLabel = waitFor("activeBrushLabel");
        QVERIFY(activeLabel);
        QVERIFY(activeLabel->property("text").toString().startsWith("Solid Round"));

        click("brushSettingsButton");
        QQuickItem *panel = waitFor("brushPanel");
        QVERIFY(panel);
        QVERIFY(panel->width() > 600); // two columns on a wide window

        // Picking a preset by tapping its entry; its thumbnail is drawn by the brush engine.
        click("preset_builtin.clean-ink");
        QTRY_COMPARE(lib->currentId(), QStringLiteral("builtin.clean-ink"));
        QTRY_VERIFY(activeLabel->property("text").toString().startsWith("Clean Ink"));
        click("favorite_builtin.clean-ink");
        QTRY_VERIFY(lib->currentFavorite());

        // Tap on the sliders' grooves, as with a finger.
        const auto tapSlider = [&](const QString &name, qreal fraction) {
            QQuickItem *slider = waitFor(name);
            QVERIFY2(slider, qPrintable(name));
            const qreal left = slider->property("leftPadding").toReal();
            const qreal avail = slider->property("availableWidth").toReal();
            QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier,
                              slider->mapToScene(QPointF(left + avail * fraction, slider->height() / 2)).toPoint());
        };
        tapSlider("brushOpacitySlider", 0.4);
        QTRY_VERIFY(std::abs(m_canvas->brushOpacity() - 0.4) < 0.03);
        QVERIFY(lib->currentModified()); // a session edit: the preset itself is unchanged
        QCOMPARE(lib->savedSettings(lib->currentId()).opacity, 1.0);
        tapSlider("smoothingSlider", 0.5);
        QTRY_VERIFY(std::abs(m_canvas->smoothing() - 50) <= 2);
        tapSlider("pressureMinSlider", 0.0);
        QTRY_COMPARE(m_canvas->pressureMinSize(), 0.0);
        click("advancedButton");
        tapSlider("pressureFirmnessSlider", 0.97);
        QTRY_VERIFY(m_canvas->pressureFirmness() >= 0.9); // towards "Firm"

        // The scratchpad draws with the working brush and never touches the document.
        const int steps = m_canvas->history().undoCount();
        QQuickItem *pad = waitFor("brushScratchpad");
        QVERIFY(pad);
        const QPoint a = centerOf(pad, 0.15, 0.5), b = centerOf(pad, 0.85, 0.5);
        QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, a);
        for (int i = 1; i <= 12; ++i)
            QTest::mouseMove(m_window, a + (b - a) * i / 12);
        QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, b);
        const QImage scratch = qobject_cast<BrushScratchpad *>(pad)->layer();
        QVERIFY(qAlpha(scratch.pixel(pad->mapFromScene(centerOf(pad)).toPoint())) > 0);
        QCOMPARE(m_canvas->history().undoCount(), steps);
        snap("brush_panel");

        // A tap outside (on the canvas, left of the panel) closes the panel without painting.
        const QPoint outside = docToScene(QPointF(300, 1300));
        QVERIFY(!panel->contains(panel->mapFromScene(outside)));
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, outside);
        QTRY_VERIFY(!find("brushPanel"));
        QCOMPARE(m_canvas->history().undoCount(), steps);
        QCOMPARE(alphaAtDoc(QPointF(300, 1300)), 0);

        // The next stroke uses the working settings: uniform 40 % (± one slider step).
        draw({{300, 300}, {900, 300}, {600, 320}});
        const int alpha = alphaAtDoc(QPointF(450, 300)); // Clean Ink is 8 px wide
        QVERIFY2(std::abs(alpha - qRound(m_canvas->brushOpacity() * 255)) <= 1, qPrintable(QString::number(alpha)));
        QCOMPARE(m_canvas->history().undoCount(), steps + 1);

        // Choosing an eraser preset switches to the eraser tool; its size is its own.
        click("brushSettingsButton");
        QVERIFY(waitFor("brushPanel"));
        click("advancedButton"); // collapse again, so the buttons below are in view
        click("preset_builtin.soft-eraser");
        QTRY_COMPARE(m_canvas->tool(), CanvasItem::Eraser);
        QCOMPARE(m_canvas->brushSize(), 70);
        click("preset_builtin.clean-ink");
        QTRY_COMPARE(m_canvas->tool(), CanvasItem::Brush);
        QTRY_VERIFY(std::abs(m_canvas->brushOpacity() - 0.4) < 0.03); // the session edit is still there

        // Save as a new custom brush through the name dialog.
        click("saveAsNewButton");
        QQuickItem *field = waitFor("brushNameField");
        QVERIFY(field);
        QTRY_VERIFY(field->hasActiveFocus());
        QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
        for (const QChar ch : QStringLiteral("Light ink"))
            QTest::keyClick(m_window, ch.toLatin1());
        QTest::keyClick(m_window, Qt::Key_Return);
        QTRY_COMPARE(lib->currentName(), QStringLiteral("Light ink"));
        QVERIFY(!lib->currentBuiltIn());
        QVERIFY(std::abs(lib->savedSettings(lib->currentId()).opacity - 0.4) < 0.03);
        QVERIFY(find("preset_" + lib->currentId()));
        snap("brush_panel_custom");
        lib->deleteCurrent();

        // A narrow (phone-sized) window stacks the panel in one scrolling column.
        const QSize wideSize = m_window->size();
        m_window->resize(420, 860);
        QTRY_VERIFY(panel->width() < 420);
        QTRY_VERIFY(find("brushScratchpad"));
        snap("brush_panel_narrow");
        m_window->resize(wideSize);
        QTRY_VERIFY(panel->width() > 600);
    }

    // --- milestone 9: Brush Studio through the real UI ---

    void brushStudio()
    {
        m_canvas->newDocument();
        m_canvas->setTool(CanvasItem::Brush);
        BrushLibrary *lib = m_canvas->brushes();
        BrushStudio *studio = lib->studio();
        QVERIFY(lib->select(QStringLiteral("builtin.clean-ink")));
        lib->resetCurrent();
        const QString inkId = lib->currentId();
        const BrushSettings before = lib->workingSettings(inkId);
        const auto restore = qScopeGuard([&] {
            if (QObject *popup = m_window->findChild<QObject *>("studioPopup"))
                QMetaObject::invokeMethod(popup, "close");
            QTRY_VERIFY(!find("brushStudioPage"));
            for (const QVariant &p : lib->presets())
                if (!p.toMap().value("builtIn").toBool() && lib->select(p.toMap().value("id").toString()))
                    lib->deleteCurrent();
            lib->select(inkId);
            lib->resetCurrent();
            lib->select(BrushPresets::defaultPaintId());
            m_canvas->setTool(CanvasItem::Brush);
        });
        auto typeInto = [&](const QString &name, const QString &text) {
            click(name);
            QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
            for (const QChar ch : text)
                QTest::keyClick(m_window, ch.toLatin1());
            QTest::keyClick(m_window, Qt::Key_Return);
        };

        // Opened from the quick panel; it edits a copy.
        click("brushSettingsButton");
        QVERIFY(waitFor("brushPanel"));
        click("openStudioButton");
        QVERIFY(waitFor("brushStudioPage"));
        QTRY_VERIFY(studio->active());
        QCOMPARE(studio->presetId(), inkId);
        QVERIFY(!find("brushPanel")); // the quick panel closed

        // Categories, numeric entry, and the preview follows every change.
        click("category_Brush Tip");
        QTRY_COMPARE(studio->group(), QStringLiteral("Brush Tip"));
        const QString preview = studio->preview();
        typeInto("field_size", "40");
        QTRY_COMPARE(studio->settings().size, 40.0);
        // Leaving the field keeps the value (whatever the locale's digit grouping).
        click("studioSearch");
        QCOMPARE(studio->settings().size, 40.0);
        QVERIFY(studio->preview() != preview);
        QQuickItem *reset = waitFor("reset_size");
        QVERIFY(reset); // a changed setting offers its reset
        // Search across categories.
        typeInto("studioSearch", "jitter");
        QTRY_VERIFY(studio->propertyIds().contains("sizeJitter") && studio->propertyIds().contains("hueJitter"));
        QVERIFY(!studio->propertyIds().contains("size"));
        typeInto("field_sizeJitter", "50");
        QTRY_COMPARE(studio->settings().sizeJitter, 0.5);
        studio->setSearch(QString());

        // Dynamics: switch on, the sensor curve editor appears; drag the curve's end point down.
        click("category_Pressure and Tilt");
        QTRY_COMPARE(studio->group(), QStringLiteral("Pressure and Tilt"));
        click("switch_dynamics.size");
        QTRY_VERIFY(studio->settings().dynamicsFor(DynamicTarget::Size).enabled);
        QQuickItem *curve = waitFor("curve_dynamics.size_0");
        QVERIFY(curve);
        QVERIFY(QMetaObject::invokeMethod(waitFor("brushStudioPage"), "ensureVisible", Q_ARG(QVariant, QVariant::fromValue(curve))));
        QTest::qWait(50);
        const qreal pad = curve->property("pad").toReal();
        const QPoint end = curve->mapToScene(QPointF(curve->width() - pad, pad)).toPoint();
        const QPoint lower = curve->mapToScene(QPointF(curve->width() - pad, curve->height() * 0.6)).toPoint();
        QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, end);
        for (int i = 1; i <= 8; ++i)
            QTest::mouseMove(m_window, end + (lower - end) * i / 8);
        QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, lower);
        QTRY_VERIFY(studio->settings().dynamicsFor(DynamicTarget::Size).mappings.at(0).curve.points().back().y() < 0.6);

        // The pad paints with the copy, not the document.
        const int steps = m_canvas->history().undoCount();
        if (QQuickItem *pad = waitFor("studioPad")) {
            const QPoint a = centerOf(pad, 0.2, 0.5), b = centerOf(pad, 0.8, 0.5);
            QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, a);
            for (int i = 1; i <= 10; ++i)
                QTest::mouseMove(m_window, a + (b - a) * i / 10);
            QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, b);
            QVERIFY(qAlpha(qobject_cast<BrushScratchpad *>(pad)->layer().pixel(pad->mapFromScene(centerOf(pad)).toPoint())) > 0);
        } else {
            QFAIL("no studio pad");
        }
        QCOMPARE(m_canvas->history().undoCount(), steps);
        snap("brush_studio");

        // Cancel: the preset and the working brush are exactly as before.
        click("studioCancel");
        QTRY_VERIFY(!studio->active());
        QTRY_VERIFY(!find("brushStudioPage"));
        QVERIFY(lib->workingSettings(inkId) == before);
        QVERIFY(lib->savedSettings(inkId) == before);

        // Done saves into the preset.
        lib->select(inkId);
        QVERIFY(studio->open());
        studio->setValue("hardness", 0.3);
        studio->setValue("taperEnd", 60.0);
        QVERIFY(studio->modified());
        QVERIFY(lib->workingSettings(inkId).hardness == before.hardness); // still untouched
        QVERIFY(studio->done().isEmpty());
        QCOMPARE(lib->savedSettings(inkId).hardness, 0.3);
        QCOMPARE(lib->savedSettings(inkId).taperEnd, 60.0);
        QVERIFY(lib->currentHasSavedChanges());

        // Engine switch shows which settings apply; erasers cannot become paint brushes.
        QVERIFY(studio->open());
        studio->setValue("blend", "smudge");
        QCOMPARE(studio->engine(), QStringLiteral("smudge"));
        QVERIFY(studio->propertyRow("smudgeStrength").value("applies").toBool());
        QVERIFY(!studio->propertyRow("paintMode").value("applies").toBool());
        studio->resetAll();
        QCOMPARE(studio->engine(), QStringLiteral("paint"));

        // Duplicate, rename, export, import.
        QVERIFY(studio->duplicate(QStringLiteral("Studio copy")).isEmpty());
        QVERIFY(studio->presetId().startsWith("custom."));
        QVERIFY(studio->rename(QStringLiteral("Studio ink")).isEmpty());
        QTemporaryDir dir;
        const QUrl file = QUrl::fromLocalFile(dir.filePath("ink.drawbrush"));
        QVERIFY(studio->exportTo(file).isEmpty());
        QVERIFY(studio->done().isEmpty());
        QCOMPARE(lib->currentName(), QStringLiteral("Studio ink"));
        QVERIFY(studio->importFrom(file).isEmpty());
        QVERIFY(studio->active());
        QCOMPARE(studio->name(), QStringLiteral("Studio ink"));
        QCOMPARE(studio->settings().hardness, 0.3);
        QVERIFY(studio->presetId() != inkId);
        studio->cancel();
        QCOMPARE(studio->rename(QStringLiteral("x")), QString()); // closed: nothing to do

        // A narrow window shows the category menu instead of the list.
        lib->select(inkId);
        const QSize wideSize = m_window->size();
        m_window->resize(420, 860);
        window()->setProperty("width", 420);
        QVERIFY(QMetaObject::invokeMethod(m_window, "openStudio"));
        QVERIFY(waitFor("brushStudioPage"));
        QTRY_VERIFY(!find("studioCategories"));
        snap("brush_studio_narrow");
        click("studioDone");
        QTRY_VERIFY(!find("brushStudioPage"));
        m_window->resize(wideSize);
    }

private:
    QQuickWindow *window() const { return m_window; }

    // --- milestone 8: the artist tasks through the real UI (helpers, not tests) ---

    // Samples along an ellipse (document coordinates), 8 ms apart, pressure rising and falling.
    static std::vector<StrokeSample> ring(QPointF c, qreal rx, qreal ry, qreal from = 0, qreal to = 2 * M_PI, int n = 90)
    {
        std::vector<StrokeSample> out;
        for (int i = 0; i <= n; ++i) {
            const qreal a = from + (to - from) * i / n;
            StrokeSample s;
            s.pos = c + QPointF(rx * std::cos(a), ry * std::sin(a));
            s.pressure = 0.55 + 0.45 * std::sin(M_PI * i / n);
            s.timeMs = i * 8.0;
            out.push_back(s);
        }
        return out;
    }

    static std::vector<StrokeSample> segment(QPointF a, QPointF b, int n = 30)
    {
        std::vector<StrokeSample> out;
        for (int i = 0; i <= n; ++i) {
            StrokeSample s;
            s.pos = a + (b - a) * (qreal(i) / n);
            s.pressure = 0.6 + 0.4 * std::sin(M_PI * i / n);
            s.timeMs = i * 8.0;
            out.push_back(s);
        }
        return out;
    }

    void paintWith(const char *preset, QColor color, const std::vector<StrokeSample> &samples, int size = 0)
    {
        QVERIFY(m_canvas->brushes()->select(QString::fromLatin1(preset)));
        if (size)
            m_canvas->setBrushSize(size);
        m_canvas->setBrushColor(color);
        QVERIFY(m_canvas->drawStroke(samples));
    }

    // A small character illustration: colour and shading layers under clean ink.
    void drawCharacter()
    {
        m_canvas->newDocument();
        renameActive("Colour");
        paintWith("builtin.solid-round", QColor(245, 214, 186), ring({700, 520}, 70, 60), 230); // face
        paintWith("builtin.solid-round", QColor(70, 110, 170), segment({620, 820}, {780, 820}), 200); // body
        paintWith("builtin.solid-round", QColor(245, 214, 186), ring({1180, 860}, 22, 18), 110); // hand
        paintWith("builtin.textured-paint", QColor(90, 55, 35), ring({700, 430}, 150, 70, M_PI, 2 * M_PI), 60); // hair
        click("addLayerButton");
        renameActive("Ink");
        paintWith("builtin.clean-ink", QColor(25, 25, 35), ring({700, 520}, 180, 170), 9);
        paintWith("builtin.clean-ink", QColor(25, 25, 35), ring({640, 500}, 22, 16), 7);   // left eye
        paintWith("builtin.clean-ink", QColor(25, 25, 35), ring({760, 500}, 22, 16), 7);   // right eye
        paintWith("builtin.solid-round", QColor(25, 25, 35), ring({646, 504}, 6, 6), 10);  // pupils
        paintWith("builtin.solid-round", QColor(25, 25, 35), ring({766, 504}, 6, 6), 10);
        paintWith("builtin.clean-ink", QColor(25, 25, 35), ring({700, 600}, 45, 18, 0.2, M_PI - 0.2), 6); // mouth
        paintWith("builtin.clean-ink", QColor(25, 25, 35), segment({850, 760}, {1120, 840}), 9);       // arm
        paintWith("builtin.clean-ink", QColor(25, 25, 35), ring({1180, 860}, 60, 50), 8);              // hand
        for (int f = 0; f < 4; ++f)
            paintWith("builtin.clean-ink", QColor(25, 25, 35),
                      segment({1200.0 + f * 12, 815.0 - f * 4}, {1235.0 + f * 16, 760.0 - f * 6}, 12), 6); // fingers
        paintWith("builtin.sketch-pencil", QColor(60, 60, 70), segment({560, 640}, {600, 700}), 6);   // pencil shading
        m_canvas->setTool(CanvasItem::Brush);
        m_canvas->fitToView();
    }

    void dragScene(QPoint from, QPoint to, int steps = 8, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::mousePress(m_window, Qt::LeftButton, mods, from);
        for (int i = 1; i <= steps; ++i) {
            const QPoint p = from + (to - from) * i / steps;
            if (mods == Qt::NoModifier) {
                QTest::mouseMove(m_window, p);
            } else {
                // QTest's moves carry no modifiers; a held key is part of every real move event.
                QMouseEvent move(QEvent::MouseMove, p, p, m_window->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, mods);
                QCoreApplication::sendEvent(m_window, &move);
            }
        }
        QTest::mouseRelease(m_window, Qt::LeftButton, mods, to);
    }

    QPoint handleScene(TransformSession::Handle h)
    {
        return docToScene(m_canvas->transformSession().handlePosition(h, 0));
    }

    void typeInto(const QString &field, const QString &value)
    {
        click(field);
        QTest::keyClick(m_window, Qt::Key_End);
        for (int i = 0; i < 10; ++i)
            QTest::keyClick(m_window, Qt::Key_Backspace);
        for (const QChar ch : value)
            QTest::keyClick(m_window, ch.toLatin1());
        QTest::keyClick(m_window, Qt::Key_Return);
        QTest::qWait(30);
    }

private slots:
    void artistTasksWithSelectionsAndTransforms()
    {
        drawCharacter();
        const auto restore = qScopeGuard([&] {
            if (m_canvas->isTransforming())
                m_canvas->cancelTransform();
            m_canvas->deselect();
            m_canvas->setTool(CanvasItem::Brush);
            m_canvas->setSelectMode(CanvasItem::Replace);
            m_canvas->setSelectShape(CanvasItem::RectangleShape);
            m_canvas->brushes()->select(BrushPresets::defaultPaintId());
        });
        snap("m8_0_illustration");
        const int inkLayer = m_canvas->activeLayerId();

        // 1. Lasso the head and enlarge it proportionally (on the ink layer).
        click("selectToolButton");
        click("shapeLasso");
        {
            const auto pts = ring({700, 500}, 215, 205, 0, 2 * M_PI, 40);
            QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, docToScene(pts.front().pos));
            for (const StrokeSample &s : pts)
                QTest::mouseMove(m_window, docToScene(s.pos));
            QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, docToScene(pts.back().pos));
        }
        QVERIFY(m_canvas->hasSelection());
        QCOMPARE(m_canvas->undoText(), QStringLiteral("Select"));
        snap("m8_1_lasso_head");
        click("selectTransformButton");
        QVERIFY(m_canvas->isTransforming());
        const qreal w0 = m_canvas->transformWidth(), h0 = m_canvas->transformHeight();
        // Alt: from the centre, so the head grows in place.
        const QPointF headCentre = m_canvas->transformSession().center();
        dragScene(handleScene(TransformSession::Handle::BottomRight),
                  handleScene(TransformSession::Handle::BottomRight) + QPoint(30, 28), 8, Qt::AltModifier);
        QVERIFY(std::abs(m_canvas->transformSession().center().x() - headCentre.x()) < 1.0);
        QVERIFY2(m_canvas->transformWidth() > w0 * 1.15, qPrintable(QString::number(m_canvas->transformWidth() / w0)));
        QVERIFY(std::abs(m_canvas->transformWidth() / m_canvas->transformHeight() - w0 / h0) < 1e-6); // proportional
        snap("m8_1_head_preview");
        const QImage inkBefore = m_canvas->document().layer(m_canvas->document().indexOf(inkLayer)).image.copy();
        click("applyTransformButton");
        QVERIFY(!m_canvas->isTransforming());
        QCOMPARE(m_canvas->undoText(), QStringLiteral("Transform"));
        QVERIFY(m_canvas->document().layer(m_canvas->document().indexOf(inkLayer)).image != inkBefore);

        // 2. Rotate the hand and move it into position.
        click("shapeRect");
        dragScene(docToScene({1100, 740}), docToScene({1270, 930}));
        click("selectTransformButton");
        typeInto("transformRotationField", "35");
        QCOMPARE(m_canvas->transformRotation(), 35.0);
        QVERIFY(m_canvas->isTransforming()); // Enter in the field confirmed the number, not the transform
        const QPoint inside = docToScene(m_canvas->transformSession().center() + QPointF(20, 0));
        dragScene(inside, inside + QPoint(-30, -25));
        snap("m8_2_hand_rotate_preview");
        QTest::keyClick(m_window, Qt::Key_Return); // Enter applies
        QVERIFY(!m_canvas->isTransforming());
        QTest::qWait(100); // the bar shows the selection buttons again; let it lay out, as a person would
        QCOMPARE(m_canvas->undoText(), QStringLiteral("Transform"));

        // 3. Add and subtract regions, then shade only inside them (on the colour layer).
        m_canvas->setActiveLayer(int(m_canvas->document().layer(0).id));
        click("shapeEllipse");
        click("modeReplace");
        dragScene(docToScene({560, 420}), docToScene({700, 640}));
        click("modeAdd");
        dragScene(docToScene({680, 420}), docToScene({840, 640}));
        click("modeSubtract");
        dragScene(docToScene({660, 470}), docToScene({740, 560}));
        click("modeReplace");
        QCOMPARE(m_canvas->undoText(), QStringLiteral("Subtract from selection"));
        const Selection shadeArea = m_canvas->selection();
        QVERIFY(shadeArea.coverageAt(600, 530) == 255 && shadeArea.coverageAt(800, 530) == 255);
        QCOMPARE(shadeArea.coverageAt(700, 515), 0); // the subtracted middle
        const QImage colourBefore = m_canvas->document().layer(0).image.copy();
        click("brushSettingsButton");
        click("preset_builtin.soft-airbrush");
        QMetaObject::invokeMethod(m_window->findChild<QObject *>("brushPopup"), "close");
        QTRY_VERIFY(!find("brushPanel"));
        m_canvas->setBrushColor(QColor(200, 120, 110));
        click("brushToolButton");
        draw({{520, 520}, {880, 540}, {520, 600}});
        const QImage colourAfter = m_canvas->document().layer(0).image;
        QVERIFY(colourAfter != colourBefore);
        for (int y = 380; y < 700; y += 4)
            for (int x = 480; x < 900; x += 4)
                if (shadeArea.coverageAt(x, y) == 0)
                    QVERIFY2(colourAfter.pixel(x, y) == colourBefore.pixel(x, y), qPrintable(QString("%1,%2").arg(x).arg(y)));
        click("selectToolButton");
        snap("m8_3_shade_inside");

        // 4. Copy an eye to a new layer and flip it.
        m_canvas->setActiveLayer(inkLayer);
        click("shapeEllipse");
        const Selection beforeEye = m_canvas->selection();
        QPointF eye = QPointF(640, 500);
        // The head was enlarged around its centre: find the left eye's ink near where it was.
        dragScene(docToScene(eye - QPointF(55, 45)), docToScene(eye + QPointF(55, 45)));
        QVERIFY(m_canvas->selection() != beforeEye);
        const int layersBefore = m_canvas->layerCount();
        click("copyButton");
        click("pasteButton");
        QCOMPARE(m_canvas->layerCount(), layersBefore + 1);
        QCOMPARE(m_canvas->undoText(), QStringLiteral("Paste"));
        click("selectTransformButton");
        click("flipHButton");
        QVERIFY(m_canvas->transformSession().scaleX() < 0);
        const QPoint eyeCentre = docToScene(m_canvas->transformSession().center());
        dragScene(eyeCentre, eyeCentre + docToScene(QPointF(1300, 0)) - docToScene(QPointF(1100, 0)));
        snap("m8_4_eye_flip_preview");
        click("applyTransformButton");
        QCOMPARE(m_canvas->layerCount(), layersBefore + 1);
        const QImage beforeTask5 = m_canvas->document().composite().copy();

        // 5. Cancel a transform: nothing changes.
        const int steps = m_canvas->history().undoCount();
        click("selectTransformButton");
        QVERIFY(m_canvas->isTransforming());
        dragScene(handleScene(TransformSession::Handle::TopLeft), handleScene(TransformSession::Handle::TopLeft) - QPoint(30, 30));
        QVERIFY(m_canvas->displayedComposite() != beforeTask5);
        click("cancelTransformButton");
        QCOMPARE(m_canvas->document().composite(), beforeTask5);
        QCOMPARE(m_canvas->history().undoCount(), steps);

        // 6. Undo and redo the applied transform.
        QCOMPARE(m_canvas->undoText(), QStringLiteral("Transform"));
        click("undoButton");
        QVERIFY(m_canvas->document().composite() != beforeTask5);
        click("redoButton");
        QCOMPARE(m_canvas->document().composite(), beforeTask5);
        QCOMPARE(m_canvas->layerCount(), layersBefore + 1);
        snap("m8_6_final");

        // 7. Save and reopen.
        QTemporaryDir dir;
        const QString path = dir.filePath("character.drawapp");
        QCOMPARE(waitFor([&] { return m_canvas->saveFileAs(QUrl::fromLocalFile(path)); }, true), QString());
        const QImage saved = m_canvas->document().composite().copy();
        const int layers = m_canvas->layerCount();
        m_canvas->newDocument();
        QCOMPARE(waitFor([&] { return m_canvas->openFile(QUrl::fromLocalFile(path)); }, false), QString());
        QCOMPARE(m_canvas->document().composite(), saved);
        QCOMPARE(m_canvas->layerCount(), layers);
        QVERIFY(!m_canvas->hasSelection()); // selections are not saved
    }

    void overwriteOfAProviderDocumentIsConfirmed()
    {
        FakeProvider provider;
        m_canvas->projectIo().setProviderForTesting(&provider);
        QTemporaryDir staging;
        m_canvas->setStagingDirectory(staging.path());
        const QString uri = QStringLiteral("content://test.documents/document/shared.drawapp");
        const QByteArray oldBytes = ProjectFile::encode(DocumentData::fromImage(RasterDocument(QSize(32, 32)).image()));
        provider.documents.insert(uri, oldBytes);
        m_canvas->newDocument();
        draw({{200, 200}, {800, 200}});

        // Cancel: nothing written, the drawing stays unsaved.
        QSignalSpy finished(m_canvas, &CanvasItem::saveFinished);
        QCOMPARE(m_canvas->saveFileAs(QUrl(uri)), QString());
        QQuickItem *cancel = waitForButton("Cancel");
        QVERIFY(cancel);
        QVERIFY(waitForButton("Overwrite") && waitForButton("Save a Copy…"));
        snap("overwrite_warning");
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centerOf(cancel));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(0).toBool());
        QCOMPARE(provider.contents(uri), oldBytes);
        QVERIFY(m_canvas->isModified());

        // Overwrite: written, checked, saved.
        QCOMPARE(m_canvas->saveFileAs(QUrl(uri)), QString());
        QQuickItem *overwrite = waitForButton("Overwrite");
        QVERIFY(overwrite);
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centerOf(overwrite));
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(finished.last().at(0).toBool());
        QCOMPARE(ProjectFile::decode(provider.contents(uri)).document.composite(), m_canvas->document().composite());
        QVERIFY(!m_canvas->isModified());
        QCOMPARE(m_canvas->documentTitle(), QStringLiteral("shared.drawapp"));

        // Save a Copy: the overwrite is dropped and the Save As dialog opens instead.
        const QString other = QStringLiteral("content://test.documents/document/other.drawapp");
        provider.documents.insert(other, oldBytes);
        draw({{200, 400}, {800, 400}});
        QCOMPARE(m_canvas->saveFileAs(QUrl(other)), QString());
        QQuickItem *copy = waitForButton("Save a Copy…");
        QVERIFY(copy);
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centerOf(copy));
        QTRY_VERIFY(!m_canvas->overwriteConfirmationPending());
        QCOMPARE(finished.count(), 3); // reported as cancelled (no error); the window then opens Save As
        QVERIFY(!finished.last().at(0).toBool());
        QVERIFY(finished.last().at(1).toString().isEmpty());
        QCOMPARE(provider.contents(other), oldBytes);
        QTest::qWait(200);
        QTest::keyClick(m_window, Qt::Key_Escape); // close the Save As dialog
        QTest::qWait(100);
        m_canvas->projectIo().setProviderForTesting(nullptr);
    }

    // On a touch screen a stray tap next to the recovery prompt must not dismiss it; Escape
    // postpones and keeps the recovery file.
    void strayPressKeepsTheRecoveryPrompt()
    {
        QTemporaryDir dir;
        QString orphanPath;
        {
            RecoveryManager crashed(dir.path());
            QCOMPARE(crashed.start(), QString());
            RecoveryFile::Metadata metadata;
            metadata.savedAt = QDateTime::currentDateTimeUtc();
            QCOMPARE(crashed.writeNow(m_canvas->document().snapshot(), metadata), QString());
            orphanPath = crashed.recoveryFilePath();
            crashed.releaseKeepingFile();
        }
        strayPressInSecondWindow(dir.path(), orphanPath); // a slot with parameters: not run as a test
        // Shortcuts only reach the active window; later tests use the main window again.
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
    }

    void strayPressInSecondWindow(const QString &recoveryDir, const QString &orphanPath)
    {
        RecoveryManager recovery(recoveryDir); // outlives the second window's canvas
        QQmlApplicationEngine engine;
        engine.loadFromModule("DrawApp", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        QQuickWindow *const mainWindow = m_window;
        CanvasItem *const mainCanvas = m_canvas;
        const auto restore = qScopeGuard([&] { m_window = mainWindow; m_canvas = mainCanvas; });
        m_window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(m_window);
        m_window->resize(1200, 800);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_canvas = qobject_cast<CanvasItem *>(find("canvas"));
        QVERIFY(m_canvas);
        m_canvas->setRecoveryManager(&recovery);

        QQuickItem *restoreButton = waitForButton("Restore");
        QVERIFY(restoreButton);
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, QPoint(40, m_window->height() / 2));
        QTest::qWait(150);
        QVERIFY(findButton("Restore"));
        QVERIFY(m_canvas->recoveryOffered());
        snap("recovery_prompt_after_stray_press");

        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!findButton("Restore"));
        QVERIFY(!m_canvas->recoveryOffered());
        QVERIFY(QFile::exists(orphanPath)); // postponed, not discarded
    }

    void closeWaitsForTheRunningSave()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("close.drawapp");
        QCOMPARE(waitFor([&] { return m_canvas->saveFileAs(QUrl::fromLocalFile(path)); }, true), QString());
        draw({{200, 400}, {400, 400}});
        QSemaphore gate;
        m_canvas->projectIo().setBeforeFinishHookForTesting([&] { gate.acquire(); });
        QTest::keyClick(m_window, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(m_canvas->fileBusy());
        m_window->close();
        QTest::qWait(150);
        QVERIFY(m_window->isVisible()); // not closed in the middle of the save
        QVERIFY(find("noticeLabel")->property("text").toString().contains("closes when it has finished"));
        gate.release();
        QTRY_VERIFY(!m_window->isVisible()); // saved and unmodified: the window closes on its own
        m_canvas->projectIo().setBeforeFinishHookForTesting({});
        QCOMPARE(ProjectFile::load(path).document.composite(), m_canvas->document().composite());
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"

#pragma once

#include "LayeredDocument.h"
#include "Pressure.h"
#include "ProjectIo.h"
#include "RasterDocument.h"
#include "RecoveryManager.h"
#include "Selection.h"
#include "BrushLibrary.h"
#include "BrushSettings.h"
#include "StrokeEngine.h"
#include "TileSnapshot.h"
#include "TransformApply.h"
#include "TransformSession.h"
#include "UndoHistory.h"
#include "WetLayer.h"
#include "ViewPyramid.h"
#include "ViewTransform.h"

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QTabletEvent;

class CanvasItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int brushSize READ brushSize WRITE setBrushSize NOTIFY brushSizeChanged)
    Q_PROPERTY(QColor brushColor READ brushColor WRITE setBrushColor NOTIFY brushColorChanged)
    // The brush presets and the paint and eraser working brushes (see BrushLibrary).
    Q_PROPERTY(BrushLibrary *brushes READ brushes CONSTANT)
    // Shortcuts to settings of the current tool's working brush, captured when a stroke starts
    // (see StrokeEngine). Pressure settings only matter for pens that report pressure.
    Q_PROPERTY(qreal brushOpacity READ brushOpacity WRITE setBrushOpacity NOTIFY brushSettingsChanged)
    Q_PROPERTY(qreal pressureMinSize READ pressureMinSize WRITE setPressureMinSize NOTIFY brushSettingsChanged)
    Q_PROPERTY(qreal pressureFirmness READ pressureFirmness WRITE setPressureFirmness NOTIFY brushSettingsChanged)
    // 0 = off .. 100; the pull-string length is smoothing / 100 × MaxSmoothingRadius view pixels.
    Q_PROPERTY(int smoothing READ smoothing WRITE setSmoothing NOTIFY brushSettingsChanged)
    Q_PROPERTY(Tool tool READ tool WRITE setTool NOTIFY toolChanged)
    // Selection tool options and state (see Selection; selections are not saved in files).
    Q_PROPERTY(SelectShape selectShape READ selectShape WRITE setSelectShape NOTIFY selectToolChanged)
    Q_PROPERTY(SelectOp selectMode READ selectMode WRITE setSelectMode NOTIFY selectToolChanged)
    // Magic wand: tolerance 0..255 per channel, contiguous region or every similar pixel, and
    // whether it looks at all visible layers (the composite) or only the active layer.
    Q_PROPERTY(int wandTolerance READ wandTolerance WRITE setWandTolerance NOTIFY selectToolChanged)
    Q_PROPERTY(bool wandContiguous READ wandContiguous WRITE setWandContiguous NOTIFY selectToolChanged)
    Q_PROPERTY(bool wandSampleAll READ wandSampleAll WRITE setWandSampleAll NOTIFY selectToolChanged)
    // A polygon lasso is being drawn (points placed, not yet closed).
    Q_PROPERTY(bool polygonOpen READ polygonOpen NOTIFY selectToolChanged)
    // The open transform moves only the selection's outline, not pixels.
    Q_PROPERTY(bool transformingSelection READ transformingSelection NOTIFY transformChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool selectionEmpty READ selectionEmpty NOTIFY selectionChanged)
    Q_PROPERTY(bool canReselect READ canReselect NOTIFY selectionChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    // A transform preview is open (see TransformSession); the numbers describe its box.
    Q_PROPERTY(bool transforming READ isTransforming NOTIFY transformChanged)
    // A transform is being applied on a worker thread (milestone 10); edits wait for it.
    Q_PROPERTY(bool applying READ isApplying NOTIFY applyChanged)
    // An action that could not be undone is waiting for the artist's answer (milestone 10).
    Q_PROPERTY(QString confirmationText READ confirmationText NOTIFY confirmationChanged)
    Q_PROPERTY(qreal applyProgress READ applyProgress NOTIFY applyProgressChanged)
    Q_PROPERTY(qreal transformX READ transformX NOTIFY transformChanged)
    Q_PROPERTY(qreal transformY READ transformY NOTIFY transformChanged)
    Q_PROPERTY(qreal transformWidth READ transformWidth NOTIFY transformChanged)
    Q_PROPERTY(qreal transformHeight READ transformHeight NOTIFY transformChanged)
    Q_PROPERTY(qreal transformRotation READ transformRotation NOTIFY transformChanged)
    Q_PROPERTY(bool keepAspect READ keepAspect WRITE setKeepAspect NOTIFY transformChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(QSize documentSize READ documentSize NOTIFY documentChanged)
    Q_PROPERTY(QString inputStatus READ inputStatus NOTIFY inputStatusChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(bool modified READ isModified NOTIFY historyChanged)
    Q_PROPERTY(bool hasFile READ hasFile NOTIFY fileChanged)
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileChanged)
    Q_PROPERTY(QString documentTitle READ documentTitle NOTIFY fileChanged)
    Q_PROPERTY(int autosaveInterval READ autosaveInterval WRITE setAutosaveInterval NOTIFY autosaveIntervalChanged)
    Q_PROPERTY(bool recoveryOffered READ recoveryOffered NOTIFY recoveryChanged)
    Q_PROPERTY(QString recoveryDescription READ recoveryDescription NOTIFY recoveryChanged)
    Q_PROPERTY(QString recoveryWarning READ recoveryWarning NOTIFY recoveryChanged)
    Q_PROPERTY(QString recoveryStatus READ recoveryStatus NOTIFY recoveryStatusChanged)
    // Layers for the panel, top layer first: {id, name, visible, opacity, locked, active}.
    Q_PROPERTY(QVariantList layers READ layers NOTIFY layersChanged)
    Q_PROPERTY(int activeLayerId READ activeLayerId NOTIFY layersChanged)
    Q_PROPERTY(int layerCount READ layerCount NOTIFY layersChanged)
    Q_PROPERTY(int maxLayers READ maxLayers NOTIFY documentChanged)
    // A save or open is running on the worker thread.
    Q_PROPERTY(bool fileBusy READ fileBusy NOTIFY fileBusyChanged)
    Q_PROPERTY(QString fileBusyText READ fileBusyText NOTIFY fileBusyChanged)
    Q_PROPERTY(bool opening READ isOpening NOTIFY fileBusyChanged)
    // A save to a storage-provider document is waiting for the user to confirm the overwrite.
    Q_PROPERTY(bool overwriteConfirmationPending READ overwriteConfirmationPending NOTIFY overwriteConfirmationChanged)
    Q_PROPERTY(QString overwriteTarget READ overwriteTarget NOTIFY overwriteConfirmationChanged)
    Q_PROPERTY(QString overwriteDetails READ overwriteDetails NOTIFY overwriteConfirmationChanged)

public:
    enum Tool { Brush, Eraser, Select, Transform };
    Q_ENUM(Tool)
    enum SelectShape { RectangleShape, EllipseShape, LassoShape, PolygonShape, WandShape };
    Q_ENUM(SelectShape)
    enum SelectOp { Replace, Add, Subtract, Intersect };
    Q_ENUM(SelectOp)
    // Handle sizes in screen pixels: touch-sized hit areas.
    static constexpr qreal HandleHitRadius = 22.0;
    static constexpr qreal RotateKnobDistance = 40.0;

    static constexpr int MinBrushSize = 1;
    static constexpr int MaxBrushSize = 256;
    static constexpr qreal MinBrushOpacity = 0.01;
    static constexpr qreal MaxSmoothingRadius = StrokeEngine::MaxSmoothingRadius; // view pixels at smoothing 100

    explicit CanvasItem(QQuickItem *parent = nullptr);

    ~CanvasItem() override;

    BrushLibrary *brushes() { return &m_brushes; }
    const BrushLibrary *brushes() const { return &m_brushes; }
    // The settings a new stroke uses: the eraser's or the paint brush's working settings.
    BrushSettings strokeSettings(bool erase) const { return m_brushes.settings(erase); }

    // Draws one whole stroke from samples in document coordinates through the same path as pointer
    // input: active-layer checks, the current tool's working brush, one undo step. For tests and
    // future automation; no pointer events are involved. Returns false (and changes nothing) if
    // the active layer cannot be painted or another gesture is in progress.
    bool drawStroke(const std::vector<StrokeSample> &samples, bool erase = false);
    // Watercolour paint still drying on the canvas (CanvasItemWet.cpp).
    bool hasWetPaint() const { return m_wetLayer != nullptr; }
    // True while the latest wet stroke's undo step is open (not yet in history()).
    bool wetStepOpen() const { return !m_wetBefore.isEmpty(); }
    const WetLayer *wetLayer() const { return m_wetLayer.get(); }
    // Advances the drying by `ms` of simulation time, as the drying timer does with the time that
    // passed; with a budget (the timer) at most that much time is spent stepping. Tests call it
    // without one (deterministic).
    void advanceWetPaint(double ms, double budgetMs = 0);
    // Combines a shape (document coordinates) with the selection, as one undo step, like the
    // selection tools. For tests and automation.
    void selectPath(const QPainterPath &docPath, SelectionMode mode);
    const Selection &selection() const { return m_document.selection(); }
    const TransformSession &transformSession() const { return m_transform; }
    TransformSession &transformSessionForTesting() { return m_transform; }
    // Tests: a different undo budget (clears the history).
    void setHistoryBudgetForTesting(qsizetype bytes)
    {
        m_history = UndoHistory(bytes);
        emit historyChanged();
    }
    // What the view shows: the document, or the transform preview while one is open.
    const TiledImage &displayedComposite() const;

    SelectShape selectShape() const { return m_selectShape; }
    void setSelectShape(SelectShape shape);
    SelectOp selectMode() const { return m_selectMode; }
    void setSelectMode(SelectOp mode);
    bool hasSelection() const { return m_document.selection().isActive(); }
    bool selectionEmpty() const { return m_document.selection().isEmpty(); }
    bool canReselect() const { return m_reselect.isActive() && !m_reselect.isEmpty(); }
    bool canPaste() const;
    bool isTransforming() const { return m_transform.isActive(); }
    qreal transformX() const { return m_transform.center().x(); }
    qreal transformY() const { return m_transform.center().y(); }
    qreal transformWidth() const { return m_transform.width(); }
    qreal transformHeight() const { return m_transform.height(); }
    qreal transformRotation() const { return m_transform.rotation(); }
    bool keepAspect() const { return m_keepAspect; }
    void setKeepAspect(bool keep);

    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deselect();
    Q_INVOKABLE void invertSelection();
    Q_INVOKABLE void reselect();
    // Modify the selection by radius pixels (one undo step each).
    Q_INVOKABLE void growSelection(qreal radius);
    Q_INVOKABLE void shrinkSelection(qreal radius);
    Q_INVOKABLE void featherSelection(qreal radius);
    // Magic wand at a document position, combined with mode (as the tool does on a click).
    void wandAt(const QPointF &docPos, SelectionMode mode);
    // Polygon lasso: closes the open polygon (as Enter or a double click does), or drops it.
    Q_INVOKABLE void closePolygon();
    Q_INVOKABLE void cancelPolygon();
    // Transforms only the selection's outline (move, scale, rotate, flip the mask).
    Q_INVOKABLE QString beginSelectionTransform();
    int wandTolerance() const { return m_wandTolerance; }
    void setWandTolerance(int tolerance);
    bool wandContiguous() const { return m_wandContiguous; }
    void setWandContiguous(bool contiguous);
    bool wandSampleAll() const { return m_wandSampleAll; }
    void setWandSampleAll(bool all);
    bool polygonOpen() const { return !m_polygon.empty(); }
    bool transformingSelection() const { return m_transform.isActive() && m_transformLayerIds.empty(); }
    // Clears the selected pixels of the active layer (the whole layer without a selection).
    Q_INVOKABLE void clearSelected();
    // Copy and Cut take the selected pixels of the active layer (soft edges as partial alpha), or
    // the whole layer without a selection. Paste puts them on a new layer above the active one at
    // their original place. Each returns an empty string or the reason it did nothing.
    Q_INVOKABLE QString copySelection();
    Q_INVOKABLE QString cutSelection();
    Q_INVOKABLE QString paste();
    // Starts a transform of the selected pixels (or the active layer); Apply is one undo step,
    // Cancel restores the original exactly. Returns an empty string or the reason it cannot start.
    Q_INVOKABLE QString beginTransform();
    // Large transforms are applied on a worker thread (see `applying`); small ones at once.
    Q_INVOKABLE void applyTransform();
    // Stops a transform being applied; the preview stays open and the document is unchanged.
    Q_INVOKABLE void cancelApply();
    // Answers confirmationText: true runs the action without undo, false drops it.
    Q_INVOKABLE void answerConfirmation(bool proceed);
    QString confirmationText() const { return m_confirmationText; }
    // Memory in use by category, the limit, and the undo history (for the Memory dialog).
    Q_INVOKABLE QString memoryReport() const;
    bool isApplying() const { return m_apply.isRunning(); }
    qreal applyProgress() const { return m_apply.progress(); }
    // Tests: the work (changed pixels x layers) from which Apply runs on the worker thread.
    void setBackgroundApplyThresholdForTesting(qint64 pixels) { m_backgroundApplyPixels = pixels; }
    TransformApply &transformApplyForTesting() { return m_apply; }
    Q_INVOKABLE void cancelTransform();
    // key: "x", "y" (box centre), "width", "height" (pixels), "rotation" (degrees).
    Q_INVOKABLE void setTransformValue(const QString &key, qreal value);
    Q_INVOKABLE void flipTransformHorizontal();
    Q_INVOKABLE void flipTransformVertical();
    Q_INVOKABLE void resetTransform();

    int brushSize() const;
    void setBrushSize(int size);
    QColor brushColor() const { return m_brushColor; }
    void setBrushColor(const QColor &color);
    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    qreal brushOpacity() const;
    void setBrushOpacity(qreal opacity);
    qreal pressureMinSize() const;
    void setPressureMinSize(qreal fraction);
    qreal pressureFirmness() const;
    void setPressureFirmness(qreal firmness);
    int smoothing() const;
    void setSmoothing(int smoothing);
    qreal zoom() const { return m_view.zoom(); }
    QSize documentSize() const { return m_document.size(); }
    QString inputStatus() const { return m_inputStatus; }
    const LayeredDocument &document() const { return m_document; }
    const ViewTransform &viewTransform() const { return m_view; }
    const UndoHistory &history() const { return m_history; }

    // Watercolour still drying is an undo step that is not yet in the history (CanvasItemWet.cpp).
    bool canUndo() const { return m_history.canUndo() || !m_wetBefore.isEmpty(); }
    bool canRedo() const { return m_wetBefore.isEmpty() && m_history.canRedo(); }
    QString undoText() const { return m_wetBefore.isEmpty() ? m_history.undoLabel() : m_wetLabel; }
    QString redoText() const { return m_history.redoLabel(); }
    bool isModified() const { return m_history.stateId() != m_savedStateId || !m_wetBefore.isEmpty(); }
    bool hasFile() const { return !m_filePath.isEmpty(); }
    QUrl fileUrl() const { return m_fileUrl; }
    QString documentTitle() const;

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void newDocument();
    // A new canvas of the given size (1 to 16384 pixels per side, milestone 10); empty on
    // success, otherwise why not. Memory is used only where it is painted.
    Q_INVOKABLE QString newDocumentSized(int width, int height);
    Q_INVOKABLE int maxCanvasSide() const { return DocumentLimits::MaxSide; }
    // Saving and opening run on a worker thread. These return an empty string when the operation
    // started (or, for Save during a save to the same file, was queued) and otherwise a message to
    // show. The outcome arrives later through saveFinished / openFinished.
    Q_INVOKABLE QString openFile(const QUrl &url);
    Q_INVOKABLE QString saveFile();
    Q_INVOKABLE QString saveFileAs(const QUrl &url);
    // Also on the worker; the outcome arrives through exportFinished.
    Q_INVOKABLE QString exportPng(const QUrl &url);
    bool fileBusy() const { return m_save.id != 0 || m_open.id != 0 || m_exportId != 0; }
    QString fileBusyText() const;
    bool isOpening() const { return m_open.id != 0; }
    // After overwriteConfirmationNeeded: overwrite the document (asked once per file per session),
    // or give up this save (saveFinished reports it as cancelled).
    Q_INVOKABLE QString confirmOverwrite();
    Q_INVOKABLE void cancelOverwrite();
    bool overwriteConfirmationPending() const { return m_pendingOverwrite.has_value(); }
    QString overwriteTarget() const;
    QString overwriteDetails() const { return m_pendingOverwrite ? m_pendingOverwrite->details : QString(); }
    // Where a local copy of a project is kept while saving to a storage provider if crash recovery
    // is not running (default: <app data>/staging).
    void setStagingDirectory(const QString &directory) { m_stagingDirectory = directory; }
    QString stagingDirectory() const;
    // After openConflict: true replaces the drawing with the opened file, false keeps the drawing.
    Q_INVOKABLE void resolveOpenConflict(bool useOpenedFile);
    bool hasOpenConflict() const { return m_conflict.has_value(); }
    ProjectIo &projectIo() { return m_io; }

    // Crash recovery. The manager must outlive this item.
    void setRecoveryManager(RecoveryManager *manager);
    // At application exit: removes recovery data if there is nothing to recover, otherwise
    // writes the latest state and keeps it for the next start.
    void shutdownRecovery();
    int autosaveInterval() const { return m_autosaveIntervalMs; }
    void setAutosaveInterval(int milliseconds);
    bool recoveryOffered() const { return !m_orphans.isEmpty(); }
    QString recoveryDescription() const;
    QString recoveryWarning() const { return m_recoveryWarning; }
    QString recoveryStatus() const { return m_recoveryStatus; }
    quint64 contentRevision() const { return m_contentRevision; }
    qint64 maxRecoveryLagMs() const { return m_maxRecoveryLagMs; }
    // Runs one autosave check now (the timer calls this too).
    void autosaveNow();
    Q_INVOKABLE void restoreRecovery();
    Q_INVOKABLE void discardRecovery();
    // Keeps the offered recovery files for the next start without restoring them now.
    Q_INVOKABLE void postponeRecovery();
    Q_INVOKABLE void clearRecoveryWarning();
    // The user chose to discard unsaved changes while closing.
    Q_INVOKABLE void discardForClose();

    // --- layers (ids are the stable layer ids; the active layer is not part of undo history) ---
    QVariantList layers() const;
    int activeLayerId() const { return int(m_document.activeLayerId()); }
    int layerCount() const { return m_document.count(); }
    int maxLayers() const { return LayeredDocument::maxLayersFor(m_document.size()); }
    Q_INVOKABLE void setActiveLayer(int id);
    // Multi-layer selection for transforms and copy, cut, paste, clear (Ctrl+click or long press).
    Q_INVOKABLE void setLayerSelected(int id, bool selected);
    // The selected layers' ids, bottom to top (always at least the active layer).
    std::vector<quint64> selectedLayerIds() const;
    Q_INVOKABLE void addLayer();
    Q_INVOKABLE void duplicateLayer(int id);
    Q_INVOKABLE void deleteLayer(int id);
    Q_INVOKABLE void moveLayerUp(int id);
    Q_INVOKABLE void moveLayerDown(int id);
    Q_INVOKABLE void renameLayer(int id, const QString &name);
    Q_INVOKABLE void setLayerVisible(int id, bool visible);
    Q_INVOKABLE void setLayerLocked(int id, bool locked);
    // Live opacity while a slider is dragged (not recorded), then one undo step for the drag.
    Q_INVOKABLE void previewLayerOpacity(int id, qreal opacity);
    Q_INVOKABLE void commitLayerOpacity(int id, qreal opacity);
    // Clears the active layer (one undo step). Refused for hidden or locked layers.
    Q_INVOKABLE void clearActiveLayer();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomToActualSize();
    Q_INVOKABLE void fitToView();

    void paint(QPainter *painter) override;

signals:
    void brushSizeChanged();
    void brushColorChanged();
    void brushSettingsChanged();
    void toolChanged();
    void selectToolChanged();
    void selectionChanged();
    void transformChanged();
    void applyChanged();
    void confirmationChanged();
    void applyProgressChanged();
    void clipboardChanged();
    void viewChanged();
    void inputStatusChanged();
    void documentChanged();
    void historyChanged();
    void fileChanged();
    void autosaveIntervalChanged();
    void recoveryChanged();
    void recoveryStatusChanged();
    void layersChanged();
    void fileBusyChanged();
    // ok = false with an empty error means the operation was cancelled.
    void saveFinished(bool ok, const QString &error);
    void openFinished(bool ok, const QString &error);
    void exportFinished(bool ok, const QString &error);
    // The save target is an existing storage-provider document that cannot be replaced safely;
    // see confirmOverwrite / cancelOverwrite. saveFinished follows only after that decision.
    void overwriteConfirmationNeeded();
    void overwriteConfirmationChanged();
    // An opened file is ready but the drawing changed while it was loading; see resolveOpenConflict.
    void openConflict();
    void recoverySnapshotTaken();
    // A user action was refused (for example drawing on a hidden layer); message explains why.
    void notice(const QString &message);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent *event) override;
    void touchEvent(QTouchEvent *event) override;
    void touchUngrabEvent() override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    enum class Gesture { None, Stroke, Pan, Pinch };
    // What a Stroke gesture does: paint, draw a selection shape, or drag the transform box.
    enum class PointerTask { Paint, Select, Transform };

    void watchWindow(QQuickWindow *window);
    bool handleTabletEvent(QTabletEvent *event);
    bool ignoreMouseEvent(const QMouseEvent *event) const;
    // Tilt in degrees (QTabletEvent::xTilt/yTilt), meaningful only with hasTilt.
    void beginStroke(const QPointF &viewPos, PointerKind kind, qreal pressure, bool eraserTip,
                     const QPointF &tilt = {}, bool hasTilt = false);
    void continueStroke(const QPointF &viewPos, qreal pressure, const QPointF &tilt = {}, bool hasTilt = false);
    void endStroke();
    // Undoes the stroke in progress completely (Escape); nothing enters the history.
    // After an engine call: if a tile could not be allocated, takes the stroke back. True then.
    bool strokeOutOfMemory();
    // Apply: in the background (the user's Apply) or at once (implicit applies before another action).
    void applyTransformWith(bool background);
    void commitApply(const TransformApply::Result &result);
    // True (after a notice) while a transform is being applied: edits wait for it.
    bool applyBlocks();
    // When an action's undo record would not fit in the history (undoBytes larger than its whole
    // budget), asks first: returns true and keeps `action` for answerConfirmation(). Once confirmed,
    // the action runs again and this returns false.
    bool confirmIfNotUndoable(qsizetype undoBytes, const QString &what, std::function<void()> action);
    // Rebuilds the composite over rect; says so if memory for it was refused.
    void refreshComposite(const QRect &rect = QRect());
    QString applyBusyText() const;
    void cancelStroke();
    // Selection and transform gestures (CanvasItemSelection.cpp).
    void beginSelectGesture(const QPointF &viewPos);
    void continueSelectGesture(const QPointF &viewPos);
    void finishSelectGesture();
    void beginTransformDrag(const QPointF &viewPos);
    void continueTransformDrag(const QPointF &viewPos);
    void finishTransformDrag();
    void abortPointerGesture();
    void setSelectionWithUndo(const Selection &next, const QString &label);
    // Empty if every selected layer can be edited, otherwise why not (naming the layer).
    QString selectedLayersBlockReason(const QString &action) const;
    // Drops ids that no longer exist and makes sure the active layer is selected.
    void sanitizeLayerSelection();
    void selectionUpdated();
    void updateTransformPreview(TransformSession::Quality quality);
    // Ends what is in progress before another document edit: a stroke or gesture, and an open
    // transform (applied).
    void finishPendingEdits();
    void endTransformSession();
    void drawOverlays(QPainter *painter);
    // Repaints the whole item. QQuickPaintedItem::update() without a rectangle is lost if a
    // partial update(rect) follows before the next frame (the pending area becomes that rect), so
    // full repaints pass the item's rectangle explicitly.
    void updateAll() { update(boundingRect().toAlignedRect()); }
    // The document-space part of beginStroke/continueStroke.
    bool startStroke(const StrokeSample &sample, PointerKind kind, bool erase);
    void extendStroke(const StrokeSample &sample);
    void advanceAirbrush();
    // Watercolour (CanvasItemWet.cpp).
    void prepareWetStroke(bool watercolour);
    bool keepStrokeWet();
    void commitWetPaint();
    void finishWetPaint();
    void dryWetPaintTick();
    void noteWetPaintSaved();
    StrokeSample strokeSample(const QPointF &viewPos, qreal pressure, const QPointF &tilt, bool hasTilt) const;
    void beginPan(const QPointF &viewPos);
    void continuePan(const QPointF &viewPos);
    void endPan();
    // Publishes pixels a stroke changed on its layer (composite, recovery, repaint).
    void strokePixelsChanged(const QRect &changed, qint64 dabs);
    bool commitPixels(quint64 layerId, TileSnapshot before, const QString &label);
    void pushCommand(UndoCommand command);
    void applyHistoryResult(const UndoCommand::Result &result);
    // Empty if the active layer can be painted on, otherwise the reason it cannot.
    QString activeLayerBlockReason() const;
    void setLayerProperties(int id, const LayerProperties &props, const QString &label);
    void markContentChanged();
    bool needsRecovery() const;
    void discardRecoveryFile();
    void onRecoveryWriteFinished(bool committed, bool stale, const QString &error, quint64 documentId,
                                 quint64 revision, qint64 workerMs);
    void setRecoveryStatus(const QString &status);
    RecoveryFile::Metadata recoveryMetadata() const;
    void replaceDocument(const LayeredDocument &document);
    QString startSave(const QString &path, const QUrl &url);
    SafeSave::LocalCopy makeLocalCopy();
    void onSaveFinished(quint64 id, const QString &path, const SafeSave::Result &result, qint64 workerMs);
    void onExportFinished(quint64 id, const QString &path, const SafeSave::Result &result, qint64 workerMs);
    void onOpenFinished(quint64 id, const QString &path, const ProjectFile::LoadResult &result, qint64 workerMs);
    void applyOpened(const DocumentData &document, const QString &path, const QUrl &url);
    void setFile(const QString &path, const QUrl &url, const QString &knownName = QString());
    void updateDocumentRect(const QRect &docRect);
    void viewModified();
    void setInputStatus(const QString &status);
    void reportStats();

    LayeredDocument m_document;
    UndoHistory m_history;
    TileSnapshot m_strokeBefore;
    quint64 m_strokeLayerId = 0;
    quint64 m_opacityDragLayerId = 0;  // non-zero while an opacity slider drag is previewing
    qreal m_opacityDragStart = 1.0;
    quint64 m_savedStateId = 0;
    QString m_filePath;
    QUrl m_fileUrl;
    QString m_fileName; // display name, see setFile

    struct SaveRequest
    {
        quint64 id = 0; // 0 = no save running
        QString path;
        QUrl url;
        quint64 stateId = 0;    // history state captured in the snapshot
        quint64 documentId = 0;
    };
    struct OpenRequest
    {
        quint64 id = 0; // 0 = no open running
        QString path;
        QUrl url;
        quint64 documentId = 0; // document and revision when the open started
        quint64 revision = 0;
    };
    struct OpenConflict
    {
        DocumentData document;
        QString path;
        QUrl url;
    };
    struct PendingOverwrite
    {
        QString path;
        QUrl url;
        QString details; // what the storage provider reported
    };
    ProjectIo m_io;
    SaveRequest m_save;
    bool m_saveQueued = false; // Save pressed again during a save to the same file
    OpenRequest m_open;
    quint64 m_exportId = 0;
    std::optional<OpenConflict> m_conflict;
    std::optional<PendingOverwrite> m_pendingOverwrite;
    QSet<QString> m_overwriteConfirmed; // provider documents the user agreed to overwrite (this session)
    QString m_stagingDirectory;

    RecoveryManager *m_recovery = nullptr;
    QTimer m_autosaveTimer;
    int m_autosaveIntervalMs = 10000;
    quint64 m_documentId = 1;
    quint64 m_contentRevision = 0;   // changes with every document change, including mid-stroke
    quint64 m_requestedDocumentId = 0;
    quint64 m_requestedRevision = 0; // last revision handed to a recovery write
    bool m_autosaveTickMissed = false;
    bool m_firstEditAfterSnapshot = false; // for timing the deferred layer copy
    bool m_discardedForClose = false;
    QList<RecoveryManager::Orphan> m_orphans;
    QString m_recoveryWarning;
    QString m_recoveryStatus;
    // Loss-window measurement: age of the oldest change not yet in a committed recovery file.
    QElapsedTimer m_recoveryClock;
    qint64 m_oldestUnrecoveredMs = -1;
    qint64 m_inFlightOldestMs = -1;
    qint64 m_maxRecoveryLagMs = 0;
    qint64 m_lastTickMs = -1;
    ViewTransform m_view;
    bool m_viewInitialized = false;

    QColor m_brushColor = QColor(20, 20, 20);
    Tool m_tool = Brush;

    Gesture m_gesture = Gesture::None;
    PointerKind m_strokeKind = PointerKind::Mouse;
    BrushMode m_strokeMode = BrushMode::Paint;
    BrushBlend m_strokeBlend = BrushBlend::Paint;
    BrushLibrary m_brushes{this};
    // The stroke in progress, created with the settings at stroke start. Sample times come from
    // m_strokeClock; while an airbrush is held still, m_airbrushTimer advances it.
    std::unique_ptr<StrokeEngine> m_engine;
    QElapsedTimer m_strokeClock;
    QTimer m_airbrushTimer;
    quint64 m_strokeCount = 0; // per-stroke random seed
    // Watercolour: the wet state of one layer's paint, kept after the stroke while it dries
    // (wet-on-wet across strokes), with the open undo record of the latest wet stroke. Any other
    // edit, undo or redo first ends it: the paint stays exactly as it looks (CanvasItemWet.cpp).
    std::unique_ptr<WetLayer> m_wetLayer;
    quint64 m_wetLayerId = 0;
    TileSnapshot m_wetBefore;
    QString m_wetLabel;
    QTimer m_wetTimer;
    QElapsedTimer m_wetClock;
    double m_wetPendingMs = 0;
    PointerTask m_pointerTask = PointerTask::Paint;
    Qt::KeyboardModifiers m_pointerModifiers; // from the latest pointer event

    // --- selection ---
    SelectShape m_selectShape = RectangleShape;
    SelectOp m_selectMode = Replace;
    SelectionMode m_gestureMode = SelectionMode::Replace;
    QPointF m_selectStartView;
    std::vector<QPointF> m_selectPoints; // document coordinates (start and current, or the lasso)
    std::vector<QPointF> m_polygon;      // polygon lasso vertices placed so far (document)
    SelectionMode m_polygonMode = SelectionMode::Replace;
    QPointF m_hoverDoc;                  // pointer position for the polygon's rubber band
    bool m_hoverKnown = false;
    int m_wandTolerance = 32;
    bool m_wandContiguous = true;
    bool m_wandSampleAll = false;
    QPainterPath m_transformedOutline;   // Transform Selection: the outline as it will be
    Selection m_reselect;                // brought back by Reselect
    QPainterPath m_selectionOutline;     // document coordinates, cached per selection change

    // --- transform ---
    TransformSession m_transform;
    TransformApply m_apply; // after m_transform: destroyed (cancelled and joined) first
    QString m_confirmationText;
    std::function<void()> m_confirmedAction;
    bool m_confirmed = false;    // running a confirmed action: no second question
    int m_undoEvictor = -1;      // MemoryBudget evictor dropping old undo steps under pressure
    QTimer m_applyProgressTimer;
    qint64 m_backgroundApplyPixels = 4 * 1024 * 1024;
    std::vector<quint64> m_transformLayerIds;
    // The session's last part is a merged copy of the (adjacent) layers, used for the preview.
    bool m_previewMerged = false; // bottom to top
    Tool m_toolBeforeTransform = Brush;
    bool m_keepAspect = true;
    // The transformed layers, rendered from the session's source: copies of the document's layers
    // (sharing their tiles) in which only the transformed area's tiles are rewritten.
    std::vector<TiledImage> m_previewLayers;
    TiledImage m_previewComposite; // the document with the preview layers, what the view shows
    ViewPyramid m_viewPyramid;     // reduced levels of what the view shows, for zooming out
    QRect m_previewRect;       // area the preview last differed from the document in
    TransformSession::Handle m_dragHandle = TransformSession::Handle::None;
    QPointF m_dragLastDoc;
    QPointF m_dragStartCenter;
    double m_dragStartRotation = 0;
    double m_dragStartAngle = 0;

    // --- clipboard (internal; also offered to the system clipboard) ---
    struct ClipItem
    {
        QString name;   // the layer it came from
        QImage image;   // premultiplied, trimmed to its visible pixels
        QPoint origin;  // where it was in the document
    };
    std::vector<ClipItem> m_clipboard; // one item per copied layer, bottom to top
    QSize m_clipDocSize;

    // Layers selected in the panel (always including the active one). View state, not history.
    QSet<quint64> m_selectedLayers;
    qreal m_lastPressure = 1.0;
    QPointF m_lastTilt;
    bool m_lastHasTilt = false;
    bool m_penReportsPressure = false;
    bool m_strokeHasPressure = false;
    QPointF m_lastPanPos;
    bool m_spaceHeld = false;

    qreal m_lastPinchDistance = 0;
    QPointF m_lastPinchCenter;

    QPointer<QQuickWindow> m_watchedWindow;
    bool m_penDown = false;
    QElapsedTimer m_sincePenEvent;

    QString m_inputStatus;

    bool m_statsEnabled = false;
    QElapsedTimer m_statsWindow;
    int m_statsPaints = 0;
    qint64 m_statsPaintNs = 0;
    qint64 m_statsPaintMaxNs = 0;
    int m_statsInputs = 0;
    qint64 m_statsInputNs = 0;
    qint64 m_statsInputMaxNs = 0;
    qint64 m_statsDabs = 0;
    QTimer m_lagProbe;
    qint64 m_lagProbeLastMs = -1;
    qint64 m_statsMaxLoopLagMs = 0;
};

#pragma once

#include "LayeredDocument.h"
#include "Pressure.h"
#include "ProjectIo.h"
#include "RasterDocument.h"
#include "RecoveryManager.h"
#include "StrokeSpacer.h"
#include "TileSnapshot.h"
#include "UndoHistory.h"
#include "ViewTransform.h"

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <optional>
#include <vector>

class QTabletEvent;

class CanvasItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int brushSize READ brushSize WRITE setBrushSize NOTIFY brushSizeChanged)
    Q_PROPERTY(QColor brushColor READ brushColor WRITE setBrushColor NOTIFY brushColorChanged)
    Q_PROPERTY(Tool tool READ tool WRITE setTool NOTIFY toolChanged)
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

public:
    enum Tool { Brush, Eraser };
    Q_ENUM(Tool)

    static constexpr int MinBrushSize = 1;
    static constexpr int MaxBrushSize = 256;

    explicit CanvasItem(QQuickItem *parent = nullptr);

    int brushSize() const { return m_brushSize; }
    void setBrushSize(int size);
    QColor brushColor() const { return m_brushColor; }
    void setBrushColor(const QColor &color);
    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    qreal zoom() const { return m_view.zoom(); }
    QSize documentSize() const { return m_document.size(); }
    QString inputStatus() const { return m_inputStatus; }
    const LayeredDocument &document() const { return m_document; }
    const ViewTransform &viewTransform() const { return m_view; }
    const UndoHistory &history() const { return m_history; }

    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    QString undoText() const { return m_history.undoLabel(); }
    QString redoText() const { return m_history.redoLabel(); }
    bool isModified() const { return m_history.stateId() != m_savedStateId; }
    bool hasFile() const { return !m_filePath.isEmpty(); }
    QUrl fileUrl() const { return m_fileUrl; }
    QString documentTitle() const;

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void newDocument();
    // Saving and opening run on a worker thread. These return an empty string when the operation
    // started (or, for Save during a save to the same file, was queued) and otherwise a message to
    // show. The outcome arrives later through saveFinished / openFinished.
    Q_INVOKABLE QString openFile(const QUrl &url);
    Q_INVOKABLE QString saveFile();
    Q_INVOKABLE QString saveFileAs(const QUrl &url);
    // Synchronous; returns an empty string on success, otherwise a message.
    Q_INVOKABLE QString exportPng(const QUrl &url);
    bool fileBusy() const { return m_save.id != 0 || m_open.id != 0; }
    QString fileBusyText() const;
    bool isOpening() const { return m_open.id != 0; }
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
    void toolChanged();
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

private:
    enum class Gesture { None, Stroke, Pan, Pinch };

    void watchWindow(QQuickWindow *window);
    bool handleTabletEvent(QTabletEvent *event);
    bool ignoreMouseEvent(const QMouseEvent *event) const;
    void beginStroke(const QPointF &viewPos, PointerKind kind, qreal pressure, bool eraserTip);
    void continueStroke(const QPointF &viewPos, qreal pressure);
    void endStroke();
    void beginPan(const QPointF &viewPos);
    void continuePan(const QPointF &viewPos);
    void endPan();
    void applyPendingDabs();
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
    void onSaveFinished(quint64 id, const QString &path, const QString &error, qint64 workerMs);
    void onOpenFinished(quint64 id, const QString &path, const ProjectFile::LoadResult &result, qint64 workerMs);
    void applyOpened(const DocumentData &document, const QString &path, const QUrl &url);
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
    ProjectIo m_io;
    SaveRequest m_save;
    bool m_saveQueued = false; // Save pressed again during a save to the same file
    OpenRequest m_open;
    std::optional<OpenConflict> m_conflict;

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

    int m_brushSize = 12;
    QColor m_brushColor = QColor(20, 20, 20);
    Tool m_tool = Brush;

    Gesture m_gesture = Gesture::None;
    PointerKind m_strokeKind = PointerKind::Mouse;
    BrushMode m_strokeMode = BrushMode::Paint;
    QColor m_strokeColor;
    std::optional<StrokeSpacer> m_spacer;
    std::vector<Dab> m_pendingDabs;
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

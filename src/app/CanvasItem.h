#pragma once

#include "Pressure.h"
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
    const RasterDocument &document() const { return m_document; }
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
    // These return an empty string on success, otherwise a message to show the user.
    Q_INVOKABLE QString openFile(const QUrl &url);
    Q_INVOKABLE QString saveFile();
    Q_INVOKABLE QString saveFileAs(const QUrl &url);
    Q_INVOKABLE QString exportPng(const QUrl &url);

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
    quint64 pixelRevision() const { return m_pixelRevision; }
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

    Q_INVOKABLE void clearCanvas();
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
    bool commitEdit(TileSnapshot before, const QString &label);
    void markPixelsChanged();
    bool needsRecovery() const;
    void discardRecoveryFile();
    void onRecoveryWriteFinished(bool committed, bool stale, const QString &error, quint64 documentId,
                                 quint64 revision, qint64 workerMs);
    void setRecoveryStatus(const QString &status);
    RecoveryFile::Metadata recoveryMetadata() const;
    void replaceDocument(const QImage &image);
    QString saveTo(const QString &path, const QUrl &url);
    void updateDocumentRect(const QRect &docRect);
    void viewModified();
    void setInputStatus(const QString &status);
    void reportStats();

    RasterDocument m_document;
    UndoHistory m_history;
    TileSnapshot m_strokeBefore;
    quint64 m_savedStateId = 0;
    QString m_filePath;
    QUrl m_fileUrl;

    RecoveryManager *m_recovery = nullptr;
    QTimer m_autosaveTimer;
    int m_autosaveIntervalMs = 10000;
    quint64 m_documentId = 1;
    quint64 m_pixelRevision = 0;     // changes with every pixel change, including mid-stroke
    quint64 m_requestedDocumentId = 0;
    quint64 m_requestedRevision = 0; // last revision handed to a recovery write
    bool m_autosaveTickMissed = false;
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

#pragma once

#include "Pressure.h"
#include "RasterDocument.h"
#include "StrokeSpacer.h"
#include "TileSnapshot.h"
#include "UndoHistory.h"
#include "ViewTransform.h"

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickWindow>
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
    void commitEdit(TileSnapshot before, const QString &label);
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
};

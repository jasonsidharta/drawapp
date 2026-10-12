#pragma once

#include "Pressure.h"
#include "RasterDocument.h"
#include "StrokeSpacer.h"
#include "ViewTransform.h"

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickWindow>
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
    Q_PROPERTY(QSize documentSize READ documentSize CONSTANT)
    Q_PROPERTY(QString inputStatus READ inputStatus NOTIFY inputStatusChanged)

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
    void updateDocumentRect(const QRect &docRect);
    void viewModified();
    void setInputStatus(const QString &status);
    void reportStats();

    RasterDocument m_document;
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

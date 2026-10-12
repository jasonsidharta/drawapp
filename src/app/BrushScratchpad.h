#pragma once

#include "CanvasItem.h"
#include "Pressure.h"
#include "StrokeEngine.h"
#include "TileSnapshot.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <memory>

class QTabletEvent;

// A small surface for trying the current brush with the real renderer: the same StrokeEngine,
// settings, colour, pressure handling and airbrush timing as the canvas, on a scratch layer that
// is never part of the document. Erasers and smudge get colour bands to work on.
class BrushScratchpad : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(CanvasItem *canvas READ canvas WRITE setCanvas NOTIFY canvasChanged)
    // Optional: while this studio is open, the pad paints with its (unsaved) copy of the brush.
    Q_PROPERTY(BrushStudio *studio READ studio WRITE setStudio NOTIFY studioChanged)

public:
    explicit BrushScratchpad(QQuickItem *parent = nullptr);
    ~BrushScratchpad() override;

    CanvasItem *canvas() const { return m_canvas; }
    void setCanvas(CanvasItem *canvas);
    BrushStudio *studio() const { return m_studio; }
    void setStudio(BrushStudio *studio);
    // The settings a stroke here uses.
    BrushSettings activeSettings() const;

    void paint(QPainter *painter) override;
    // Empties the scratch layer (with colour bands for erasers and smudge).
    Q_INVOKABLE void clear();

    // Strokes in item coordinates, as from input events (also used by tests).
    void beginStroke(const QPointF &pos, qreal pressure, const QPointF &tilt = {}, bool hasTilt = false);
    void continueStroke(const QPointF &pos, qreal pressure, const QPointF &tilt = {}, bool hasTilt = false);
    void endStroke();
    const QImage &layer() const { return m_layer; }

signals:
    void canvasChanged();
    void studioChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void touchEvent(QTouchEvent *event) override;

private:
    bool handleTablet(QTabletEvent *event);
    StrokeSample sampleAt(const QPointF &pos, qreal pressure, const QPointF &tilt, bool hasTilt) const;
    void ensureLayer();
    void advance();

    QPointer<CanvasItem> m_canvas;
    QPointer<BrushStudio> m_studio;
    QPointer<QQuickWindow> m_window;
    QImage m_layer;
    bool m_layerHasBands = false;
    std::unique_ptr<StrokeEngine> m_engine;
    TileSnapshot m_before;
    QElapsedTimer m_clock;
    QTimer m_airbrush;
    quint64 m_strokes = 0;
    bool m_penDown = false;
    // Whether the current input reports pressure (direct calls, as from tests, simulate it).
    bool m_inputHasPressure = true;
    QElapsedTimer m_sincePen;
};

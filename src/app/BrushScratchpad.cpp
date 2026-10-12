#include "BrushScratchpad.h"

#include "CanvasItem.h"

#include <QPainter>
#include <QTabletEvent>

namespace {

constexpr qint64 PenMouseSuppressMs = 400; // as CanvasItem: ignore mouse events synthesized from pen input

} // namespace

BrushScratchpad::BrushScratchpad(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptTouchEvents(true);
    m_airbrush.setInterval(16);
    m_airbrush.setTimerType(Qt::PreciseTimer);
    connect(&m_airbrush, &QTimer::timeout, this, &BrushScratchpad::advance);
}

BrushScratchpad::~BrushScratchpad()
{
    if (m_window)
        m_window->removeEventFilter(this);
}

void BrushScratchpad::setCanvas(CanvasItem *canvas)
{
    if (canvas == m_canvas)
        return;
    if (m_canvas)
        disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas;
    if (m_canvas) {
        // A different kind of brush (eraser or smudge vs. paint) needs a different scratch layer.
        connect(m_canvas, &CanvasItem::toolChanged, this, &BrushScratchpad::clear);
        connect(m_canvas->brushes(), &BrushLibrary::currentChanged, this, [this] {
            if ((activeSettings().blend != BrushBlend::Paint) != m_layerHasBands)
                clear();
        });
    }
    clear();
    emit canvasChanged();
}

BrushSettings BrushScratchpad::activeSettings() const
{
    if (m_studio && m_studio->active())
        return m_studio->settings(); // the Brush Studio's copy, before Done
    return m_canvas ? m_canvas->strokeSettings(m_canvas->tool() == CanvasItem::Eraser) : BrushSettings();
}

void BrushScratchpad::setStudio(BrushStudio *studio)
{
    if (studio == m_studio)
        return;
    if (m_studio)
        disconnect(m_studio, nullptr, this, nullptr);
    m_studio = studio;
    if (m_studio)
        connect(m_studio, &BrushStudio::changed, this, [this] {
            if ((activeSettings().blend != BrushBlend::Paint) != m_layerHasBands)
                clear();
        });
    clear();
    emit studioChanged();
}

void BrushScratchpad::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange) {
        // Pen pressure only arrives as tablet events at the window (see CanvasItem).
        if (m_window)
            m_window->removeEventFilter(this);
        m_window = value.window;
        if (m_window)
            m_window->installEventFilter(this);
    }
    QQuickPaintedItem::itemChange(change, value);
}

void BrushScratchpad::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size().toSize() != m_layer.size())
        clear();
}

void BrushScratchpad::ensureLayer()
{
    const QSize size = boundingRect().size().toSize();
    if (m_layer.size() != size || m_layer.isNull()) {
        m_layer = QImage(size.expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
        m_layer.fill(Qt::transparent);
    }
}

void BrushScratchpad::clear()
{
    endStroke();
    m_layer = QImage();
    ensureLayer();
    m_layerHasBands = m_canvas && activeSettings().blend != BrushBlend::Paint;
    if (m_layerHasBands) {
        QPainter p(&m_layer);
        const QColor bands[] = {QColor(205, 70, 60), QColor(235, 190, 70), QColor(60, 120, 200), QColor(50, 150, 90)};
        const int w = m_layer.width();
        for (int i = 0; i < 4; ++i)
            p.fillRect(QRectF(w * i / 4.0, 0, w / 4.0 + 1, m_layer.height() * 0.75), bands[i]);
    }
    update();
}

void BrushScratchpad::paint(QPainter *painter)
{
    const QRectF r = boundingRect();
    if (m_layerHasBands) {
        for (int y = 0; y < r.height(); y += 8)
            for (int x = 0; x < r.width(); x += 8)
                painter->fillRect(x, y, 8, 8, ((x + y) / 8) % 2 ? QColor(204, 204, 204) : QColor(250, 250, 250));
    } else {
        painter->fillRect(r, QColor(252, 251, 248));
    }
    painter->drawImage(0, 0, m_layer);
    painter->setPen(QColor(160, 160, 160));
    painter->drawRect(r.adjusted(0, 0, -1, -1));
}

StrokeSample BrushScratchpad::sampleAt(const QPointF &pos, qreal pressure, const QPointF &tilt, bool hasTilt) const
{
    StrokeSample s;
    s.pos = pos;
    s.pressure = pressure;
    s.hasPressure = m_inputHasPressure;
    s.hasTilt = hasTilt;
    if (hasTilt) {
        s.tiltX = tilt.x();
        s.tiltY = tilt.y();
    }
    s.timeMs = double(m_clock.nsecsElapsed()) / 1e6;
    return s;
}

void BrushScratchpad::beginStroke(const QPointF &pos, qreal pressure, const QPointF &tilt, bool hasTilt)
{
    if (!m_canvas)
        return;
    endStroke();
    ensureLayer();
    const BrushSettings settings = activeSettings();
    // Smoothing in screen pixels, as on the canvas at 100 % zoom.
    m_engine = std::make_unique<StrokeEngine>(settings, m_canvas->brushColor(), ++m_strokes,
                                              StrokeEngine::smoothingRadius(settings));
    m_before = TileSnapshot();
    m_clock.start();
    // The scratchpad sits in a scrolling panel: a drag here draws, it does not scroll.
    setKeepMouseGrab(true);
    setKeepTouchGrab(true);
    m_engine->begin(m_layer, sampleAt(pos, pressure, tilt, hasTilt), m_before);
    if (m_engine->usesTime())
        m_airbrush.start();
    update();
}

void BrushScratchpad::continueStroke(const QPointF &pos, qreal pressure, const QPointF &tilt, bool hasTilt)
{
    if (!m_engine)
        return;
    if (!m_engine->moveTo(m_layer, sampleAt(pos, pressure, tilt, hasTilt), m_before).isEmpty())
        update();
}

void BrushScratchpad::advance()
{
    if (!m_engine) {
        m_airbrush.stop();
        return;
    }
    if (!m_engine->advanceTime(m_layer, double(m_clock.nsecsElapsed()) / 1e6, m_before).isEmpty())
        update();
}

void BrushScratchpad::endStroke()
{
    m_airbrush.stop();
    setKeepMouseGrab(false);
    setKeepTouchGrab(false);
    if (!m_engine)
        return;
    m_engine->finish(m_layer, m_before);
    m_engine.reset();
    m_before = TileSnapshot(); // no undo on the scratchpad: Clear starts over
    update();
}

bool BrushScratchpad::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
        return handleTablet(static_cast<QTabletEvent *>(event));
    default:
        return QQuickPaintedItem::eventFilter(watched, event);
    }
}

bool BrushScratchpad::handleTablet(QTabletEvent *event)
{
    if (!isVisible() || !isEnabled())
        return false;
    const QPointF pos = mapFromScene(event->scenePosition());
    const auto *device = event->pointingDevice();
    const bool reportsPressure = device && device->capabilities().testFlag(QInputDevice::Capability::Pressure);
    const bool hasTilt = device && device->capabilities().testFlag(QInputDevice::Capability::XTilt)
        && device->capabilities().testFlag(QInputDevice::Capability::YTilt);
    const qreal pressure = effectivePressure(PointerKind::Pen, reportsPressure, event->pressure());
    const QPointF tilt(event->xTilt(), event->yTilt());
    m_inputHasPressure = reportsPressure;
    switch (event->type()) {
    case QEvent::TabletPress:
        if (!contains(pos) || event->button() != Qt::LeftButton)
            return false;
        m_penDown = true;
        m_sincePen.start();
        beginStroke(pos, pressure, tilt, hasTilt);
        event->accept();
        return true;
    case QEvent::TabletMove:
        if (!m_penDown)
            return false;
        m_sincePen.start();
        continueStroke(pos, pressure, tilt, hasTilt);
        event->accept();
        return true;
    case QEvent::TabletRelease:
        if (!m_penDown)
            return false;
        m_penDown = false;
        m_sincePen.start();
        endStroke();
        event->accept();
        return true;
    default:
        return false;
    }
}

void BrushScratchpad::mousePressEvent(QMouseEvent *event)
{
    if (m_penDown || (m_sincePen.isValid() && m_sincePen.elapsed() < PenMouseSuppressMs)) {
        event->accept();
        return;
    }
    m_inputHasPressure = false;
    beginStroke(event->position(), 1.0);
    event->accept();
}

void BrushScratchpad::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_penDown)
        continueStroke(event->position(), 1.0);
}

void BrushScratchpad::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_penDown)
        return;
    continueStroke(event->position(), 1.0);
    endStroke();
}

void BrushScratchpad::touchEvent(QTouchEvent *event)
{
    if (m_penDown || event->points().isEmpty()) {
        event->accept();
        return;
    }
    const QEventPoint &point = event->points().first();
    switch (event->type()) {
    case QEvent::TouchBegin:
        m_inputHasPressure = false; // touch never counts as pressure
        beginStroke(point.position(), 1.0);
        break;
    case QEvent::TouchUpdate:
        continueStroke(point.position(), 1.0);
        break;
    default: // TouchEnd, TouchCancel
        if (event->type() == QEvent::TouchEnd)
            continueStroke(point.position(), 1.0);
        endStroke();
        break;
    }
    event->accept();
}

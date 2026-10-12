#include "CanvasItem.h"

#include "ProjectFile.h"
#include "ViewRenderer.h"

#include <QCursor>
#include <QFileInfo>
#include <QPainter>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

constexpr qreal ZoomStep = 1.25;
// Ignore mouse events this long after a pen event: some platforms (notably Windows) also
// deliver OS-synthesized mouse events for pen input.
constexpr qint64 PenMouseSuppressMs = 400;

bool isPenOrTouchDevice(const QPointingDevice *device)
{
    if (!device)
        return false;
    switch (device->type()) {
    case QInputDevice::DeviceType::Stylus:
    case QInputDevice::DeviceType::Airbrush:
    case QInputDevice::DeviceType::Puck:
    case QInputDevice::DeviceType::TouchScreen:
        return true;
    default:
        return false;
    }
}

// Local paths for file: URLs; other URLs (Android content://) are passed to QFile as strings.
QString pathFromUrl(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

// Adds the extension to a local path chosen without one. Content URIs are left alone.
void ensureSuffix(QString &path, QUrl &url, const QString &suffix)
{
    if (!url.isLocalFile() || !QFileInfo(path).suffix().isEmpty())
        return;
    path += QLatin1Char('.') + suffix;
    url = QUrl::fromLocalFile(path);
}

} // namespace

CanvasItem::CanvasItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setAcceptTouchEvents(true);
    setFlag(ItemIsFocusScope, false);
    setFlag(ItemAcceptsInputMethod, false);
    setActiveFocusOnTab(true);
    setCursor(Qt::CrossCursor);
    setOpaquePainting(true);
    setFillColor(QColor::fromRgb(ViewRenderer::WorkspaceColor));
    m_inputStatus = QStringLiteral("Input: waiting");
    m_statsEnabled = qEnvironmentVariableIsSet("DRAWAPP_STATS");
    m_statsWindow.start();
    // Constructed with a parent already in a window: ItemSceneChange ran before this class existed.
    watchWindow(window());
}

void CanvasItem::setBrushSize(int size)
{
    size = std::clamp(size, MinBrushSize, MaxBrushSize);
    if (size == m_brushSize)
        return;
    m_brushSize = size;
    emit brushSizeChanged();
}

void CanvasItem::setBrushColor(const QColor &color)
{
    // Opaque only: dabs overlap, so a translucent color would build up within a stroke.
    QColor opaque = color;
    opaque.setAlpha(255);
    if (opaque == m_brushColor)
        return;
    m_brushColor = opaque;
    emit brushColorChanged();
}

void CanvasItem::setTool(Tool tool)
{
    if (tool == m_tool)
        return;
    m_tool = tool;
    emit toolChanged();
}

QString CanvasItem::documentTitle() const
{
    if (m_filePath.isEmpty())
        return QStringLiteral("Untitled");
    const QString name = m_fileUrl.isLocalFile() ? QFileInfo(m_filePath).fileName() : m_fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled") : name;
}

void CanvasItem::clearCanvas()
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    TileSnapshot before;
    m_document.clear(&before);
    commitEdit(std::move(before), QStringLiteral("Clear canvas"));
    update();
}

void CanvasItem::undo()
{
    // An edit in progress is committed first, so undo removes it as a whole.
    if (m_gesture == Gesture::Stroke)
        endStroke();
    if (!m_history.canUndo())
        return;
    updateDocumentRect(m_document.undo(m_history));
    emit historyChanged();
}

void CanvasItem::redo()
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    if (!m_history.canRedo())
        return;
    updateDocumentRect(m_document.redo(m_history));
    emit historyChanged();
}

void CanvasItem::commitEdit(TileSnapshot before, const QString &label)
{
    // Tiles touched but left identical (for example erasing transparent pixels) are not history.
    before.dropUnchanged(m_document.image());
    if (before.isEmpty())
        return;
    m_history.push(std::move(before), label);
    emit historyChanged();
}

void CanvasItem::replaceDocument(const QImage &image)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    m_gesture = Gesture::None;
    m_document = RasterDocument(image);
    m_history.clear();
    m_savedStateId = m_history.stateId();
    m_view.fit(QSizeF(m_document.size()), size());
    update();
    emit documentChanged();
    emit historyChanged();
    emit viewChanged();
}

void CanvasItem::newDocument()
{
    replaceDocument(RasterDocument().image());
    m_filePath.clear();
    m_fileUrl = QUrl();
    emit fileChanged();
}

QString CanvasItem::openFile(const QUrl &url)
{
    const QString path = pathFromUrl(url);
    // Decoded into a separate image first: a failed open leaves the current artwork untouched.
    ProjectFile::LoadResult result = ProjectFile::load(path);
    if (!result.ok())
        return result.error;
    replaceDocument(result.image);
    m_filePath = path;
    m_fileUrl = url;
    emit fileChanged();
    return {};
}

QString CanvasItem::saveFile()
{
    if (m_filePath.isEmpty())
        return QStringLiteral("Choose a file name first (Save As).");
    return saveTo(m_filePath, m_fileUrl);
}

QString CanvasItem::saveFileAs(const QUrl &url)
{
    QString path = pathFromUrl(url);
    QUrl target = url;
    ensureSuffix(path, target, QString::fromLatin1(ProjectFile::Extension));
    return saveTo(path, target);
}

QString CanvasItem::saveTo(const QString &path, const QUrl &url)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    const QString error = ProjectFile::save(m_document.image(), path);
    if (!error.isEmpty())
        return error;
    m_savedStateId = m_history.stateId();
    const bool fileChangedNow = path != m_filePath;
    m_filePath = path;
    m_fileUrl = url;
    emit historyChanged();
    if (fileChangedNow)
        emit fileChanged();
    return {};
}

QString CanvasItem::exportPng(const QUrl &url)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    QString path = pathFromUrl(url);
    QUrl target = url;
    ensureSuffix(path, target, QStringLiteral("png"));
    // Export does not change the project file or the unsaved-changes state.
    return ProjectFile::exportPng(m_document.image(), path);
}

void CanvasItem::zoomIn()
{
    m_view.zoomByAt(ZoomStep, boundingRect().center());
    viewModified();
}

void CanvasItem::zoomOut()
{
    m_view.zoomByAt(1.0 / ZoomStep, boundingRect().center());
    viewModified();
}

void CanvasItem::zoomToActualSize()
{
    m_view.center(QSizeF(m_document.size()), size());
    viewModified();
}

void CanvasItem::fitToView()
{
    m_view.fit(QSizeF(m_document.size()), size());
    viewModified();
}

void CanvasItem::paint(QPainter *painter)
{
    QElapsedTimer timer;
    if (m_statsEnabled)
        timer.start();

    const QRect exposed = painter->hasClipping() ? painter->clipBoundingRect().toAlignedRect()
                                                 : boundingRect().toAlignedRect();
    ViewRenderer::render(*painter, m_document.image(), m_view, exposed);

    if (m_statsEnabled) {
        const qint64 ns = timer.nsecsElapsed();
        ++m_statsPaints;
        m_statsPaintNs += ns;
        m_statsPaintMaxNs = std::max(m_statsPaintMaxNs, ns);
        reportStats();
    }
}

void CanvasItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    // Resizing only changes the view; the document keeps its pixels and the view keeps its
    // zoom and pan, apart from the first layout where the document is fitted.
    if (!m_viewInitialized && !newGeometry.size().isEmpty()) {
        m_viewInitialized = true;
        m_view.fit(QSizeF(m_document.size()), newGeometry.size());
        emit viewChanged();
    }
}

void CanvasItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange)
        watchWindow(value.window);
    QQuickPaintedItem::itemChange(change, value);
}

void CanvasItem::watchWindow(QQuickWindow *window)
{
    // QQuickItem has no tablet event handler; pen pressure only arrives at the window.
    if (window == m_watchedWindow)
        return;
    if (m_watchedWindow)
        m_watchedWindow->removeEventFilter(this);
    m_watchedWindow = window;
    if (window)
        window->installEventFilter(this);
}

bool CanvasItem::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
        return handleTabletEvent(static_cast<QTabletEvent *>(event));
    default:
        return QQuickPaintedItem::eventFilter(watched, event);
    }
}

bool CanvasItem::handleTabletEvent(QTabletEvent *event)
{
    m_sincePenEvent.start();
    const QPointF pos = mapFromScene(event->scenePosition());
    const bool reportsPressure = event->pointingDevice()
        && event->pointingDevice()->capabilities().testFlag(QInputDevice::Capability::Pressure);
    const qreal pressure = effectivePressure(PointerKind::Pen, reportsPressure, event->pressure());

    if (reportsPressure)
        setInputStatus(QStringLiteral("Input: pen, pressure %1").arg(event->pressure(), 0, 'f', 2));
    else
        setInputStatus(QStringLiteral("Input: pen, no pressure data (full size)"));

    switch (event->type()) {
    case QEvent::TabletPress: {
        if (!isEnabled() || !isVisible() || !contains(pos) || m_gesture != Gesture::None)
            return false;
        m_penDown = true;
        forceActiveFocus(Qt::MouseFocusReason);
        if (event->button() == Qt::LeftButton && !m_spaceHeld) {
            const bool eraserTip = event->pointerType() == QPointingDevice::PointerType::Eraser;
            beginStroke(pos, PointerKind::Pen, pressure, eraserTip);
        } else {
            // Barrel button (or Space) with the tip down pans.
            beginPan(pos);
        }
        event->accept();
        return true;
    }
    case QEvent::TabletMove:
        if (!m_penDown)
            return false; // hover
        if (m_gesture == Gesture::Stroke && m_strokeKind == PointerKind::Pen)
            continueStroke(pos, pressure);
        else if (m_gesture == Gesture::Pan)
            continuePan(pos);
        event->accept();
        return true;
    case QEvent::TabletRelease:
        if (!m_penDown)
            return false;
        m_penDown = false;
        if (m_gesture == Gesture::Stroke)
            endStroke();
        else if (m_gesture == Gesture::Pan)
            endPan();
        event->accept();
        return true;
    default:
        return false;
    }
}

bool CanvasItem::ignoreMouseEvent(const QMouseEvent *event) const
{
    if (m_penDown || isPenOrTouchDevice(event->pointingDevice()))
        return true;
    return m_sincePenEvent.isValid() && m_sincePenEvent.elapsed() < PenMouseSuppressMs;
}

void CanvasItem::mousePressEvent(QMouseEvent *event)
{
    if (ignoreMouseEvent(event) || m_gesture != Gesture::None) {
        event->accept();
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    setInputStatus(QStringLiteral("Input: mouse (no pressure, full size)"));
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld))
        beginPan(event->position());
    else if (event->button() == Qt::LeftButton)
        beginStroke(event->position(), PointerKind::Mouse, 1.0, false);
    event->accept();
}

void CanvasItem::mouseMoveEvent(QMouseEvent *event)
{
    if (ignoreMouseEvent(event))
        return;
    if (m_gesture == Gesture::Stroke && m_strokeKind == PointerKind::Mouse)
        continueStroke(event->position(), 1.0);
    else if (m_gesture == Gesture::Pan)
        continuePan(event->position());
}

void CanvasItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (ignoreMouseEvent(event))
        return;
    if (m_gesture == Gesture::Stroke && m_strokeKind == PointerKind::Mouse)
        endStroke();
    else if (m_gesture == Gesture::Pan)
        endPan();
}

void CanvasItem::mouseUngrabEvent()
{
    if (m_gesture == Gesture::Stroke && m_strokeKind == PointerKind::Mouse)
        endStroke();
    else if (m_gesture == Gesture::Pan && !m_penDown)
        endPan();
}

void CanvasItem::wheelEvent(QWheelEvent *event)
{
    const QPoint angle = event->angleDelta();
    if (angle.y() != 0) {
        // 120 units is one notch on a standard wheel: one zoom step per notch.
        m_view.zoomByAt(std::pow(ZoomStep, angle.y() / 120.0), event->position());
        viewModified();
    } else if (!event->pixelDelta().isNull()) {
        m_view.panBy(QPointF(event->pixelDelta()));
        viewModified();
    }
    event->accept();
}

void CanvasItem::touchEvent(QTouchEvent *event)
{
    QList<QEventPoint> active;
    for (const QEventPoint &point : event->points()) {
        if (point.state() != QEventPoint::State::Released)
            active.append(point);
    }

    if (event->type() == QEvent::TouchCancel || active.isEmpty()) {
        if (m_gesture == Gesture::Stroke && m_strokeKind == PointerKind::Touch)
            endStroke();
        else if (m_gesture == Gesture::Pinch)
            m_gesture = Gesture::None;
        event->accept();
        return;
    }

    if (active.size() >= 2) {
        const QPointF a = active.at(0).position();
        const QPointF b = active.at(1).position();
        const QPointF center = (a + b) / 2.0;
        const qreal distance = std::hypot(a.x() - b.x(), a.y() - b.y());
        if (m_gesture != Gesture::Pinch) {
            // A second finger turns a one-finger stroke into a view gesture. Marks already
            // made stay and become one undo step.
            if (m_gesture == Gesture::Stroke)
                endStroke();
            m_gesture = Gesture::Pinch;
        } else {
            m_view.panBy(center - m_lastPinchCenter);
            if (m_lastPinchDistance > 0 && distance > 0)
                m_view.zoomByAt(distance / m_lastPinchDistance, center);
            viewModified();
        }
        m_lastPinchCenter = center;
        m_lastPinchDistance = distance;
    } else if (m_penDown || (m_sincePenEvent.isValid() && m_sincePenEvent.elapsed() < PenMouseSuppressMs)) {
        // Palm or finger contact while the pen is in use: ignore.
    } else if (m_gesture == Gesture::None && event->type() == QEvent::TouchBegin) {
        setInputStatus(QStringLiteral("Input: touch (no pressure, full size)"));
        beginStroke(active.first().position(), PointerKind::Touch, 1.0, false);
    } else if (m_gesture == Gesture::Stroke && m_strokeKind == PointerKind::Touch) {
        continueStroke(active.first().position(), 1.0);
    }
    event->accept();
}

void CanvasItem::touchUngrabEvent()
{
    if (m_gesture == Gesture::Stroke && m_strokeKind == PointerKind::Touch)
        endStroke();
    else if (m_gesture == Gesture::Pinch)
        m_gesture = Gesture::None;
}

void CanvasItem::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QQuickPaintedItem::keyPressEvent(event);
}

void CanvasItem::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QQuickPaintedItem::keyReleaseEvent(event);
}

void CanvasItem::beginStroke(const QPointF &viewPos, PointerKind kind, qreal pressure, bool eraserTip)
{
    QElapsedTimer timer;
    if (m_statsEnabled)
        timer.start();

    m_gesture = Gesture::Stroke;
    m_strokeKind = kind;
    m_strokeMode = (eraserTip || m_tool == Eraser) ? BrushMode::Erase : BrushMode::Paint;
    m_strokeColor = m_brushColor;
    m_spacer.emplace(m_brushSize);
    m_spacer->begin({m_view.viewToDoc(viewPos), pressure}, m_pendingDabs);
    applyPendingDabs();

    if (m_statsEnabled) {
        const qint64 ns = timer.nsecsElapsed();
        ++m_statsInputs;
        m_statsInputNs += ns;
        m_statsInputMaxNs = std::max(m_statsInputMaxNs, ns);
    }
}

void CanvasItem::continueStroke(const QPointF &viewPos, qreal pressure)
{
    if (!m_spacer)
        return;
    QElapsedTimer timer;
    if (m_statsEnabled)
        timer.start();

    m_spacer->moveTo({m_view.viewToDoc(viewPos), pressure}, m_pendingDabs);
    applyPendingDabs();

    if (m_statsEnabled) {
        const qint64 ns = timer.nsecsElapsed();
        ++m_statsInputs;
        m_statsInputNs += ns;
        m_statsInputMaxNs = std::max(m_statsInputMaxNs, ns);
    }
}

void CanvasItem::endStroke()
{
    // However a stroke ends (release, lost grab, a second finger, an undo or save mid-stroke),
    // the marks it made become one undo step.
    if (m_gesture == Gesture::Stroke) {
        commitEdit(std::move(m_strokeBefore),
                   m_strokeMode == BrushMode::Erase ? QStringLiteral("Eraser stroke") : QStringLiteral("Brush stroke"));
        m_strokeBefore = TileSnapshot();
    }
    m_spacer.reset();
    m_pendingDabs.clear();
    m_gesture = Gesture::None;
}

void CanvasItem::beginPan(const QPointF &viewPos)
{
    m_gesture = Gesture::Pan;
    m_lastPanPos = viewPos;
    setCursor(Qt::ClosedHandCursor);
}

void CanvasItem::endPan()
{
    m_gesture = Gesture::None;
    setCursor(m_spaceHeld ? Qt::OpenHandCursor : Qt::CrossCursor);
}

void CanvasItem::continuePan(const QPointF &viewPos)
{
    m_view.panBy(viewPos - m_lastPanPos);
    m_lastPanPos = viewPos;
    viewModified();
}

void CanvasItem::applyPendingDabs()
{
    if (m_pendingDabs.empty())
        return;
    m_statsDabs += qint64(m_pendingDabs.size());
    updateDocumentRect(m_document.stampDabs(m_pendingDabs, m_strokeColor, m_strokeMode, &m_strokeBefore));
    m_pendingDabs.clear();
}

void CanvasItem::updateDocumentRect(const QRect &docRect)
{
    if (docRect.isEmpty())
        return;
    // Repaint only the view area covering the changed document pixels.
    update(m_view.docToView(QRectF(docRect)).toAlignedRect().adjusted(-1, -1, 1, 1));
}

void CanvasItem::viewModified()
{
    if (m_gesture != Gesture::Pan)
        setCursor(m_spaceHeld ? Qt::OpenHandCursor : Qt::CrossCursor);
    update();
    emit viewChanged();
}

void CanvasItem::setInputStatus(const QString &status)
{
    if (status == m_inputStatus)
        return;
    m_inputStatus = status;
    emit inputStatusChanged();
}

void CanvasItem::reportStats()
{
    if (m_statsWindow.elapsed() < 2000)
        return;
    const auto ms = [](qint64 ns) { return QString::number(ns / 1e6, 'f', 3); };
    qInfo().noquote() << QStringLiteral("[stats] paints=%1 avg=%2ms max=%3ms | input events=%4 avg=%5ms max=%6ms dabs=%7 | zoom=%8")
                             .arg(m_statsPaints)
                             .arg(ms(m_statsPaints ? m_statsPaintNs / m_statsPaints : 0), ms(m_statsPaintMaxNs))
                             .arg(m_statsInputs)
                             .arg(ms(m_statsInputs ? m_statsInputNs / m_statsInputs : 0), ms(m_statsInputMaxNs))
                             .arg(m_statsDabs)
                             .arg(m_view.zoom(), 0, 'f', 3);
    m_statsWindow.restart();
    m_statsPaints = 0;
    m_statsPaintNs = m_statsPaintMaxNs = 0;
    m_statsInputs = 0;
    m_statsInputNs = m_statsInputMaxNs = 0;
    m_statsDabs = 0;
}

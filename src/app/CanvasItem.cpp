#include "CanvasItem.h"

#include "ProjectFile.h"
#include "ViewRenderer.h"

#include <QCursor>
#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <limits>
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
    m_recoveryClock.start();
    if (m_statsEnabled) {
        // UI-thread responsiveness probe: how late a 5 ms timer fires.
        m_lagProbe.setTimerType(Qt::PreciseTimer);
        m_lagProbe.setInterval(5);
        connect(&m_lagProbe, &QTimer::timeout, this, [this] {
            const qint64 now = m_recoveryClock.elapsed();
            if (m_lagProbeLastMs >= 0)
                m_statsMaxLoopLagMs = std::max(m_statsMaxLoopLagMs, now - m_lagProbeLastMs - 5);
            m_lagProbeLastMs = now;
            reportStats();
        });
        m_lagProbe.start();
    }
    m_autosaveTimer.setInterval(m_autosaveIntervalMs);
    m_autosaveTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_autosaveTimer, &QTimer::timeout, this, &CanvasItem::autosaveNow);
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
    if (commitEdit(std::move(before), QStringLiteral("Clear canvas")))
        markPixelsChanged();
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
    markPixelsChanged();
    emit historyChanged();
}

void CanvasItem::redo()
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    if (!m_history.canRedo())
        return;
    updateDocumentRect(m_document.redo(m_history));
    markPixelsChanged();
    emit historyChanged();
}

bool CanvasItem::commitEdit(TileSnapshot before, const QString &label)
{
    // Tiles touched but left identical (for example erasing transparent pixels) are not history.
    before.dropUnchanged(m_document.image());
    if (before.isEmpty())
        return false;
    m_history.push(std::move(before), label);
    emit historyChanged();
    return true;
}

void CanvasItem::replaceDocument(const QImage &image)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    m_gesture = Gesture::None;
    // The previous document was saved or explicitly discarded: its recovery data goes too.
    discardRecoveryFile();
    m_document = RasterDocument(image);
    m_history.clear();
    m_savedStateId = m_history.stateId();
    ++m_documentId;
    ++m_pixelRevision;
    m_oldestUnrecoveredMs = -1;
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
        return error; // recovery data stays: the work is still only recoverable from there
    m_savedStateId = m_history.stateId();
    // The saved file now holds this state; recovery data would only duplicate it.
    discardRecoveryFile();
    m_oldestUnrecoveredMs = -1;
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
    const QRect changed = m_document.stampDabs(m_pendingDabs, m_strokeColor, m_strokeMode, &m_strokeBefore);
    if (!changed.isEmpty())
        markPixelsChanged();
    updateDocumentRect(changed);
    m_pendingDabs.clear();
}

// --- crash recovery ---

void CanvasItem::markPixelsChanged()
{
    ++m_pixelRevision;
    if (m_oldestUnrecoveredMs < 0)
        m_oldestUnrecoveredMs = m_recoveryClock.elapsed();
}

bool CanvasItem::needsRecovery() const
{
    // A stroke in progress counts before it is committed to history.
    return isModified() || (m_gesture == Gesture::Stroke && !m_strokeBefore.isEmpty());
}

RecoveryFile::Metadata CanvasItem::recoveryMetadata() const
{
    RecoveryFile::Metadata meta;
    meta.documentId = m_documentId;
    meta.revision = m_pixelRevision;
    meta.savedAt = QDateTime::currentDateTimeUtc();
    meta.originalPath = m_filePath;
    meta.originalUrl = m_fileUrl.toString();
    return meta;
}

void CanvasItem::setAutosaveInterval(int milliseconds)
{
    milliseconds = std::max(50, milliseconds);
    if (milliseconds == m_autosaveIntervalMs)
        return;
    m_autosaveIntervalMs = milliseconds;
    m_autosaveTimer.setInterval(milliseconds);
    emit autosaveIntervalChanged();
}

void CanvasItem::setRecoveryManager(RecoveryManager *manager)
{
    m_recovery = manager;
    if (!manager)
        return;
    connect(manager, &RecoveryManager::writeFinished, this, &CanvasItem::onRecoveryWriteFinished);
    const QString error = manager->start();
    if (!error.isEmpty()) {
        setRecoveryStatus(QStringLiteral("Crash recovery unavailable: %1").arg(error));
        return;
    }

    QStringList damaged;
    for (const RecoveryManager::Orphan &orphan : manager->findOrphans()) {
        if (orphan.contents.ok()) {
            m_orphans.append(orphan);
        } else {
            const QString aside = manager->setAside(orphan);
            damaged.append(aside.isEmpty() ? orphan.contents.error
                                           : QStringLiteral("%1 It was moved to \"%2\".").arg(orphan.contents.error, aside));
        }
    }
    if (!damaged.isEmpty())
        m_recoveryWarning = QStringLiteral("Recovery data from an earlier session could not be used.\n\n")
                            + damaged.join(QLatin1Char('\n'));
    m_autosaveTimer.start();
    emit recoveryChanged();
}

QString CanvasItem::recoveryDescription() const
{
    if (m_orphans.isEmpty())
        return {};
    const RecoveryFile::Contents &c = m_orphans.first().contents;
    const QString name = c.metadata.originalPath.isEmpty() ? QStringLiteral("Untitled")
                                                           : QFileInfo(c.metadata.originalPath).fileName();
    return QStringLiteral("DrawApp did not close normally. Unsaved artwork \"%1\" (%2 × %3 px) from %4 can be restored.")
        .arg(name)
        .arg(c.image.width())
        .arg(c.image.height())
        .arg(QLocale().toString(c.metadata.savedAt.toLocalTime(), QLocale::ShortFormat));
}

void CanvasItem::restoreRecovery()
{
    if (m_orphans.isEmpty() || !m_recovery)
        return;
    const RecoveryManager::Orphan orphan = m_orphans.takeFirst();
    m_orphans.clear(); // further orphans stay on disk for the next start
    replaceDocument(orphan.contents.image);
    m_filePath = orphan.contents.metadata.originalPath;
    m_fileUrl = orphan.contents.metadata.originalUrl.isEmpty() ? (m_filePath.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_filePath))
                                                               : QUrl(orphan.contents.metadata.originalUrl);
    // Restored artwork was never saved: no history state equals the "saved" id.
    m_savedStateId = std::numeric_limits<quint64>::max();
    const QString error = m_recovery->adopt(orphan);
    if (error.isEmpty()) {
        // The adopted file already holds exactly this state.
        m_requestedDocumentId = m_documentId;
        m_requestedRevision = m_pixelRevision;
        setRecoveryStatus(QStringLiteral("Restored unsaved artwork"));
    } else {
        m_oldestUnrecoveredMs = m_recoveryClock.elapsed();
        setRecoveryStatus(error);
    }
    emit historyChanged();
    emit fileChanged();
    emit recoveryChanged();
}

void CanvasItem::discardRecovery()
{
    if (m_orphans.isEmpty() || !m_recovery)
        return;
    m_recovery->removeOrphan(m_orphans.takeFirst());
    emit recoveryChanged();
}

void CanvasItem::postponeRecovery()
{
    m_orphans.clear();
    emit recoveryChanged();
}

void CanvasItem::clearRecoveryWarning()
{
    m_recoveryWarning.clear();
    emit recoveryChanged();
}

void CanvasItem::discardForClose()
{
    m_discardedForClose = true;
    m_autosaveTimer.stop();
    discardRecoveryFile();
}

void CanvasItem::discardRecoveryFile()
{
    if (!m_recovery || !m_recovery->isStarted())
        return;
    m_recovery->discard(); // also makes any in-flight write stale
    m_requestedDocumentId = 0;
    m_requestedRevision = 0;
    if (!m_recoveryStatus.isEmpty() && !m_recoveryStatus.startsWith(QLatin1String("Crash recovery unavailable")))
        setRecoveryStatus(QString());
}

void CanvasItem::autosaveNow()
{
    if (!m_recovery || !m_recovery->isStarted() || m_discardedForClose)
        return;
    const qint64 now = m_recoveryClock.elapsed();
    if (m_statsEnabled && m_lastTickMs >= 0)
        qInfo().noquote() << QStringLiteral("[recovery] tick after %1 ms").arg(now - m_lastTickMs);
    m_lastTickMs = now;

    if (!needsRecovery()) {
        if (m_recovery->hasRecoveryFile() || m_recovery->isWriting())
            discardRecoveryFile();
        m_oldestUnrecoveredMs = -1;
        return;
    }
    if (m_documentId == m_requestedDocumentId && m_pixelRevision == m_requestedRevision)
        return; // already written or being written
    if (m_recovery->isWriting()) {
        m_autosaveTickMissed = true; // run again as soon as the current write ends
        return;
    }

    // Consistent snapshot on the UI thread; the worker only ever sees this private copy.
    QElapsedTimer copyTimer;
    copyTimer.start();
    const QImage snapshot = m_document.image().copy();
    const qint64 copyUs = copyTimer.nsecsElapsed() / 1000;
    if (!m_recovery->requestWrite(snapshot, recoveryMetadata()))
        return;
    m_requestedDocumentId = m_documentId;
    m_requestedRevision = m_pixelRevision;
    m_inFlightOldestMs = m_oldestUnrecoveredMs;
    m_oldestUnrecoveredMs = -1;
    if (m_statsEnabled)
        qInfo().noquote() << QStringLiteral("[recovery] snapshot rev %1 copied on UI thread in %2 ms")
                                 .arg(m_pixelRevision)
                                 .arg(copyUs / 1000.0, 0, 'f', 3);
}

void CanvasItem::onRecoveryWriteFinished(bool committed, bool stale, const QString &error, quint64 documentId,
                                         quint64 revision, qint64 workerMs)
{
    const qint64 now = m_recoveryClock.elapsed();
    if (committed && documentId == m_documentId) {
        if (m_inFlightOldestMs >= 0) {
            const qint64 lag = now - m_inFlightOldestMs;
            m_maxRecoveryLagMs = std::max(m_maxRecoveryLagMs, lag);
            if (m_statsEnabled)
                qInfo().noquote() << QStringLiteral("[recovery] committed rev %1, worker %2 ms, oldest change was %3 ms old "
                                                    "(max so far %4 ms)")
                                         .arg(revision).arg(workerMs).arg(lag).arg(m_maxRecoveryLagMs);
        }
        setRecoveryStatus(QStringLiteral("Recovery data saved %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
    } else if (!error.isEmpty()) {
        // The previous recovery file is intact. Retry at the next tick, and keep measuring the
        // loss window from the older change.
        if (m_inFlightOldestMs >= 0 && (m_oldestUnrecoveredMs < 0 || m_inFlightOldestMs < m_oldestUnrecoveredMs))
            m_oldestUnrecoveredMs = m_inFlightOldestMs;
        m_requestedDocumentId = 0;
        m_requestedRevision = 0;
        setRecoveryStatus(error);
        if (m_statsEnabled)
            qInfo().noquote() << QStringLiteral("[recovery] write failed: %1").arg(error);
    } else if (stale && m_statsEnabled) {
        qInfo().noquote() << QStringLiteral("[recovery] write of rev %1 skipped (stale)").arg(revision);
    }
    m_inFlightOldestMs = -1;
    if (m_autosaveTickMissed) {
        m_autosaveTickMissed = false;
        autosaveNow();
    }
}

void CanvasItem::shutdownRecovery()
{
    if (!m_recovery || !m_recovery->isStarted())
        return;
    m_autosaveTimer.stop();
    if (needsRecovery() && !m_discardedForClose) {
        // Quitting without the close prompt (for example the OS ending the session): keep the
        // latest state recoverable instead of deleting it.
        if (m_gesture == Gesture::Stroke)
            endStroke();
        m_recovery->writeNow(m_document.image().copy(), recoveryMetadata());
        m_recovery->releaseKeepingFile();
    } else {
        m_recovery->finishCleanly();
    }
}

void CanvasItem::setRecoveryStatus(const QString &status)
{
    if (status == m_recoveryStatus)
        return;
    m_recoveryStatus = status;
    emit recoveryStatusChanged();
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
    qInfo().noquote() << QStringLiteral("[stats] paints=%1 avg=%2ms max=%3ms | input events=%4 avg=%5ms max=%6ms dabs=%7 | "
                                        "zoom=%8 | max UI loop lag=%9ms")
                             .arg(m_statsPaints)
                             .arg(ms(m_statsPaints ? m_statsPaintNs / m_statsPaints : 0), ms(m_statsPaintMaxNs))
                             .arg(m_statsInputs)
                             .arg(ms(m_statsInputs ? m_statsInputNs / m_statsInputs : 0), ms(m_statsInputMaxNs))
                             .arg(m_statsDabs)
                             .arg(m_view.zoom(), 0, 'f', 3)
                             .arg(m_statsMaxLoopLagMs);
    m_statsMaxLoopLagMs = 0;
    m_statsWindow.restart();
    m_statsPaints = 0;
    m_statsPaintNs = m_statsPaintMaxNs = 0;
    m_statsInputs = 0;
    m_statsInputNs = m_statsInputMaxNs = 0;
    m_statsDabs = 0;
}

#include "CanvasItem.h"

#include "ProjectFile.h"
#include "ViewRenderer.h"

#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QLocale>
#include <limits>
#include <QPainter>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QVariantMap>
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
    connect(&m_io, &ProjectIo::saveFinished, this, &CanvasItem::onSaveFinished);
    connect(&m_io, &ProjectIo::openFinished, this, &CanvasItem::onOpenFinished);
    connect(&m_io, &ProjectIo::exportFinished, this, &CanvasItem::onExportFinished);
    connect(&m_brushes, &BrushLibrary::currentChanged, this, [this] {
        emit brushSizeChanged();
        emit brushSettingsChanged();
    });
    connect(&m_brushes, &BrushLibrary::toolRequested, this, [this](bool eraser) { setTool(eraser ? Eraser : Brush); });
    // About one display frame: an airbrush held still keeps spraying at its rate.
    m_airbrushTimer.setInterval(16);
    m_airbrushTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_airbrushTimer, &QTimer::timeout, this, &CanvasItem::advanceAirbrush);
    // Constructed with a parent already in a window: ItemSceneChange ran before this class existed.
    watchWindow(window());
}

int CanvasItem::brushSize() const
{
    return int(std::lround(strokeSettings(m_tool == Eraser).size));
}

qreal CanvasItem::brushOpacity() const
{
    return strokeSettings(m_tool == Eraser).opacity;
}

qreal CanvasItem::pressureMinSize() const
{
    const PressureMapping &m = strokeSettings(m_tool == Eraser).pressureSize;
    return m.enabled ? m.minimum : 1.0;
}

qreal CanvasItem::pressureFirmness() const
{
    const PressureMapping &m = strokeSettings(m_tool == Eraser).pressureSize;
    return std::log(std::clamp(m.gamma, PressureResponse::MinGamma, PressureResponse::MaxGamma))
         / std::log(PressureResponse::MaxGamma);
}

int CanvasItem::smoothing() const
{
    return strokeSettings(m_tool == Eraser).smoothing;
}

void CanvasItem::setBrushSize(int size)
{
    m_brushes.setValue(QStringLiteral("size"), std::clamp(size, MinBrushSize, MaxBrushSize));
}

void CanvasItem::setBrushOpacity(qreal opacity)
{
    m_brushes.setValue(QStringLiteral("opacity"), std::clamp(opacity, MinBrushOpacity, 1.0));
}

void CanvasItem::setPressureMinSize(qreal fraction)
{
    m_brushes.setValue(QStringLiteral("pressureSize.enabled"), true);
    m_brushes.setValue(QStringLiteral("pressureSize.minimum"), std::clamp(fraction, 0.0, 1.0));
}

void CanvasItem::setPressureFirmness(qreal firmness)
{
    m_brushes.setValue(QStringLiteral("pressureSize.gamma"),
                       PressureResponse::gammaForFirmness(std::clamp(firmness, -1.0, 1.0)));
}

void CanvasItem::setSmoothing(int smoothing)
{
    m_brushes.setValue(QStringLiteral("smoothing"), std::clamp(smoothing, 0, 100));
}

void CanvasItem::setBrushColor(const QColor &color)
{
    // The color is opaque; transparency comes from brushOpacity, applied once per stroke.
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
    m_brushes.setEraserSlot(tool == Eraser);
    emit toolChanged();
    emit brushSizeChanged();
    emit brushSettingsChanged();
}

QString CanvasItem::documentTitle() const
{
    return m_fileName.isEmpty() ? QStringLiteral("Untitled") : m_fileName;
}

// --- history ---

void CanvasItem::undo()
{
    // An edit in progress is committed first, so undo removes it as a whole.
    if (m_gesture == Gesture::Stroke)
        endStroke();
    if (!m_history.canUndo())
        return;
    applyHistoryResult(m_history.undo(m_document));
}

void CanvasItem::redo()
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    if (!m_history.canRedo())
        return;
    applyHistoryResult(m_history.redo(m_document));
}

void CanvasItem::applyHistoryResult(const UndoCommand::Result &result)
{
    if (result.activate)
        m_document.setActiveLayer(result.activate);
    if (!result.dirty.isEmpty()) {
        m_document.refreshComposite(result.dirty);
        updateDocumentRect(result.dirty);
    }
    markContentChanged();
    emit historyChanged();
    if (result.structure || result.activate)
        emit layersChanged();
}

bool CanvasItem::commitPixels(quint64 layerId, TileSnapshot before, const QString &label)
{
    const int index = m_document.indexOf(layerId);
    if (index < 0)
        return false;
    // Tiles touched but left identical (for example erasing transparent pixels) are not history.
    before.dropUnchanged(m_document.layer(index).image);
    if (before.isEmpty())
        return false;
    pushCommand(UndoCommand::pixels(layerId, std::move(before), label));
    return true;
}

void CanvasItem::pushCommand(UndoCommand command)
{
    m_history.push(std::move(command));
    emit historyChanged();
}

// --- layers ---

QVariantList CanvasItem::layers() const
{
    QVariantList list;
    for (int i = m_document.count() - 1; i >= 0; --i) {
        const LayerData &layer = m_document.layer(i);
        QVariantMap row;
        row.insert(QStringLiteral("id"), int(layer.id));
        row.insert(QStringLiteral("name"), layer.name);
        row.insert(QStringLiteral("visible"), layer.visible);
        row.insert(QStringLiteral("opacity"), layer.opacity);
        row.insert(QStringLiteral("locked"), layer.locked);
        row.insert(QStringLiteral("active"), layer.id == m_document.activeLayerId());
        list.append(row);
    }
    return list;
}

void CanvasItem::setActiveLayer(int id)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    if (quint64(id) == m_document.activeLayerId() || m_document.indexOf(quint64(id)) < 0)
        return;
    m_document.setActiveLayer(quint64(id)); // selection only: not an edit, not undoable
    emit layersChanged();
}

QString CanvasItem::activeLayerBlockReason() const
{
    const int index = m_document.activeIndex();
    if (index < 0)
        return QStringLiteral("There is no active layer.");
    const LayerData &layer = m_document.layer(index);
    if (!layer.visible)
        return QStringLiteral("Can't paint: layer \"%1\" is hidden. Show it or choose another layer.").arg(layer.name);
    if (layer.locked)
        return QStringLiteral("Can't paint: layer \"%1\" is locked. Unlock it or choose another layer.").arg(layer.name);
    return {};
}

void CanvasItem::addLayer()
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    if (m_document.count() >= maxLayers()) {
        emit notice(QStringLiteral("This canvas can have at most %1 layers.").arg(maxLayers()));
        return;
    }
    LayerData layer;
    layer.id = m_document.allocateId();
    layer.name = m_document.nextLayerName();
    layer.image = RasterDocument::blank(m_document.size());
    const int index = m_document.activeIndex() + 1; // above the active layer
    const quint64 id = layer.id;
    m_document.insertLayer(index, std::move(layer));
    m_document.setActiveLayer(id);
    pushCommand(UndoCommand::layerInserted(id, index, QStringLiteral("Add layer")));
    markContentChanged();
    emit layersChanged();
}

void CanvasItem::duplicateLayer(int id)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    const int source = m_document.indexOf(quint64(id));
    if (source < 0)
        return;
    if (m_document.count() >= maxLayers()) {
        emit notice(QStringLiteral("This canvas can have at most %1 layers.").arg(maxLayers()));
        return;
    }
    LayerData copy = m_document.layer(source); // pixels shared until either layer is edited
    copy.id = m_document.allocateId();
    copy.name = (copy.name + QStringLiteral(" copy")).left(DocumentLimits::MaxNameLength);
    const quint64 newId = copy.id;
    m_document.insertLayer(source + 1, std::move(copy));
    m_document.setActiveLayer(newId);
    m_document.refreshComposite();
    update();
    pushCommand(UndoCommand::layerInserted(newId, source + 1, QStringLiteral("Duplicate layer")));
    markContentChanged();
    emit layersChanged();
}

void CanvasItem::deleteLayer(int id)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    const int index = m_document.indexOf(quint64(id));
    if (index < 0)
        return;
    if (m_document.count() <= 1) {
        emit notice(QStringLiteral("A drawing needs at least one layer."));
        return;
    }
    LayerData removed = m_document.takeLayer(index); // the layer below becomes active if needed
    m_document.refreshComposite();
    update();
    pushCommand(UndoCommand::layerRemoved(std::move(removed), index, QStringLiteral("Delete layer")));
    markContentChanged();
    emit layersChanged();
}

void CanvasItem::moveLayerUp(int id)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    const int index = m_document.indexOf(quint64(id));
    if (index < 0 || index >= m_document.count() - 1)
        return;
    m_document.moveLayer(index, index + 1);
    m_document.refreshComposite();
    update();
    pushCommand(UndoCommand::moved(quint64(id), index, QStringLiteral("Move layer up")));
    markContentChanged();
    emit layersChanged();
}

void CanvasItem::moveLayerDown(int id)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    const int index = m_document.indexOf(quint64(id));
    if (index <= 0)
        return;
    m_document.moveLayer(index, index - 1);
    m_document.refreshComposite();
    update();
    pushCommand(UndoCommand::moved(quint64(id), index, QStringLiteral("Move layer down")));
    markContentChanged();
    emit layersChanged();
}

void CanvasItem::setLayerProperties(int id, const LayerProperties &props, const QString &label)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    const int index = m_document.indexOf(quint64(id));
    if (index < 0)
        return;
    LayerData &layer = m_document.layerRef(index);
    const LayerProperties before{layer.name, layer.visible, layer.opacity, layer.locked};
    if (before.name == props.name && before.visible == props.visible && before.opacity == props.opacity
        && before.locked == props.locked)
        return;
    const bool pixelsChange = before.visible != props.visible || before.opacity != props.opacity;
    layer.name = props.name;
    layer.visible = props.visible;
    layer.opacity = props.opacity;
    layer.locked = props.locked;
    if (pixelsChange) {
        m_document.refreshComposite();
        update();
    }
    pushCommand(UndoCommand::properties(quint64(id), before, label));
    markContentChanged();
    emit layersChanged();
}

void CanvasItem::renameLayer(int id, const QString &name)
{
    const int index = m_document.indexOf(quint64(id));
    const QString trimmed = name.trimmed().left(DocumentLimits::MaxNameLength);
    if (index < 0 || trimmed.isEmpty())
        return;
    LayerData layer = m_document.layer(index);
    setLayerProperties(id, {trimmed, layer.visible, layer.opacity, layer.locked}, QStringLiteral("Rename layer"));
}

void CanvasItem::setLayerVisible(int id, bool visible)
{
    const int index = m_document.indexOf(quint64(id));
    if (index < 0)
        return;
    const LayerData &layer = m_document.layer(index);
    setLayerProperties(id, {layer.name, visible, layer.opacity, layer.locked},
                       visible ? QStringLiteral("Show layer") : QStringLiteral("Hide layer"));
}

void CanvasItem::setLayerLocked(int id, bool locked)
{
    const int index = m_document.indexOf(quint64(id));
    if (index < 0)
        return;
    const LayerData &layer = m_document.layer(index);
    setLayerProperties(id, {layer.name, layer.visible, layer.opacity, locked},
                       locked ? QStringLiteral("Lock layer") : QStringLiteral("Unlock layer"));
}

void CanvasItem::previewLayerOpacity(int id, qreal opacity)
{
    const int index = m_document.indexOf(quint64(id));
    if (index < 0)
        return;
    opacity = std::clamp(opacity, 0.0, 1.0);
    LayerData &layer = m_document.layerRef(index);
    if (m_opacityDragLayerId != quint64(id)) {
        // A new drag: remember where it started so the whole drag becomes one undo step.
        if (m_opacityDragLayerId != 0)
            commitLayerOpacity(int(m_opacityDragLayerId), m_document.layer(m_document.indexOf(m_opacityDragLayerId)).opacity);
        m_opacityDragLayerId = quint64(id);
        m_opacityDragStart = layer.opacity;
    }
    if (layer.opacity == opacity)
        return;
    layer.opacity = opacity;
    m_document.refreshComposite();
    update();
    markContentChanged(); // the layer list is refreshed when the drag is committed
}

void CanvasItem::commitLayerOpacity(int id, qreal opacity)
{
    const int index = m_document.indexOf(quint64(id));
    if (index < 0)
        return;
    opacity = std::clamp(opacity, 0.0, 1.0);
    LayerData &layer = m_document.layerRef(index);
    const bool wasDragging = m_opacityDragLayerId == quint64(id);
    if (wasDragging) {
        // Put the start value back so the recorded "before" is where the drag began.
        layer.opacity = m_opacityDragStart;
        m_opacityDragLayerId = 0;
    }
    setLayerProperties(id, {layer.name, layer.visible, opacity, layer.locked}, QStringLiteral("Change layer opacity"));
    if (wasDragging) {
        // The composite may still show a preview value if the drag ended where it started.
        m_document.refreshComposite();
        update();
        emit layersChanged();
    }
}

void CanvasItem::clearActiveLayer()
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    const QString reason = activeLayerBlockReason();
    if (!reason.isEmpty()) {
        emit notice(QString(reason).replace(QStringLiteral("Can't paint"), QStringLiteral("Can't clear")));
        return;
    }
    const int index = m_document.activeIndex();
    TileSnapshot before;
    const QRect changed = RasterDocument::clear(m_document.pixels(index), &before);
    if (commitPixels(m_document.activeLayerId(), std::move(before), QStringLiteral("Clear layer"))) {
        m_document.refreshComposite(changed);
        markContentChanged();
        update();
    }
}

// --- documents and files ---

void CanvasItem::replaceDocument(const LayeredDocument &document)
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    m_gesture = Gesture::None;
    m_opacityDragLayerId = 0;
    // The previous document was saved or explicitly discarded: its recovery data goes too.
    discardRecoveryFile();
    m_document = document;
    m_history.clear();
    m_savedStateId = m_history.stateId();
    ++m_documentId;
    ++m_contentRevision;
    m_oldestUnrecoveredMs = -1;
    m_view.fit(QSizeF(m_document.size()), size());
    update();
    emit documentChanged();
    emit historyChanged();
    emit viewChanged();
    emit layersChanged();
}

void CanvasItem::newDocument()
{
    replaceDocument(LayeredDocument());
    setFile({}, {});
    emit fileChanged();
}

void CanvasItem::setFile(const QString &path, const QUrl &url, const QString &knownName)
{
    m_filePath = path;
    m_fileUrl = url;
    // Looked up once: for an Android content:// URI, QFileInfo asks the document provider for the
    // display name (the URI's last segment is often only a document id), which it can no longer do
    // once the document is gone.
    m_fileName = path.isEmpty() ? QString() : !knownName.isEmpty() ? knownName : QFileInfo(path).fileName();
}

QString CanvasItem::fileBusyText() const
{
    if (m_save.id)
        return m_saveQueued ? QStringLiteral("Saving (another save queued)…") : QStringLiteral("Saving…");
    if (m_open.id)
        return QStringLiteral("Opening…");
    if (m_exportId)
        return QStringLiteral("Exporting PNG…");
    return {};
}

QString CanvasItem::openFile(const QUrl &url)
{
    if (fileBusy())
        return QStringLiteral("Wait until the current save or open has finished.");
    if (m_conflict)
        return QStringLiteral("Decide first what to do with the file that was just opened.");
    if (m_pendingOverwrite)
        return QStringLiteral("Decide first whether to overwrite \"%1\".").arg(overwriteTarget());
    const QString path = pathFromUrl(url);
    // Decoded into a separate DocumentData on the worker; the open document is only replaced when
    // that succeeds (onOpenFinished).
    const quint64 id = m_io.open(path);
    if (!id)
        return QStringLiteral("Wait until the current save or open has finished.");
    m_open = {id, path, url, m_documentId, m_contentRevision};
    emit fileBusyChanged();
    return {};
}

void CanvasItem::onOpenFinished(quint64 id, const QString &path, const ProjectFile::LoadResult &result, qint64 workerMs)
{
    if (id != m_open.id)
        return;
    const OpenRequest request = m_open;
    m_open = {};
    emit fileBusyChanged();
    if (m_statsEnabled)
        qInfo().noquote() << QStringLiteral("[file] open finished in %1 ms on the worker").arg(workerMs);
    if (!result.ok()) {
        emit openFinished(false, result.error); // the open document was never touched
        return;
    }
    if (m_documentId != request.documentId || m_contentRevision != request.revision) {
        // The drawing changed while the file was loading: never drop those edits silently.
        m_conflict = OpenConflict{result.document, path, request.url};
        emit openConflict();
        return;
    }
    QElapsedTimer applyTimer;
    applyTimer.start();
    applyOpened(result.document, path, request.url);
    if (m_statsEnabled)
        qInfo().noquote() << QStringLiteral("[file] replacing the document on the UI thread took %1 ms (%2 layers)")
                                 .arg(applyTimer.nsecsElapsed() / 1e6, 0, 'f', 2)
                                 .arg(result.document.layers.size());
    emit openFinished(true, {});
}

void CanvasItem::resolveOpenConflict(bool useOpenedFile)
{
    if (!m_conflict)
        return;
    const OpenConflict conflict = std::move(*m_conflict);
    m_conflict.reset();
    if (useOpenedFile) {
        applyOpened(conflict.document, conflict.path, conflict.url);
        emit openFinished(true, {});
    } else {
        emit openFinished(false, {}); // cancelled: the current drawing stays as it is
    }
}

void CanvasItem::applyOpened(const DocumentData &document, const QString &path, const QUrl &url)
{
    replaceDocument(LayeredDocument(document));
    setFile(path, url);
    emit fileChanged();
}

QString CanvasItem::saveFile()
{
    if (m_filePath.isEmpty())
        return QStringLiteral("Choose a file name first (Save As).");
    return startSave(m_filePath, m_fileUrl);
}

QString CanvasItem::saveFileAs(const QUrl &url)
{
    QString path = pathFromUrl(url);
    QUrl target = url;
    ensureSuffix(path, target, QString::fromLatin1(ProjectFile::Extension));
    return startSave(path, target);
}

QString CanvasItem::startSave(const QString &path, const QUrl &url)
{
    if (m_open.id)
        return QStringLiteral("Wait until the file has finished opening.");
    if (m_exportId)
        return QStringLiteral("Wait until the PNG export has finished.");
    if (m_pendingOverwrite)
        return QStringLiteral("Decide first whether to overwrite \"%1\".").arg(overwriteTarget());
    if (m_save.id) {
        if (path == m_save.path) {
            // Saved again after the running save, from a newer snapshot: newer data always lands last.
            m_saveQueued = true;
            emit fileBusyChanged();
            return {};
        }
        return QStringLiteral("Another save is still running. Try again when it has finished.");
    }
    if (m_gesture == Gesture::Stroke)
        endStroke();
    if (m_opacityDragLayerId != 0)
        commitLayerOpacity(int(m_opacityDragLayerId), m_document.layer(m_document.indexOf(m_opacityDragLayerId)).opacity);

    // Immutable snapshot (copy-on-write images); editing continues while the worker writes it.
    SafeSave::Options options;
    if (SafeSave::isProviderPath(path)) {
        options.allowOverwrite = m_overwriteConfirmed.contains(path);
        options.localCopy = makeLocalCopy();
        if (path == m_filePath)
            options.name = documentTitle(); // still known if the provider has lost the document
    }
    const quint64 id = m_io.save(m_document.snapshot(), path, options);
    m_firstEditAfterSnapshot = true;
    if (!id)
        return QStringLiteral("Another file operation is still running.");
    m_save = {id, path, url, m_history.stateId(), m_documentId};
    emit fileBusyChanged();
    return {};
}

QString CanvasItem::stagingDirectory() const
{
    if (!m_stagingDirectory.isEmpty())
        return m_stagingDirectory;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("staging"));
}

SafeSave::LocalCopy CanvasItem::makeLocalCopy()
{
    SafeSave::LocalCopy copy;
    if (m_recovery && m_recovery->isStarted()) {
        // The crash-recovery file receives exactly the state being saved before the destination
        // is touched. It is deleted only once that state is saved and nothing newer needs it
        // (onSaveFinished), so after a crash during the write the next start offers it.
        RecoveryManager *recovery = m_recovery;
        const RecoveryManager::WriteToken token = m_recovery->reserveWrite();
        const RecoveryFile::Metadata metadata = recoveryMetadata();
        copy.keep = [recovery, token, metadata](const QByteArray &project) {
            return recovery->writeProject(token, project, metadata);
        };
        return copy;
    }
    // Without crash recovery: a plain project file in private storage, kept if the save fails.
    const QString file = QDir(stagingDirectory())
                             .filePath(QStringLiteral("unsaved-%1.drawapp")
                                           .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
    copy.keep = [file](const QByteArray &project) -> QString {
        if (!QDir().mkpath(QFileInfo(file).absolutePath()))
            return QStringLiteral("the folder for it could not be created");
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(project) != project.size() || !out.commit())
            return out.errorString();
        QFile check(file);
        if (!check.open(QIODevice::ReadOnly) || check.readAll() != project)
            return QStringLiteral("it could not be read back correctly");
        return {};
    };
    copy.release = [file] { QFile::remove(file); };
    return copy;
}

QString CanvasItem::overwriteTarget() const
{
    return m_pendingOverwrite ? SafeSave::displayName(m_pendingOverwrite->path) : QString();
}

QString CanvasItem::confirmOverwrite()
{
    if (!m_pendingOverwrite)
        return QStringLiteral("There is nothing waiting to be overwritten.");
    const PendingOverwrite pending = *m_pendingOverwrite;
    m_pendingOverwrite.reset();
    emit overwriteConfirmationChanged();
    m_overwriteConfirmed.insert(pending.path);
    return startSave(pending.path, pending.url);
}

void CanvasItem::cancelOverwrite()
{
    if (!m_pendingOverwrite)
        return;
    m_pendingOverwrite.reset();
    emit overwriteConfirmationChanged();
    emit saveFinished(false, {}); // cancelled: nothing was written
}

void CanvasItem::onSaveFinished(quint64 id, const QString &path, const SafeSave::Result &result, qint64 workerMs)
{
    Q_UNUSED(path);
    if (id != m_save.id)
        return;
    const SaveRequest done = m_save;
    const bool queued = m_saveQueued;
    m_save = {};
    m_saveQueued = false;
    if (m_statsEnabled || result.viaProvider)
        qInfo().noquote() << QStringLiteral("[file] save %1 in %2 ms on the worker%3")
                                 .arg(result.ok() ? QStringLiteral("finished")
                                      : result.status == SafeSave::Status::NeedsOverwriteConfirmation
                                          ? QStringLiteral("waits for overwrite confirmation")
                                          : QStringLiteral("failed"))
                                 .arg(workerMs)
                                 .arg(result.probe.isEmpty() ? QString() : QStringLiteral("; provider: ") + result.probe);
    if (result.status == SafeSave::Status::NeedsOverwriteConfirmation) {
        // Nothing was written. The save resumes or ends with the user's decision.
        m_pendingOverwrite = PendingOverwrite{done.path, done.url, result.probe};
        emit fileBusyChanged();
        emit overwriteConfirmationChanged();
        emit overwriteConfirmationNeeded();
        return;
    }
    if (!result.ok()) {
        // The path is unchanged and a queued save is dropped. A local file is intact (atomic
        // replace). A provider document may have been partly written: then the drawing no longer
        // matches any saved file, so it is marked unsaved even if it was unchanged, which also
        // keeps its recovery data.
        if (result.destinationTouched && done.documentId == m_documentId && done.path == m_filePath) {
            m_savedStateId = std::numeric_limits<quint64>::max();
            emit historyChanged();
        }
        emit fileBusyChanged();
        emit saveFinished(false, result.error);
        return;
    }
    if (done.documentId == m_documentId) {
        // Only the state captured in the snapshot is saved; later edits keep the document modified.
        m_savedStateId = done.stateId;
        const bool pathChanged = done.path != m_filePath;
        if (pathChanged)
            setFile(done.path, done.url);
        if (!needsRecovery()) {
            discardRecoveryFile();
            m_oldestUnrecoveredMs = -1;
        }
        emit historyChanged();
        if (pathChanged)
            emit fileChanged();
        if (queued && isModified())
            startSave(m_filePath, m_fileUrl);
    }
    emit fileBusyChanged();
    emit saveFinished(true, {});
}

QString CanvasItem::exportPng(const QUrl &url)
{
    if (fileBusy() || m_pendingOverwrite)
        return QStringLiteral("Wait until the current file operation has finished.");
    if (m_gesture == Gesture::Stroke)
        endStroke();
    QString path = pathFromUrl(url);
    QUrl target = url;
    ensureSuffix(path, target, QStringLiteral("png"));
    // Composited and encoded on the worker from a snapshot. Export does not change the project
    // file or the unsaved-changes state. An existing PNG at a provider location is overwritten
    // without asking: the drawing itself is never at risk from an export.
    SafeSave::Options options;
    options.allowOverwrite = true;
    const quint64 id = m_io.exportPng(m_document.snapshot(), path, options);
    if (!id)
        return QStringLiteral("Wait until the current file operation has finished.");
    m_exportId = id;
    m_firstEditAfterSnapshot = true;
    emit fileBusyChanged();
    return {};
}

void CanvasItem::onExportFinished(quint64 id, const QString &path, const SafeSave::Result &result, qint64 workerMs)
{
    Q_UNUSED(path);
    if (id != m_exportId)
        return;
    m_exportId = 0;
    if (m_statsEnabled || result.viaProvider)
        qInfo().noquote() << QStringLiteral("[file] PNG export %1 in %2 ms on the worker%3")
                                 .arg(result.ok() ? QStringLiteral("finished") : QStringLiteral("failed"))
                                 .arg(workerMs)
                                 .arg(result.probe.isEmpty() ? QString() : QStringLiteral("; provider: ") + result.probe);
    emit fileBusyChanged();
    emit exportFinished(result.ok(), result.error);
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
    ViewRenderer::render(*painter, m_document.composite(), m_view, exposed);

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
    // Tilt only when the pen reports it; brushes fall back to the stroke direction otherwise.
    const bool hasTilt = event->pointingDevice()
        && event->pointingDevice()->capabilities().testFlag(QInputDevice::Capability::XTilt)
        && event->pointingDevice()->capabilities().testFlag(QInputDevice::Capability::YTilt);
    const QPointF tilt(event->xTilt(), event->yTilt());

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
            beginStroke(pos, PointerKind::Pen, pressure, eraserTip, tilt, hasTilt);
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
            continueStroke(pos, pressure, tilt, hasTilt);
        else if (m_gesture == Gesture::Pan)
            continuePan(pos);
        event->accept();
        return true;
    case QEvent::TabletRelease:
        if (!m_penDown)
            return false;
        m_penDown = false;
        if (m_gesture == Gesture::Stroke) {
            // The lift-off position ends the line; its pressure (often near zero) and tilt are not used.
            if (m_strokeKind == PointerKind::Pen)
                continueStroke(pos, m_lastPressure, m_lastTilt, m_lastHasTilt);
            endStroke();
        }
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
    if (m_gesture == Gesture::Stroke && m_strokeKind == PointerKind::Mouse) {
        continueStroke(event->position(), 1.0);
        endStroke();
    } else if (m_gesture == Gesture::Pan) {
        endPan();
    }
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
        if (m_gesture == Gesture::Stroke && m_strokeKind == PointerKind::Touch) {
            // A lifted finger ends the line where it was lifted; a cancelled touch ends where the
            // last delivered event left it.
            if (event->type() != QEvent::TouchCancel && !event->points().isEmpty())
                continueStroke(event->points().first().position(), 1.0);
            endStroke();
        }
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
    if (event->key() == Qt::Key_Escape && m_gesture == Gesture::Stroke) {
        cancelStroke();
        event->accept();
        return;
    }
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

StrokeSample CanvasItem::strokeSample(const QPointF &viewPos, qreal pressure, const QPointF &tilt, bool hasTilt) const
{
    StrokeSample sample;
    sample.pos = m_view.viewToDoc(viewPos);
    sample.pressure = pressure;
    sample.hasTilt = hasTilt;
    if (hasTilt) {
        sample.tiltX = tilt.x();
        sample.tiltY = tilt.y();
    }
    sample.timeMs = double(m_strokeClock.nsecsElapsed()) / 1e6;
    return sample;
}

void CanvasItem::beginStroke(const QPointF &viewPos, PointerKind kind, qreal pressure, bool eraserTip,
                             const QPointF &tilt, bool hasTilt)
{
    QElapsedTimer timer;
    if (m_statsEnabled)
        timer.start();

    m_strokeClock.start();
    m_lastPressure = pressure;
    m_lastTilt = tilt;
    m_lastHasTilt = hasTilt;
    startStroke(strokeSample(viewPos, pressure, tilt, hasTilt), kind, eraserTip || m_tool == Eraser);

    if (m_statsEnabled) {
        const qint64 ns = timer.nsecsElapsed();
        ++m_statsInputs;
        m_statsInputNs += ns;
        m_statsInputMaxNs = std::max(m_statsInputMaxNs, ns);
    }
}

bool CanvasItem::startStroke(const StrokeSample &sample, PointerKind kind, bool erase)
{
    const QString blocked = activeLayerBlockReason();
    if (!blocked.isEmpty()) {
        emit notice(blocked); // the gesture stays None, so the rest of this press does nothing
        return false;
    }
    const int index = m_document.indexOf(m_document.activeLayerId());
    if (index < 0)
        return false;
    m_gesture = Gesture::Stroke;
    m_strokeLayerId = m_document.activeLayerId();
    m_strokeKind = kind;
    m_strokeMode = erase ? BrushMode::Erase : BrushMode::Paint;
    // Every brush setting is fixed for the whole stroke here. The smoothing distance is set in view
    // pixels, so it feels the same at every zoom level.
    const BrushSettings settings = strokeSettings(erase);
    m_strokeBlend = settings.blend;
    m_engine = std::make_unique<StrokeEngine>(settings, m_brushColor, ++m_strokeCount,
                                              settings.smoothing / 100.0 * MaxSmoothingRadius / m_view.zoom());
    if (!m_strokeClip.isNull() && m_strokeClip.size() == m_document.size())
        m_engine->setClipMask(m_strokeClip);
    QImage &layer = m_document.pixels(index);
    const qint64 dabs = m_engine->dabCount();
    const QRect changed = m_engine->begin(layer, sample, m_strokeBefore);
    strokePixelsChanged(changed, m_engine->dabCount() - dabs);
    if (m_engine->usesTime())
        m_airbrushTimer.start();
    return true;
}

void CanvasItem::continueStroke(const QPointF &viewPos, qreal pressure, const QPointF &tilt, bool hasTilt)
{
    if (!m_engine)
        return;
    QElapsedTimer timer;
    if (m_statsEnabled)
        timer.start();

    m_lastPressure = pressure;
    m_lastTilt = tilt;
    m_lastHasTilt = hasTilt;
    extendStroke(strokeSample(viewPos, pressure, tilt, hasTilt));

    if (m_statsEnabled) {
        const qint64 ns = timer.nsecsElapsed();
        ++m_statsInputs;
        m_statsInputNs += ns;
        m_statsInputMaxNs = std::max(m_statsInputMaxNs, ns);
    }
}

void CanvasItem::extendStroke(const StrokeSample &sample)
{
    const int index = m_document.indexOf(m_strokeLayerId);
    if (!m_engine || index < 0)
        return;
    QImage &layer = m_document.pixels(index);
    const qint64 dabs = m_engine->dabCount();
    const QRect changed = m_engine->moveTo(layer, sample, m_strokeBefore);
    strokePixelsChanged(changed, m_engine->dabCount() - dabs);
}

bool CanvasItem::drawStroke(const std::vector<StrokeSample> &samples, bool erase)
{
    if (samples.empty() || m_gesture != Gesture::None)
        return false;
    m_strokeClock.start();
    if (!startStroke(samples.front(), PointerKind::Mouse, erase))
        return false;
    for (size_t i = 1; i < samples.size(); ++i)
        extendStroke(samples[i]);
    endStroke();
    return true;
}

void CanvasItem::setStrokeClipMask(const QImage &mask)
{
    m_strokeClip = mask.isNull() ? QImage() : mask.convertToFormat(QImage::Format_Grayscale8);
}

void CanvasItem::advanceAirbrush()
{
    if (!m_engine || m_gesture != Gesture::Stroke) {
        m_airbrushTimer.stop();
        return;
    }
    const int index = m_document.indexOf(m_strokeLayerId);
    if (index < 0)
        return;
    QImage &layer = m_document.pixels(index);
    const qint64 dabs = m_engine->dabCount();
    const QRect changed = m_engine->advanceTime(layer, double(m_strokeClock.nsecsElapsed()) / 1e6, m_strokeBefore);
    strokePixelsChanged(changed, m_engine->dabCount() - dabs);
}

void CanvasItem::endStroke()
{
    // However a stroke ends (release, lost grab, a second finger, an undo or save mid-stroke),
    // the smoothed line is completed to the last input position at once and the marks it made
    // become one undo step.
    m_airbrushTimer.stop();
    if (m_gesture == Gesture::Stroke) {
        const int index = m_document.indexOf(m_strokeLayerId);
        if (m_engine && index >= 0) {
            QImage &layer = m_document.pixels(index);
            const qint64 dabs = m_engine->dabCount();
            const QRect changed = m_engine->finish(layer, m_strokeBefore);
            strokePixelsChanged(changed, m_engine->dabCount() - dabs);
        }
        commitPixels(m_strokeLayerId, std::move(m_strokeBefore),
                     m_strokeBlend == BrushBlend::Erase    ? QStringLiteral("Eraser stroke")
                     : m_strokeBlend == BrushBlend::Smudge ? QStringLiteral("Smudge stroke")
                                                           : QStringLiteral("Brush stroke"));
        m_strokeBefore = TileSnapshot();
    }
    m_engine.reset();
    m_gesture = Gesture::None;
}

void CanvasItem::cancelStroke()
{
    m_airbrushTimer.stop();
    if (m_gesture != Gesture::Stroke)
        return;
    // The snapshot holds every pixel the stroke changed as it was before, so swapping it back
    // restores the layer exactly.
    const int index = m_document.indexOf(m_strokeLayerId);
    if (index >= 0 && !m_strokeBefore.isEmpty())
        strokePixelsChanged(m_strokeBefore.swapWith(m_document.pixels(index)), 0);
    m_strokeBefore = TileSnapshot();
    m_engine.reset();
    m_gesture = Gesture::None;
    emit notice(QStringLiteral("Stroke cancelled"));
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

void CanvasItem::strokePixelsChanged(const QRect &changed, qint64 dabs)
{
    m_statsDabs += dabs;
    if (changed.isEmpty())
        return;
    // Only the stroke's layer changed; the composite is rebuilt for the changed area only.
    QElapsedTimer firstEditTimer;
    const bool firstAfterSnapshot = m_firstEditAfterSnapshot;
    if (firstAfterSnapshot)
        firstEditTimer.start();
    m_document.refreshComposite(changed);
    markContentChanged();
    if (firstAfterSnapshot) {
        m_firstEditAfterSnapshot = false;
        if (m_statsEnabled)
            qInfo().noquote() << QStringLiteral("[snapshot] first edit after a snapshot: %1 ms (composite refresh, "
                                                "before the repaint; the layer copy is timed by the stroke input)")
                                     .arg(firstEditTimer.nsecsElapsed() / 1e6, 0, 'f', 3);
    }
    updateDocumentRect(changed);
}

// --- crash recovery ---

void CanvasItem::markContentChanged()
{
    ++m_contentRevision;
    if (m_oldestUnrecoveredMs < 0)
        m_oldestUnrecoveredMs = m_recoveryClock.elapsed();
}

bool CanvasItem::needsRecovery() const
{
    // A stroke in progress or an opacity drag counts before it is committed to history.
    return isModified() || (m_gesture == Gesture::Stroke && !m_strokeBefore.isEmpty()) || m_opacityDragLayerId != 0;
}

RecoveryFile::Metadata CanvasItem::recoveryMetadata() const
{
    RecoveryFile::Metadata meta;
    meta.documentId = m_documentId;
    meta.revision = m_contentRevision;
    meta.savedAt = QDateTime::currentDateTimeUtc();
    meta.originalPath = m_filePath;
    meta.originalUrl = m_fileUrl.toString();
    if (!m_filePath.isEmpty())
        meta.displayName = documentTitle();
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
                         : !c.metadata.displayName.isEmpty() ? c.metadata.displayName
                                                             : QFileInfo(c.metadata.originalPath).fileName();
    const int layers = int(c.document.layers.size());
    QString text = QStringLiteral("DrawApp did not close normally. Unsaved artwork \"%1\" (%2 × %3 px, %4 %5) from %6 can be restored.")
                       .arg(name)
                       .arg(c.document.size.width())
                       .arg(c.document.size.height())
                       .arg(layers)
                       .arg(layers == 1 ? QStringLiteral("layer") : QStringLiteral("layers"))
                       .arg(QLocale().toString(c.metadata.savedAt.toLocalTime(), QLocale::ShortFormat));
    if (c.recoveryVersion == 1)
        text += QStringLiteral(" It was saved by an earlier single-layer version of DrawApp and opens as one layer.");
    return text;
}

void CanvasItem::restoreRecovery()
{
    if (m_orphans.isEmpty() || !m_recovery)
        return;
    const RecoveryManager::Orphan orphan = m_orphans.takeFirst();
    m_orphans.clear(); // further orphans stay on disk for the next start
    replaceDocument(LayeredDocument(orphan.contents.document));
    const RecoveryFile::Metadata &meta = orphan.contents.metadata;
    setFile(meta.originalPath,
            !meta.originalUrl.isEmpty() ? QUrl(meta.originalUrl)
                                        : (meta.originalPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(meta.originalPath)),
            meta.displayName);
    // Restored artwork was never saved: no history state equals the "saved" id.
    m_savedStateId = std::numeric_limits<quint64>::max();
    const QString error = m_recovery->adopt(orphan);
    if (error.isEmpty()) {
        // The adopted file already holds exactly this state.
        m_requestedDocumentId = m_documentId;
        m_requestedRevision = m_contentRevision;
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
        // A running save may be keeping its local copy in the recovery file; it is released when
        // the save succeeds (onSaveFinished).
        if (m_save.id)
            return;
        if (m_recovery->hasRecoveryFile() || m_recovery->isWriting())
            discardRecoveryFile();
        m_oldestUnrecoveredMs = -1;
        return;
    }
    if (m_documentId == m_requestedDocumentId && m_contentRevision == m_requestedRevision)
        return; // already written or being written
    if (m_recovery->isWriting()) {
        m_autosaveTickMissed = true; // run again as soon as the current write ends
        return;
    }

    // Consistent snapshot on the UI thread: plain values whose layer images are shared
    // copy-on-write, so the worker never sees later edits (the next edit of a shared layer copies
    // that layer's pixels on the UI thread first).
    QElapsedTimer copyTimer;
    copyTimer.start();
    const DocumentData snapshot = m_document.snapshot();
    const qint64 copyNs = copyTimer.nsecsElapsed();
    if (!m_recovery->requestWrite(snapshot, recoveryMetadata()))
        return;
    m_requestedDocumentId = m_documentId;
    m_requestedRevision = m_contentRevision;
    m_inFlightOldestMs = m_oldestUnrecoveredMs;
    m_oldestUnrecoveredMs = -1;
    m_firstEditAfterSnapshot = true;
    emit recoverySnapshotTaken();
    if (m_statsEnabled)
        qInfo().noquote() << QStringLiteral("[recovery] snapshot rev %1 of %2 layers taken on UI thread in %3 ms")
                                 .arg(m_contentRevision)
                                 .arg(snapshot.layers.size())
                                 .arg(copyNs / 1e6, 0, 'f', 4);
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
        m_recovery->writeNow(m_document.snapshot(), recoveryMetadata());
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

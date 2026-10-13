// CanvasItem: selections, transforms and the clipboard (milestone 8).

#include "CanvasItem.h"

#include "RasterDocument.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// A drag shorter than this (screen pixels) is a click.
constexpr qreal ClickDistance = 3.0;

inline quint32 mul255(quint32 x, quint32 y)
{
    const quint32 t = x * y + 128;
    return (t + (t >> 8)) >> 8;
}

SelectionMode toMode(CanvasItem::SelectOp op)
{
    switch (op) {
    case CanvasItem::Add: return SelectionMode::Add;
    case CanvasItem::Subtract: return SelectionMode::Subtract;
    case CanvasItem::Intersect: return SelectionMode::Intersect;
    default: return SelectionMode::Replace;
    }
}

QString modeLabel(SelectionMode mode)
{
    switch (mode) {
    case SelectionMode::Add: return QStringLiteral("Add to selection");
    case SelectionMode::Subtract: return QStringLiteral("Subtract from selection");
    case SelectionMode::Intersect: return QStringLiteral("Intersect selection");
    default: return QStringLiteral("Select");
    }
}

// A two-tone line (white under black dashes) that reads on any artwork.
void strokeTwoTone(QPainter *painter, const QPainterPath &path)
{
    QPen light(QColor(255, 255, 255), 1);
    light.setCosmetic(true);
    painter->strokePath(path, light);
    QPen dark(QColor(0, 0, 0), 1, Qt::CustomDashLine);
    dark.setDashPattern({4, 4});
    dark.setCosmetic(true);
    painter->strokePath(path, dark);
}

} // namespace

// --- options and state ---

void CanvasItem::setSelectShape(SelectShape shape)
{
    if (shape == m_selectShape)
        return;
    cancelPolygon();
    m_selectShape = shape;
    emit selectToolChanged();
}

void CanvasItem::setSelectMode(SelectOp mode)
{
    if (mode == m_selectMode)
        return;
    m_selectMode = mode;
    emit selectToolChanged();
}

void CanvasItem::setWandTolerance(int tolerance)
{
    tolerance = std::clamp(tolerance, 0, 255);
    if (tolerance == m_wandTolerance)
        return;
    m_wandTolerance = tolerance;
    emit selectToolChanged();
}

void CanvasItem::setWandContiguous(bool contiguous)
{
    if (contiguous == m_wandContiguous)
        return;
    m_wandContiguous = contiguous;
    emit selectToolChanged();
}

void CanvasItem::setWandSampleAll(bool all)
{
    if (all == m_wandSampleAll)
        return;
    m_wandSampleAll = all;
    emit selectToolChanged();
}

void CanvasItem::setKeepAspect(bool keep)
{
    if (keep == m_keepAspect)
        return;
    m_keepAspect = keep;
    emit transformChanged();
}

const TiledImage &CanvasItem::displayedComposite() const
{
    return m_transform.isActive() ? m_previewComposite : m_document.composite();
}

void CanvasItem::selectionUpdated()
{
    m_selectionOutline = m_document.selection().outline();
    updateAll();
    emit selectionChanged();
}

void CanvasItem::setSelectionWithUndo(const Selection &next, const QString &label)
{
    const Selection before = m_document.selection();
    if (next == before)
        return;
    if (before.isActive() && !before.isEmpty())
        m_reselect = before;
    m_document.setSelection(next);
    // In history so a mistaken selection can be undone; never an unsaved change (see UndoHistory).
    pushCommand(UndoCommand::selection(before, label));
    selectionUpdated();
}

void CanvasItem::finishPendingEdits()
{
    if (m_gesture == Gesture::Stroke)
        endStroke();
    finishWetPaint(); // watercolour still drying stays as it looks (CanvasItemWet.cpp)
    if (m_transform.isActive())
        applyTransformWith(false);
}

// --- selection commands ---

void CanvasItem::selectPath(const QPainterPath &docPath, SelectionMode mode)
{
    finishPendingEdits();
    const QImage shape = Selection::rasterize(m_document.size(), docPath, true);
    setSelectionWithUndo(m_document.selection().combined(shape, mode), modeLabel(mode));
}

void CanvasItem::selectAll()
{
    if (applyBlocks())
        return;
    finishPendingEdits();
    setSelectionWithUndo(Selection::all(m_document.size()), QStringLiteral("Select all"));
}

void CanvasItem::deselect()
{
    if (applyBlocks())
        return;
    finishPendingEdits();
    setSelectionWithUndo(Selection(), QStringLiteral("Deselect"));
}

void CanvasItem::invertSelection()
{
    if (applyBlocks())
        return;
    finishPendingEdits();
    if (!m_document.selection().isActive()) {
        emit notice(QStringLiteral("There is no selection to invert."));
        return;
    }
    setSelectionWithUndo(m_document.selection().inverted(), QStringLiteral("Invert selection"));
}

void CanvasItem::growSelection(qreal radius)
{
    if (applyBlocks())
        return;
    finishPendingEdits();
    if (!m_document.selection().isActive()) {
        emit notice(QStringLiteral("There is no selection to grow."));
        return;
    }
    setSelectionWithUndo(m_document.selection().grown(radius), QStringLiteral("Grow selection"));
}

void CanvasItem::shrinkSelection(qreal radius)
{
    if (applyBlocks())
        return;
    finishPendingEdits();
    if (!m_document.selection().isActive()) {
        emit notice(QStringLiteral("There is no selection to shrink."));
        return;
    }
    setSelectionWithUndo(m_document.selection().shrunk(radius), QStringLiteral("Shrink selection"));
    if (m_document.selection().isEmpty())
        emit notice(QStringLiteral("The selection is now empty: nothing can be painted until you select something or deselect."));
}

void CanvasItem::featherSelection(qreal radius)
{
    if (applyBlocks())
        return;
    finishPendingEdits();
    if (!m_document.selection().isActive()) {
        emit notice(QStringLiteral("There is no selection to feather."));
        return;
    }
    setSelectionWithUndo(m_document.selection().feathered(radius), QStringLiteral("Feather selection"));
}

void CanvasItem::wandAt(const QPointF &docPos, SelectionMode mode)
{
    const QPoint seed(int(std::floor(docPos.x())), int(std::floor(docPos.y())));
    if (!QRect(QPoint(0, 0), m_document.size()).contains(seed))
        return;
    const int index = m_document.activeIndex();
    const TiledImage &source = m_wandSampleAll || index < 0 ? m_document.composite() : m_document.layer(index).image;
    const QImage mask = Selection::magicWand(source, seed, m_wandTolerance, m_wandContiguous, true);
    setSelectionWithUndo(m_document.selection().combined(mask, mode), QStringLiteral("Magic wand"));
    if (m_document.selection().isEmpty())
        emit notice(QStringLiteral("The selection is now empty: nothing can be painted until you select something or deselect."));
}

void CanvasItem::closePolygon()
{
    if (applyBlocks())
        return;
    if (m_polygon.size() < 3) {
        cancelPolygon();
        return;
    }
    QPainterPath path;
    path.addPolygon(QPolygonF(QList<QPointF>(m_polygon.begin(), m_polygon.end())));
    path.closeSubpath();
    const SelectionMode mode = m_polygonMode;
    m_polygon.clear();
    emit selectToolChanged();
    const QImage shape = Selection::rasterize(m_document.size(), path, true);
    setSelectionWithUndo(m_document.selection().combined(shape, mode), modeLabel(mode));
    updateAll();
}

void CanvasItem::cancelPolygon()
{
    if (m_polygon.empty())
        return;
    m_polygon.clear();
    emit selectToolChanged();
    updateAll();
}

void CanvasItem::hoverMoveEvent(QHoverEvent *event)
{
    m_hoverDoc = m_view.viewToDoc(event->position());
    m_hoverKnown = true;
    if (!m_polygon.empty())
        updateAll(); // the rubber band follows the pointer
    QQuickPaintedItem::hoverMoveEvent(event);
}

void CanvasItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_tool == Select && m_selectShape == PolygonShape && !m_polygon.empty()) {
        closePolygon();
        event->accept();
        return;
    }
    QQuickPaintedItem::mouseDoubleClickEvent(event);
}

QString CanvasItem::beginSelectionTransform()
{
    if (m_apply.isRunning())
        return applyBusyText();
    if (m_transform.isActive())
        return {};
    finishPendingEdits();
    const Selection &selected = m_document.selection();
    if (!selected.isActive())
        return QStringLiteral("There is no selection to transform.");
    if (selected.isEmpty())
        return QStringLiteral("Nothing is selected, so there is nothing to transform.");
    if (!m_transform.begin(std::vector<QImage>{}, selected))
        return QStringLiteral("The selection could not be transformed.");
    m_transformLayerIds.clear(); // outline only
    m_toolBeforeTransform = m_tool == Transform ? Brush : m_tool;
    m_tool = Transform;
    m_previewLayers.clear();
    m_previewComposite = m_document.composite();
    m_previewRect = QRect();
    updateTransformPreview(TransformSession::Quality::Final);
    emit toolChanged();
    emit transformChanged();
    return {};
}

void CanvasItem::reselect()
{
    if (applyBlocks())
        return;
    finishPendingEdits();
    if (!canReselect())
        return;
    setSelectionWithUndo(m_reselect, QStringLiteral("Reselect"));
}

QString CanvasItem::selectedLayersBlockReason(const QString &action) const
{
    for (const quint64 id : selectedLayerIds()) {
        const LayerData &layer = m_document.layer(m_document.indexOf(id));
        if (layer.locked)
            return QStringLiteral("Can't %1: layer \"%2\" is locked. Unlock it or leave it out of the selected layers.")
                .arg(action, layer.name);
        if (!layer.visible)
            return QStringLiteral("Can't %1: layer \"%2\" is hidden. Show it or leave it out of the selected layers.")
                .arg(action, layer.name);
    }
    return {};
}

void CanvasItem::clearSelected()
{
    if (applyBlocks())
        return;
    finishPendingEdits();
    const QString reason = selectedLayersBlockReason(QStringLiteral("clear"));
    if (!reason.isEmpty()) {
        emit notice(reason);
        return;
    }
    const Selection &selected = m_document.selection();
    if (selected.isEmpty()) {
        emit notice(QStringLiteral("Nothing is selected, so nothing was cleared."));
        return;
    }
    const QString label = selected.isActive() ? QStringLiteral("Clear selection") : QStringLiteral("Clear layer");
    {
        // The undo record keeps the painted tiles the clear touches.
        const QRect area = selected.isActive() ? selected.bounds() : QRect(QPoint(0, 0), m_document.size());
        qsizetype bytes = 0;
        for (const quint64 id : selectedLayerIds())
            bytes += m_document.layer(m_document.indexOf(id)).image.allocatedBytes(area);
        if (confirmIfNotUndoable(bytes, label, [this] { clearSelected(); }))
            return;
    }
    std::vector<UndoCommand> children;
    QRect changedAll;
    for (const quint64 id : selectedLayerIds()) {
        TiledImage &layer = m_document.pixels(m_document.indexOf(id));
        TileSnapshot before;
        QRect changed;
        if (!selected.isActive()) {
            changed = RasterDocument::clear(layer, &before);
        } else {
            // Keeps layer × (1 − selection), the exact complement of what Copy takes. Only
            // painted tiles are visited (clearing a transparent tile changes nothing); each is
            // made writable before any pixel changes, so a refused allocation changes nothing.
            changed = selected.bounds();
            before.capture(layer, changed);
            std::vector<QPoint> tiles;
            for (int row = layer.rowOf(changed.top()); row <= layer.rowOf(changed.bottom()); ++row)
                for (int column = layer.columnOf(changed.left()); column <= layer.columnOf(changed.right()); ++column)
                    if (layer.hasTile(column, row))
                        tiles.push_back(QPoint(column, row));
            for (const QPoint &t : tiles)
                if (!layer.writableTileData(t.x(), t.y())) {
                    before.swapWith(layer); // tiles made writable so far are unchanged copies
                    emit notice(QStringLiteral("Not enough memory to clear the selection. Nothing was changed."));
                    return;
                }
            for (const QPoint &t : tiles) {
                const QRect part = layer.tileRect(t.x(), t.y()) & changed;
                for (int y = part.top(); y <= part.bottom(); ++y) {
                    const uchar *s = selected.mask().constScanLine(y);
                    QRgb *row = layer.writableRow(part.left(), y) - part.left(); // indexed by x
                    for (int x = part.left(); x <= part.right(); ++x) {
                        const QRgb p = row[x];
                        const quint32 c = s[x];
                        row[x] = qRgba(int(qRed(p) - mul255(qRed(p), c)), int(qGreen(p) - mul255(qGreen(p), c)),
                                       int(qBlue(p) - mul255(qBlue(p), c)), int(qAlpha(p) - mul255(qAlpha(p), c)));
                    }
                }
            }
            layer.pruneTransparent(changed);
        }
        before.dropUnchanged(layer);
        if (!before.isEmpty()) {
            children.push_back(UndoCommand::pixels(id, std::move(before), label));
            changedAll |= changed;
        }
    }
    if (children.empty())
        return;
    pushCommand(children.size() == 1 ? std::move(children.front()) : UndoCommand::group(std::move(children), label));
    refreshComposite(changedAll);
    markContentChanged();
    updateDocumentRect(changedAll);
}

// --- selection gestures ---

void CanvasItem::beginSelectGesture(const QPointF &viewPos)
{
    if (applyBlocks())
        return;
    const QPointF docPoint = m_view.viewToDoc(viewPos);
    if (m_selectShape == WandShape || m_selectShape == PolygonShape) {
        // A click, not a drag: the gesture ends at once.
        const Qt::KeyboardModifiers mods = m_pointerModifiers;
        const bool shift = mods.testFlag(Qt::ShiftModifier), alt = mods.testFlag(Qt::AltModifier);
        const SelectionMode mode = shift && alt ? SelectionMode::Intersect
                                 : shift        ? SelectionMode::Add
                                 : alt          ? SelectionMode::Subtract
                                                : toMode(m_selectMode);
        if (m_selectShape == WandShape) {
            wandAt(docPoint, mode);
            return;
        }
        if (m_polygon.empty()) {
            m_polygonMode = mode;
            m_polygon.push_back(docPoint);
            emit selectToolChanged();
        } else {
            // Clicking the first point (within the handle radius) closes the polygon.
            const QPointF first = m_view.docToView(m_polygon.front());
            if (m_polygon.size() >= 3 && std::hypot(first.x() - viewPos.x(), first.y() - viewPos.y()) <= HandleHitRadius * 0.6) {
                closePolygon();
                return;
            }
            m_polygon.push_back(docPoint);
        }
        m_hoverDoc = docPoint;
        updateAll();
        return;
    }
    m_gesture = Gesture::Stroke;
    m_pointerTask = PointerTask::Select;
    // Shift adds, Alt subtracts, both intersect (when the gesture starts); otherwise the mode
    // chosen in the toolbar (for touch).
    const Qt::KeyboardModifiers mods = m_pointerModifiers;
    const bool shift = mods.testFlag(Qt::ShiftModifier), alt = mods.testFlag(Qt::AltModifier);
    m_gestureMode = shift && alt ? SelectionMode::Intersect
                  : shift        ? SelectionMode::Add
                  : alt          ? SelectionMode::Subtract
                                 : toMode(m_selectMode);
    m_selectStartView = viewPos;
    const QPointF doc = m_view.viewToDoc(viewPos);
    m_selectPoints = {doc, doc};
    updateAll();
}

void CanvasItem::continueSelectGesture(const QPointF &viewPos)
{
    const QPointF doc = m_view.viewToDoc(viewPos);
    if (m_selectShape == LassoShape) {
        const QPointF last = m_view.docToView(m_selectPoints.back());
        if (std::hypot(viewPos.x() - last.x(), viewPos.y() - last.y()) >= 1.0)
            m_selectPoints.push_back(doc);
    } else {
        m_selectPoints.back() = doc;
    }
    updateAll();
}

void CanvasItem::finishSelectGesture()
{
    const std::vector<QPointF> points = std::move(m_selectPoints);
    m_selectPoints.clear();
    updateAll();
    if (points.size() < 2)
        return;
    // Extent on screen decides between a click and a shape.
    QRectF extent(m_view.docToView(points.front()), QSizeF());
    for (const QPointF &p : points)
        extent |= QRectF(m_view.docToView(p), QSizeF(0.01, 0.01));
    if (extent.width() < ClickDistance && extent.height() < ClickDistance) {
        // A click with the plain tool deselects (as in most editors); with a modifier it does nothing.
        if (m_gestureMode == SelectionMode::Replace && m_document.selection().isActive())
            setSelectionWithUndo(Selection(), QStringLiteral("Deselect"));
        return;
    }
    QPainterPath path;
    if (m_selectShape == LassoShape) {
        path.addPolygon(QPolygonF(QList<QPointF>(points.begin(), points.end())));
        path.closeSubpath();
    } else {
        const QRectF r = QRectF(points.front(), points.back()).normalized();
        if (m_selectShape == EllipseShape)
            path.addEllipse(r);
        else
            path.addRect(r);
    }
    const QImage shape = Selection::rasterize(m_document.size(), path, true);
    setSelectionWithUndo(m_document.selection().combined(shape, m_gestureMode), modeLabel(m_gestureMode));
    if (m_document.selection().isEmpty())
        emit notice(QStringLiteral("The selection is now empty: nothing can be painted until you select something or deselect."));
}

void CanvasItem::abortPointerGesture()
{
    if (m_pointerTask == PointerTask::Select) {
        m_selectPoints.clear();
    } else if (m_pointerTask == PointerTask::Transform) {
        m_dragHandle = TransformSession::Handle::None;
        updateTransformPreview(TransformSession::Quality::Final);
    }
    m_pointerTask = PointerTask::Paint;
    m_gesture = Gesture::None;
    updateAll();
}

// --- transform ---

QString CanvasItem::beginTransform()
{
    if (m_apply.isRunning())
        return applyBusyText();
    if (m_transform.isActive())
        return {};
    if (fileBusy())
        return QStringLiteral("Wait until the current file operation has finished.");
    finishPendingEdits();
    const QString blocked = selectedLayersBlockReason(QStringLiteral("transform"));
    if (!blocked.isEmpty())
        return blocked;
    const Selection &selected = m_document.selection();
    if (selected.isEmpty())
        return QStringLiteral("Nothing is selected, so there is nothing to transform.");
    // Every selected layer moves together, with one geometry.
    const std::vector<quint64> ids = selectedLayerIds();
    std::vector<TiledImage> images; // copy-on-write snapshots: no pixels are copied
    for (const quint64 id : ids)
        images.push_back(m_document.layer(m_document.indexOf(id)).image);
    // Several adjacent layers without a selection: the preview is drawn from one merged copy (one
    // resampling per frame instead of one per layer; source-over is associative, so only
    // anti-aliased edges where the layers overlap can differ slightly). Apply transforms each layer.
    bool merged = false;
    if (!selected.isActive() && ids.size() > 1) {
        bool adjacent = true;
        for (size_t i = 1; i < ids.size(); ++i)
            adjacent = adjacent && m_document.indexOf(ids[i]) == m_document.indexOf(ids[i - 1]) + 1;
        if (adjacent) {
            TiledImage flat(m_document.size(), TiledImage::DefaultTileSize, MemoryBudget::Category::Preview);
            std::vector<LayerData> stack;
            for (const quint64 id : ids)
                stack.push_back(m_document.layer(m_document.indexOf(id)));
            if (compositeLayers(flat, flat.rect(), stack)) {
                images.push_back(flat);
                merged = true;
            } // else the per-layer preview still works
        }
    }
    if (!m_transform.begin(images, selected)) {
        if (m_transform.lastBeginOutOfMemory())
            return QStringLiteral("Not enough memory to transform this area (%1). Nothing was changed.")
                .arg(MemoryBudget::shortageText());
        if (ids.size() == 1)
            return QStringLiteral("The layer \"%1\" has nothing to transform.").arg(m_document.layer(m_document.indexOf(ids.front())).name);
        return QStringLiteral("The selected layers have nothing to transform.");
    }
    m_transformLayerIds = ids;
    m_previewMerged = merged;
    m_toolBeforeTransform = m_tool == Transform ? Brush : m_tool;
    m_tool = Transform;
    // The preview lives in copies; the document is not touched until Apply.
    m_previewLayers = images;
    m_previewComposite = m_document.composite();
    m_previewRect = QRect();
    updateTransformPreview(TransformSession::Quality::Final);
    emit toolChanged();
    emit transformChanged();
    return {};
}

void CanvasItem::updateTransformPreview(TransformSession::Quality quality)
{
    if (!m_transform.isActive())
        return;
    if (m_transformLayerIds.empty()) {
        // Outline only: show where the selection will be.
        m_transformedOutline = m_transform.transformedSelection().outline();
        updateAll();
        emit transformChanged();
        return;
    }
    const QRect now = m_transform.changedRect();
    const QRect area = (now | m_previewRect) & QRect(QPoint(0, 0), m_document.size());
    std::vector<LayerData> layers = m_document.layers(); // shallow: images are shared
    if (m_previewMerged) {
        // The merged copy (the last part) stands in for the selected layers, at the bottom one's
        // place in the stack; their opacities are already in it.
        const size_t mergedPart = m_transformLayerIds.size();
        m_transform.render(int(mergedPart), m_previewLayers[mergedPart], area, quality);
        for (size_t i = 0; i < m_transformLayerIds.size(); ++i) {
            const int index = m_document.indexOf(m_transformLayerIds[i]);
            if (index < 0)
                continue;
            if (i == 0) {
                layers[size_t(index)].image = m_previewLayers[mergedPart];
                layers[size_t(index)].opacity = 1.0;
            } else {
                layers[size_t(index)].visible = false;
            }
        }
    } else {
        for (size_t i = 0; i < m_transformLayerIds.size(); ++i) {
            const int index = m_document.indexOf(m_transformLayerIds[i]);
            if (index < 0)
                continue;
            m_transform.render(int(i), m_previewLayers[i], area, quality);
            layers[size_t(index)].image = m_previewLayers[i];
        }
    }
    compositeLayers(m_previewComposite, area, layers);
    m_previewRect = now;
    updateDocumentRect(area);
    updateAll(); // the box and handles
    emit transformChanged();
}

void CanvasItem::endTransformSession()
{
    if (m_apply.isRunning()) {
        m_apply.cancel(); // before the session's caches go
        m_applyProgressTimer.stop();
        emit applyChanged();
    }
    m_transform.end();
    m_transformedOutline = QPainterPath();
    m_transformLayerIds.clear();
    m_previewMerged = false;
    m_previewLayers.clear();
    m_previewComposite = TiledImage();
    m_previewRect = QRect();
    m_dragHandle = TransformSession::Handle::None;
    if (m_gesture == Gesture::Stroke && m_pointerTask == PointerTask::Transform) {
        m_gesture = Gesture::None;
        m_pointerTask = PointerTask::Paint;
    }
    m_tool = m_toolBeforeTransform;
    emit toolChanged();
    emit transformChanged();
    selectionUpdated();
}

void CanvasItem::applyTransform()
{
    applyTransformWith(true);
}

QString CanvasItem::applyBusyText() const
{
    return QStringLiteral("The transform is still being applied. Wait for it to finish, or cancel it (Esc).");
}

bool CanvasItem::applyBlocks()
{
    if (!m_apply.isRunning())
        return false;
    emit notice(applyBusyText());
    return true;
}

void CanvasItem::applyTransformWith(bool background)
{
    if (!m_transform.isActive())
        return;
    if (m_apply.isRunning()) {
        if (!background)
            m_apply.finishNow(); // an implicit apply needs the result before the caller continues
        return;
    }
    if (m_transform.isIdentity()) {
        endTransformSession(); // nothing moved: nothing to record
        updateAll();
        return;
    }
    if (m_transformLayerIds.empty()) {
        // Outline only: one selection step.
        const Selection moved = m_transform.transformedSelection();
        endTransformSession();
        setSelectionWithUndo(moved, QStringLiteral("Transform selection"));
        if (m_document.selection().isEmpty())
            emit notice(QStringLiteral("The selection was moved off the canvas and is now empty."));
        updateAll();
        return;
    }
    // Transaction: each layer's result is prepared first (in the background for large work) as a
    // copy of the layer sharing its tiles, with the changed area's tiles rewritten. Only then is
    // the document changed, by exchanging tile references, which cannot fail. Pixels moved beyond
    // the canvas are cut off (layers are canvas-sized).
    if (background) {
        // Asked before, where the cost is known: the undo record keeps the layers' painted tiles in
        // the changed area. (Implicit applies cannot ask; they say so afterwards, as strokes do.)
        qsizetype bytes = 0;
        for (const quint64 id : m_transformLayerIds)
            if (const int index = m_document.indexOf(id); index >= 0)
                bytes += m_document.layer(index).image.allocatedBytes(m_transform.changedRect());
        if (confirmIfNotUndoable(bytes, QStringLiteral("This transform"), [this] { applyTransformWith(true); }))
            return;
    }
    TransformApply::Input input;
    input.session = m_transform.workerCopy();
    input.area = m_transform.changedRect();
    for (const quint64 id : m_transformLayerIds) {
        const int index = m_document.indexOf(id);
        if (index < 0) {
            emit notice(QStringLiteral("A transformed layer no longer exists. Nothing was changed."));
            return;
        }
        input.layerIds.push_back(id);
        input.layers.push_back(m_document.layer(index).image);
        input.versions.push_back(m_document.layer(index).image.version());
    }
    // Room for the new tiles is made here: the worker cannot run evictors (they belong to this
    // thread).
    const qint64 tiles = qint64(input.area.width() / TiledImage::DefaultTileSize + 2)
                         * (input.area.height() / TiledImage::DefaultTileSize + 2);
    MemoryBudget::evict(tiles * TiledImage::DefaultTileSize * TiledImage::DefaultTileSize * 4 * qint64(input.layers.size()));
    const qint64 work = qint64(input.area.width()) * input.area.height() * qint64(input.layers.size());
    if (!background || work < m_backgroundApplyPixels) {
        commitApply(TransformApply::run(std::move(input)));
        return;
    }
    m_apply.start(std::move(input));
    m_applyProgressTimer.start();
    emit applyChanged();
    emit applyProgressChanged();
}

void CanvasItem::commitApply(const TransformApply::Result &result)
{
    if (!m_transform.isActive())
        return;
    if (!result.ok) {
        emit notice(result.outOfMemory
                        ? QStringLiteral("Not enough memory to apply the transform (%1). Nothing was changed; the preview is still open.")
                              .arg(MemoryBudget::shortageText())
                        : QStringLiteral("The transform was not applied. Nothing was changed; the preview is still open."));
        updateAll();
        return;
    }
    // The job's inputs must still be the document's layers (edits wait for Apply, so this only
    // guards against mistakes).
    for (size_t i = 0; i < result.layerIds.size(); ++i) {
        const int index = m_document.indexOf(result.layerIds[i]);
        if (index < 0 || m_document.layer(index).image.version() != result.versions[i]) {
            emit notice(QStringLiteral("The layers changed while the transform was being applied, so it was not applied. "
                                       "The preview is still open."));
            return;
        }
    }
    const QRect changed = result.area;
    Selection newSelection;
    bool ok = true;
    try {
        if (m_transform.hasSelection())
            newSelection = m_transform.transformedSelection();
    } catch (const std::bad_alloc &) {
        ok = false;
    }
    if (!ok || (m_transform.hasSelection() && !newSelection.isActive())) {
        emit notice(QStringLiteral("Not enough memory to apply the transform. Nothing was changed; the preview is still open."));
        return;
    }
    // Commit: tile references only, which cannot fail.
    std::vector<UndoCommand> children;
    for (size_t i = 0; i < result.layerIds.size(); ++i) {
        const quint64 id = result.layerIds[i];
        TiledImage &layer = m_document.pixels(m_document.indexOf(id));
        TileSnapshot before;
        before.capture(layer, changed);
        layer = result.layers[i];
        before.dropUnchanged(layer);
        if (!before.isEmpty())
            children.push_back(UndoCommand::pixels(id, std::move(before), QStringLiteral("Transform")));
    }
    const Selection oldSelection = m_document.selection();
    if (m_transform.hasSelection())
        m_document.setSelection(newSelection); // follows the pixels
    if (!children.empty() || m_transform.hasSelection()) {
        UndoCommand command = UndoCommand::group(std::move(children), QStringLiteral("Transform"));
        if (m_transform.hasSelection())
            command.withSelection(oldSelection);
        pushCommand(std::move(command));
    }
    refreshComposite(changed);
    markContentChanged();
    endTransformSession();
    updateDocumentRect(changed);
    updateAll();
}

void CanvasItem::cancelApply()
{
    if (!m_apply.isRunning())
        return;
    m_apply.cancel();
    m_applyProgressTimer.stop();
    emit applyChanged();
    emit notice(QStringLiteral("Apply cancelled. Nothing was changed; the preview is still open."));
    updateAll();
}

void CanvasItem::cancelTransform()
{
    if (!m_transform.isActive())
        return;
    if (m_apply.isRunning()) {
        cancelApply(); // the first Cancel stops the apply; the next one closes the preview
        return;
    }
    // The document was never touched: dropping the preview restores it exactly.
    const QRect shown = m_previewRect;
    endTransformSession();
    updateDocumentRect(shown);
    updateAll();
}

void CanvasItem::setTransformValue(const QString &key, qreal value)
{
    if (applyBlocks())
        return;
    if (!m_transform.isActive() || !std::isfinite(value))
        return;
    if (key == QLatin1String("x")) {
        m_transform.setCenter(QPointF(value, m_transform.center().y()));
    } else if (key == QLatin1String("y")) {
        m_transform.setCenter(QPointF(m_transform.center().x(), value));
    } else if (key == QLatin1String("width") || key == QLatin1String("height")) {
        const QPointF center = m_transform.center();
        double w = m_transform.width(), h = m_transform.height();
        if (key == QLatin1String("width")) {
            if (m_keepAspect && w > 0)
                h *= value / w;
            w = value;
        } else {
            if (m_keepAspect && h > 0)
                w *= value / h;
            h = value;
        }
        m_transform.setSize(w, h);
        m_transform.setCenter(center); // numeric sizes keep the box centred
    } else if (key == QLatin1String("rotation")) {
        const QPointF center = m_transform.center();
        m_transform.setRotation(value);
        m_transform.setCenter(center);
    } else {
        return;
    }
    updateTransformPreview(TransformSession::Quality::Final);
}

void CanvasItem::flipTransformHorizontal()
{
    if (applyBlocks())
        return;
    if (!m_transform.isActive())
        return;
    m_transform.flipHorizontal();
    updateTransformPreview(TransformSession::Quality::Final);
}

void CanvasItem::flipTransformVertical()
{
    if (applyBlocks())
        return;
    if (!m_transform.isActive())
        return;
    m_transform.flipVertical();
    updateTransformPreview(TransformSession::Quality::Final);
}

void CanvasItem::resetTransform()
{
    if (applyBlocks())
        return;
    if (!m_transform.isActive())
        return;
    m_transform.reset();
    updateTransformPreview(TransformSession::Quality::Final);
}

void CanvasItem::beginTransformDrag(const QPointF &viewPos)
{
    if (applyBlocks())
        return;
    if (!m_transform.isActive())
        return;
    const qreal zoom = m_view.zoom();
    const QPointF doc = m_view.viewToDoc(viewPos);
    m_dragHandle = m_transform.hitTest(doc, HandleHitRadius / zoom, RotateKnobDistance / zoom);
    if (m_dragHandle == TransformSession::Handle::None)
        return;
    m_gesture = Gesture::Stroke;
    m_pointerTask = PointerTask::Transform;
    m_dragLastDoc = doc; // where the drag started
    m_dragStartCenter = m_transform.center();
    const QPointF p = m_transform.pivot();
    m_dragStartAngle = qRadiansToDegrees(std::atan2(doc.y() - p.y(), doc.x() - p.x()));
    m_dragStartRotation = m_transform.rotation();
}

void CanvasItem::continueTransformDrag(const QPointF &viewPos)
{
    using H = TransformSession::Handle;
    const QPointF doc = m_view.viewToDoc(viewPos);
    const Qt::KeyboardModifiers mods = m_pointerModifiers;
    switch (m_dragHandle) {
    case H::None:
        return;
    case H::Move:
        // From the press: whole-pixel snapping must not swallow small steps.
        m_transform.setCenter(m_dragStartCenter + (doc - m_dragLastDoc));
        break;
    case H::Pivot:
        m_transform.setPivot(doc);
        break;
    case H::Rotate: {
        const QPointF p = m_transform.pivot();
        double angle = m_dragStartRotation
                     + qRadiansToDegrees(std::atan2(doc.y() - p.y(), doc.x() - p.x())) - m_dragStartAngle;
        if (mods.testFlag(Qt::ShiftModifier))
            angle = std::round(angle / 15.0) * 15.0; // Shift snaps to 15°
        m_transform.setRotation(angle);
        break;
    }
    default:
        // Shift inverts the aspect setting for this drag; Alt scales from the centre.
        m_transform.dragScaleHandle(m_dragHandle, doc, m_keepAspect != mods.testFlag(Qt::ShiftModifier),
                                    mods.testFlag(Qt::AltModifier));
        break;
    }
    updateTransformPreview(TransformSession::Quality::Fast);
}

void CanvasItem::finishTransformDrag()
{
    m_dragHandle = TransformSession::Handle::None;
    // The dragged preview used fast sampling; show the final quality once the pointer is up.
    updateTransformPreview(TransformSession::Quality::Final);
}

// --- clipboard ---

bool CanvasItem::canPaste() const
{
    if (!m_clipboard.empty())
        return true;
    const QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard && !clipboard->image().isNull();
}

QString CanvasItem::copySelection()
{
    finishPendingEdits();
    if (m_document.activeIndex() < 0)
        return QStringLiteral("There is no active layer.");
    const Selection &selected = m_document.selection();
    if (selected.isEmpty())
        return QStringLiteral("Nothing is selected, so nothing was copied.");
    // One item per selected layer (bottom to top) that has visible pixels in the selection.
    std::vector<ClipItem> items;
    for (const quint64 id : selectedLayerIds()) {
        const LayerData &source = m_document.layer(m_document.indexOf(id));
        const TiledImage &layer = source.image;
        QImage taken;
        QRect rect;
        if (selected.isActive()) {
            rect = selected.bounds();
            taken = layer.copy(rect);
            if (taken.isNull())
                return QStringLiteral("Not enough memory to copy the selection. The clipboard was not changed.");
            for (int y = 0; y < rect.height(); ++y) {
                const uchar *s = selected.mask().constScanLine(y + rect.top()) + rect.left();
                QRgb *row = reinterpret_cast<QRgb *>(taken.scanLine(y));
                for (int x = 0; x < rect.width(); ++x) {
                    const QRgb p = row[x];
                    row[x] = qRgba(int(mul255(qRed(p), s[x])), int(mul255(qGreen(p), s[x])), int(mul255(qBlue(p), s[x])),
                                   int(mul255(qAlpha(p), s[x])));
                }
            }
        } else {
            rect = layer.contentBounds();
            if (rect.isEmpty())
                continue;
            taken = layer.copy(rect);
            if (taken.isNull())
                return QStringLiteral("Not enough memory to copy the layers. The clipboard was not changed.");
        }
        // Trim fully transparent margins, keeping the position.
        int left = taken.width(), right = -1, top = taken.height(), bottom = -1;
        for (int y = 0; y < taken.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(taken.constScanLine(y));
            for (int x = 0; x < taken.width(); ++x) {
                if (qAlpha(row[x])) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = y;
                }
            }
        }
        if (right < 0)
            continue;
        const QRect trimmed(QPoint(left, top), QPoint(right, bottom));
        items.push_back({source.name, taken.copy(trimmed), rect.topLeft() + trimmed.topLeft()});
    }
    if (items.empty())
        return selected.isActive() ? QStringLiteral("The selected area of the selected layers is empty, so nothing was copied.")
                                   : QStringLiteral("The selected layers are empty, so nothing was copied.");
    m_clipboard = std::move(items);
    m_clipDocSize = m_document.size();
    // Other applications get one flattened image (straight alpha, as they expect).
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        QRect bounds;
        for (const ClipItem &item : m_clipboard)
            bounds |= QRect(item.origin, item.image.size());
        QImage flat(bounds.size(), QImage::Format_ARGB32_Premultiplied);
        flat.fill(Qt::transparent);
        QPainter p(&flat);
        for (const ClipItem &item : m_clipboard)
            p.drawImage(item.origin - bounds.topLeft(), item.image);
        p.end();
        clipboard->setImage(flat.convertToFormat(QImage::Format_ARGB32));
    }
    emit clipboardChanged();
    return {};
}

QString CanvasItem::cutSelection()
{
    if (m_apply.isRunning())
        return applyBusyText();
    finishPendingEdits();
    const QString blocked = selectedLayersBlockReason(QStringLiteral("cut"));
    if (!blocked.isEmpty())
        return blocked;
    const QString copied = copySelection();
    if (!copied.isEmpty())
        return copied;
    // Leaves layer × (1 − selection) on every selected layer: pasting the copy back in place
    // restores soft edges.
    const int steps = m_history.undoCount();
    clearSelected();
    if (m_history.undoCount() == steps + 1)
        m_history.relabelLast(QStringLiteral("Cut"));
    emit historyChanged();
    return {};
}

QString CanvasItem::paste()
{
    if (m_apply.isRunning())
        return applyBusyText();
    finishPendingEdits();
    std::vector<ClipItem> items = m_clipboard;
    const QRect canvas(QPoint(0, 0), m_document.size());
    if (!items.empty()) {
        // From a canvas of another size, or entirely off this canvas: centred as a group.
        QRect bounds;
        for (const ClipItem &item : items)
            bounds |= QRect(item.origin, item.image.size());
        if (m_clipDocSize != m_document.size() || !bounds.intersects(canvas)) {
            const QPoint shift = canvas.center() - bounds.center();
            for (ClipItem &item : items)
                item.origin += shift;
        }
    } else if (const QClipboard *clipboard = QGuiApplication::clipboard(); clipboard && !clipboard->image().isNull()) {
        // Another application's image: centred, at its own pixel size (cut off at the canvas).
        const QImage image = clipboard->image().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        items.push_back({QStringLiteral("Pasted"), image,
                         QPoint((canvas.width() - image.width()) / 2, (canvas.height() - image.height()) / 2)});
    } else {
        return QStringLiteral("There is nothing to paste.");
    }
    if (m_document.count() + int(items.size()) > maxLayers())
        return QStringLiteral("This canvas can have at most %1 layers.").arg(maxLayers());
    // New layers above the topmost selected layer, bottom to top as copied; one undo step.
    int index = 0;
    for (const quint64 id : selectedLayerIds())
        index = std::max(index, m_document.indexOf(id) + 1);
    std::vector<UndoCommand> children;
    std::vector<quint64> added;
    QRect changed;
    // Every new layer is made before the document changes, so running out of memory changes nothing.
    std::vector<TiledImage> images;
    for (const ClipItem &item : items) {
        images.emplace_back(m_document.size());
        if (!images.back().write(item.origin, item.image))
            return QStringLiteral("Not enough memory to paste (%1). Nothing was changed.")
                .arg(MemoryBudget::shortageText());
    }
    for (size_t i = 0; i < items.size(); ++i) {
        const ClipItem &item = items[i];
        LayerData layer;
        layer.id = m_document.allocateId();
        layer.name = items.size() == 1 && item.name == QLatin1String("Pasted") ? item.name
                                                                               : QStringLiteral("%1 (pasted)").arg(item.name);
        layer.image = std::move(images[i]);
        const quint64 id = layer.id;
        m_document.insertLayer(index, std::move(layer));
        children.push_back(UndoCommand::layerInserted(id, index, QStringLiteral("Paste")));
        added.push_back(id);
        changed |= QRect(item.origin, item.image.size()) & canvas;
        ++index;
    }
    m_document.setActiveLayer(added.back());
    m_selectedLayers = QSet<quint64>(added.begin(), added.end()); // ready to transform together
    pushCommand(children.size() == 1 ? std::move(children.front()) : UndoCommand::group(std::move(children), QStringLiteral("Paste")));
    refreshComposite(changed);
    markContentChanged();
    updateDocumentRect(changed);
    emit layersChanged();
    return {};
}

// --- drawing ---

void CanvasItem::drawOverlays(QPainter *painter)
{
    const QTransform toView = m_view.toQTransform();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    if (m_transform.isActive()) {
        // The box, eight scale handles, the rotation knob and the pivot.
        const QPolygonF quad = toView.map(m_transform.quad());
        QPainterPath box;
        box.addPolygon(quad);
        box.closeSubpath();
        painter->setRenderHint(QPainter::Antialiasing, true);
        strokeTwoTone(painter, box);
        const qreal zoom = m_view.zoom();
        const QPointF knob = toView.map(m_transform.handlePosition(TransformSession::Handle::Rotate, RotateKnobDistance / zoom));
        const QPointF top = toView.map(m_transform.handlePosition(TransformSession::Handle::Top, 0));
        QPainterPath stem;
        stem.moveTo(top);
        stem.lineTo(knob);
        strokeTwoTone(painter, stem);
        painter->setPen(QPen(QColor(20, 20, 20), 1.5));
        painter->setBrush(QColor(255, 255, 255));
        using H = TransformSession::Handle;
        for (H h : {H::TopLeft, H::Top, H::TopRight, H::Right, H::BottomRight, H::Bottom, H::BottomLeft, H::Left}) {
            const QPointF c = toView.map(m_transform.handlePosition(h, 0));
            painter->drawRect(QRectF(c.x() - 6, c.y() - 6, 12, 12));
        }
        painter->drawEllipse(knob, 7, 7);
        const QPointF pivot = toView.map(m_transform.pivot());
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 255, 255), 3));
        painter->drawEllipse(pivot, 6, 6);
        painter->setPen(QPen(QColor(30, 110, 230), 1.5));
        painter->drawEllipse(pivot, 6, 6);
        painter->drawLine(pivot - QPointF(10, 0), pivot + QPointF(10, 0));
        painter->drawLine(pivot - QPointF(0, 10), pivot + QPointF(0, 10));
    } else if (!m_selectionOutline.isEmpty()) {
        strokeTwoTone(painter, toView.map(m_selectionOutline));
    }
    if (!m_polygon.empty()) {
        // The polygon so far, a rubber band to the pointer and a ring on the first point.
        QPainterPath poly;
        poly.moveTo(m_polygon.front());
        for (const QPointF &p : m_polygon)
            poly.lineTo(p);
        if (m_hoverKnown)
            poly.lineTo(m_hoverDoc);
        painter->setRenderHint(QPainter::Antialiasing, true);
        strokeTwoTone(painter, toView.map(poly));
        const QPointF first = toView.map(m_polygon.front());
        painter->setPen(QPen(QColor(30, 110, 230), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(first, HandleHitRadius * 0.6, HandleHitRadius * 0.6);
    }
    if (m_transform.isActive() && m_transformLayerIds.empty() && !m_transformedOutline.isEmpty())
        strokeTwoTone(painter, toView.map(m_transformedOutline));
    if (m_pointerTask == PointerTask::Select && m_selectPoints.size() >= 2) {
        // The shape being drawn.
        QPainterPath shape;
        if (m_selectShape == LassoShape) {
            shape.moveTo(m_selectPoints.front());
            for (const QPointF &p : m_selectPoints)
                shape.lineTo(p);
        } else {
            const QRectF r = QRectF(m_selectPoints.front(), m_selectPoints.back()).normalized();
            if (m_selectShape == EllipseShape)
                shape.addEllipse(r);
            else
                shape.addRect(r);
        }
        painter->setRenderHint(QPainter::Antialiasing, true);
        strokeTwoTone(painter, toView.map(shape));
    }
    painter->restore();
}

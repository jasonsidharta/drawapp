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

void CanvasItem::setKeepAspect(bool keep)
{
    if (keep == m_keepAspect)
        return;
    m_keepAspect = keep;
    emit transformChanged();
}

const QImage &CanvasItem::displayedComposite() const
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
    if (m_transform.isActive())
        applyTransform();
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
    finishPendingEdits();
    setSelectionWithUndo(Selection::all(m_document.size()), QStringLiteral("Select all"));
}

void CanvasItem::deselect()
{
    finishPendingEdits();
    setSelectionWithUndo(Selection(), QStringLiteral("Deselect"));
}

void CanvasItem::invertSelection()
{
    finishPendingEdits();
    if (!m_document.selection().isActive()) {
        emit notice(QStringLiteral("There is no selection to invert."));
        return;
    }
    setSelectionWithUndo(m_document.selection().inverted(), QStringLiteral("Invert selection"));
}

void CanvasItem::reselect()
{
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
    std::vector<UndoCommand> children;
    QRect changedAll;
    for (const quint64 id : selectedLayerIds()) {
        QImage &layer = m_document.pixels(m_document.indexOf(id));
        TileSnapshot before;
        QRect changed;
        if (!selected.isActive()) {
            changed = RasterDocument::clear(layer, &before);
        } else {
            // Keeps layer × (1 − selection), the exact complement of what Copy takes.
            changed = selected.bounds();
            before.capture(layer, changed);
            for (int y = changed.top(); y <= changed.bottom(); ++y) {
                const uchar *s = selected.mask().constScanLine(y);
                QRgb *row = reinterpret_cast<QRgb *>(layer.scanLine(y));
                for (int x = changed.left(); x <= changed.right(); ++x) {
                    const QRgb p = row[x];
                    const quint32 c = s[x];
                    row[x] = qRgba(int(qRed(p) - mul255(qRed(p), c)), int(qGreen(p) - mul255(qGreen(p), c)),
                                   int(qBlue(p) - mul255(qBlue(p), c)), int(qAlpha(p) - mul255(qAlpha(p), c)));
                }
            }
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
    m_document.refreshComposite(changedAll);
    markContentChanged();
    updateDocumentRect(changedAll);
}

// --- selection gestures ---

void CanvasItem::beginSelectGesture(const QPointF &viewPos)
{
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
    std::vector<QImage> images;
    for (const quint64 id : ids)
        images.push_back(m_document.layer(m_document.indexOf(id)).image);
    if (!m_transform.begin(images, selected)) {
        if (ids.size() == 1)
            return QStringLiteral("The layer \"%1\" has nothing to transform.").arg(m_document.layer(m_document.indexOf(ids.front())).name);
        return QStringLiteral("The selected layers have nothing to transform.");
    }
    m_transformLayerIds = ids;
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
    const QRect now = m_transform.changedRect();
    const QRect area = (now | m_previewRect) & QRect(QPoint(0, 0), m_document.size());
    std::vector<LayerData> layers = m_document.layers(); // shallow: images are shared
    for (size_t i = 0; i < m_transformLayerIds.size(); ++i) {
        const int index = m_document.indexOf(m_transformLayerIds[i]);
        if (index < 0)
            continue;
        m_transform.render(int(i), m_previewLayers[i], area, quality);
        layers[size_t(index)].image = m_previewLayers[i];
    }
    compositeLayers(m_previewComposite, area, layers);
    m_previewRect = now;
    updateDocumentRect(area);
    updateAll(); // the box and handles
    emit transformChanged();
}

void CanvasItem::endTransformSession()
{
    m_transform.end();
    m_transformLayerIds.clear();
    m_previewLayers.clear();
    m_previewComposite = QImage();
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
    if (!m_transform.isActive())
        return;
    if (m_transform.isIdentity()) {
        endTransformSession(); // nothing moved: nothing to record
        updateAll();
        return;
    }
    // Transaction: first prepare everything that can fail (each layer's result, the undo tiles,
    // the new selection), then change the document. A failure before that point leaves the
    // document exactly as it was. Pixels moved beyond the canvas are cut off (layers are
    // canvas-sized).
    const QRect changed = m_transform.changedRect();
    struct Prepared
    {
        quint64 id;
        QImage region;
        TileSnapshot before;
    };
    std::vector<Prepared> prepared;
    Selection newSelection;
    bool ok = true;
    try {
        for (size_t i = 0; i < m_transformLayerIds.size() && ok; ++i) {
            const quint64 id = m_transformLayerIds[i];
            const int index = m_document.indexOf(id);
            if (index < 0) {
                ok = false;
                break;
            }
            Prepared p{id, m_transform.renderRegion(int(i), changed, TransformSession::Quality::Final), TileSnapshot()};
            // Also makes the layer's pixels private (a recovery snapshot may share them).
            QImage &layer = m_document.pixels(index);
            if (p.region.isNull() || layer.bits() == nullptr) {
                ok = false;
                break;
            }
            p.before.capture(layer, changed);
            if (p.before.byteSize() <= 0 && !changed.isEmpty()) {
                ok = false;
                break;
            }
            prepared.push_back(std::move(p));
        }
        if (ok && m_transform.hasSelection())
            newSelection = m_transform.transformedSelection();
    } catch (const std::bad_alloc &) {
        ok = false;
    }
    if (!ok || (m_transform.hasSelection() && !newSelection.isActive())) {
        emit notice(QStringLiteral("Not enough memory to apply the transform. Nothing was changed; the preview is still open."));
        return;
    }
    // Commit: copies into existing images only, which cannot fail.
    std::vector<UndoCommand> children;
    for (Prepared &p : prepared) {
        QImage &layer = m_document.pixels(m_document.indexOf(p.id));
        for (int y = 0; y < p.region.height(); ++y)
            std::memcpy(layer.scanLine(changed.top() + y) + changed.left() * 4, p.region.constScanLine(y),
                        size_t(p.region.width()) * 4);
        p.before.dropUnchanged(layer);
        if (!p.before.isEmpty())
            children.push_back(UndoCommand::pixels(p.id, std::move(p.before), QStringLiteral("Transform")));
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
    m_document.refreshComposite(changed);
    markContentChanged();
    endTransformSession();
    updateDocumentRect(changed);
    updateAll();
}

void CanvasItem::cancelTransform()
{
    if (!m_transform.isActive())
        return;
    // The document was never touched: dropping the preview restores it exactly.
    const QRect shown = m_previewRect;
    endTransformSession();
    updateDocumentRect(shown);
    updateAll();
}

void CanvasItem::setTransformValue(const QString &key, qreal value)
{
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
    if (!m_transform.isActive())
        return;
    m_transform.flipHorizontal();
    updateTransformPreview(TransformSession::Quality::Final);
}

void CanvasItem::flipTransformVertical()
{
    if (!m_transform.isActive())
        return;
    m_transform.flipVertical();
    updateTransformPreview(TransformSession::Quality::Final);
}

void CanvasItem::resetTransform()
{
    if (!m_transform.isActive())
        return;
    m_transform.reset();
    updateTransformPreview(TransformSession::Quality::Final);
}

void CanvasItem::beginTransformDrag(const QPointF &viewPos)
{
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
        const QImage &layer = source.image;
        QImage taken;
        QRect rect;
        if (selected.isActive()) {
            rect = selected.bounds();
            taken = layer.copy(rect);
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
            rect = layer.rect();
            taken = layer;
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
    for (const ClipItem &item : items) {
        LayerData layer;
        layer.id = m_document.allocateId();
        layer.name = items.size() == 1 && item.name == QLatin1String("Pasted") ? item.name
                                                                               : QStringLiteral("%1 (pasted)").arg(item.name);
        layer.image = RasterDocument::blank(m_document.size());
        {
            QPainter p(&layer.image);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(item.origin, item.image);
        }
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
    m_document.refreshComposite(changed);
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

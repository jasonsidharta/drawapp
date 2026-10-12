#include "UndoHistory.h"

#include "LayeredDocument.h"

#include <algorithm>

UndoCommand UndoCommand::pixels(quint64 layerId, TileSnapshot before, const QString &label)
{
    UndoCommand c;
    c.m_kind = Kind::Pixels;
    c.m_label = label;
    c.m_layerId = layerId;
    c.m_tiles = std::move(before);
    return c;
}

UndoCommand UndoCommand::layerInserted(quint64 layerId, int index, const QString &label)
{
    UndoCommand c;
    c.m_kind = Kind::Presence;
    c.m_label = label;
    c.m_layerId = layerId;
    c.m_index = index;
    return c;
}

UndoCommand UndoCommand::layerRemoved(LayerData removed, int index, const QString &label)
{
    UndoCommand c;
    c.m_kind = Kind::Presence;
    c.m_label = label;
    c.m_layerId = removed.id;
    c.m_index = index;
    c.m_stored = std::move(removed);
    return c;
}

UndoCommand UndoCommand::properties(quint64 layerId, const LayerProperties &before, const QString &label)
{
    UndoCommand c;
    c.m_kind = Kind::Properties;
    c.m_label = label;
    c.m_layerId = layerId;
    c.m_props = before;
    return c;
}

UndoCommand UndoCommand::moved(quint64 layerId, int fromIndex, const QString &label)
{
    UndoCommand c;
    c.m_kind = Kind::Move;
    c.m_label = label;
    c.m_layerId = layerId;
    c.m_index = fromIndex;
    return c;
}

UndoCommand UndoCommand::selection(const Selection &before, const QString &label)
{
    UndoCommand c;
    c.m_kind = Kind::Selection;
    c.m_label = label;
    c.m_selection = before;
    return c;
}

UndoCommand &UndoCommand::withSelection(const Selection &before)
{
    m_selection = before;
    return *this;
}

qsizetype UndoCommand::byteSize() const
{
    const qsizetype selectionBytes = m_selection ? m_selection->byteSize() : 0;
    switch (m_kind) {
    case Kind::Selection:
        return selectionBytes;
    case Kind::Pixels:
        return m_tiles.byteSize() + selectionBytes;
    case Kind::Presence:
        // Counted in full even if the pixels are still shared with another layer (duplicate).
        return m_stored ? m_stored->image.sizeInBytes() : 0;
    default:
        return 0;
    }
}

UndoCommand::Result UndoCommand::swap(LayeredDocument &doc)
{
    Result result;
    const QRect all(QPoint(0, 0), doc.size());
    switch (m_kind) {
    case Kind::Selection: {
        const Selection current = doc.selection();
        doc.setSelection(*m_selection);
        m_selection = current;
        result.selection = true;
        break;
    }
    case Kind::Pixels: {
        const int index = doc.indexOf(m_layerId);
        if (index < 0)
            return result;
        result.dirty = m_tiles.swapWith(doc.pixels(index));
        if (m_selection) {
            const Selection current = doc.selection();
            doc.setSelection(*m_selection);
            m_selection = current;
            result.selection = true;
        }
        break;
    }
    case Kind::Presence:
        result.structure = true;
        result.dirty = all;
        if (m_stored) {
            const quint64 id = m_stored->id;
            doc.insertLayer(m_index, std::move(*m_stored));
            m_stored.reset();
            result.activate = id;
        } else {
            const int index = doc.indexOf(m_layerId);
            if (index < 0)
                return {};
            m_index = index;
            m_stored = doc.takeLayer(index);
        }
        break;
    case Kind::Properties: {
        const int index = doc.indexOf(m_layerId);
        if (index < 0)
            return result;
        LayerData &layer = doc.layerRef(index);
        const LayerProperties current{layer.name, layer.visible, layer.opacity, layer.locked};
        const bool pixelsChange = current.visible != m_props.visible || current.opacity != m_props.opacity;
        layer.name = m_props.name;
        layer.visible = m_props.visible;
        layer.opacity = m_props.opacity;
        layer.locked = m_props.locked;
        m_props = current;
        result.structure = true;
        if (pixelsChange)
            result.dirty = all;
        break;
    }
    case Kind::Move: {
        const int index = doc.indexOf(m_layerId);
        if (index < 0)
            return result;
        doc.moveLayer(index, m_index);
        m_index = index;
        result.structure = true;
        result.dirty = all;
        break;
    }
    }
    return result;
}

UndoHistory::UndoHistory(qsizetype budgetBytes)
    : m_budget(std::max<qsizetype>(0, budgetBytes))
{
}

void UndoHistory::push(UndoCommand command)
{
    for (const Entry &entry : m_redo)
        m_bytes -= entry.bytes;
    m_redo.clear();
    const qsizetype bytes = command.byteSize();
    m_bytes += bytes;
    m_undo.push_back({std::move(command), m_nextId++, bytes});
    enforceBudget();
}

UndoCommand::Result UndoHistory::undo(LayeredDocument &doc)
{
    if (m_undo.empty())
        return {};
    Entry entry = std::move(m_undo.back());
    m_undo.pop_back();
    const UndoCommand::Result result = entry.command.swap(doc);
    m_bytes -= entry.bytes;
    entry.bytes = entry.command.byteSize();
    m_bytes += entry.bytes;
    m_redo.push_back(std::move(entry));
    enforceBudget();
    return result;
}

UndoCommand::Result UndoHistory::redo(LayeredDocument &doc)
{
    if (m_redo.empty())
        return {};
    Entry entry = std::move(m_redo.back());
    m_redo.pop_back();
    const UndoCommand::Result result = entry.command.swap(doc);
    m_bytes -= entry.bytes;
    entry.bytes = entry.command.byteSize();
    m_bytes += entry.bytes;
    m_undo.push_back(std::move(entry));
    enforceBudget();
    return result;
}

quint64 UndoHistory::stateId() const
{
    for (auto it = m_undo.rbegin(); it != m_undo.rend(); ++it)
        if (it->command.changesArtwork())
            return it->id;
    return m_baseId;
}

void UndoHistory::enforceBudget()
{
    // The undo stack's bottom state becomes the evicted entry's resulting state.
    while (m_bytes > m_budget && !m_undo.empty()) {
        m_bytes -= m_undo.front().bytes;
        if (m_undo.front().command.changesArtwork())
            m_baseId = m_undo.front().id;
        m_undo.pop_front();
    }
    while (m_bytes > m_budget && !m_redo.empty()) {
        m_bytes -= m_redo.front().bytes;
        m_redo.pop_front();
    }
}

void UndoHistory::relabelLast(const QString &label)
{
    if (!m_undo.empty())
        m_undo.back().command.setLabel(label);
}

void UndoHistory::clear()
{
    m_undo.clear();
    m_redo.clear();
    m_bytes = 0;
    m_baseId = m_nextId++;
}

#include "UndoHistory.h"

#include <algorithm>

UndoHistory::UndoHistory(qsizetype budgetBytes)
    : m_budget(std::max<qsizetype>(0, budgetBytes))
{
}

void UndoHistory::push(TileSnapshot before, const QString &label)
{
    for (const Entry &entry : m_redo)
        m_bytes -= entry.tiles.byteSize();
    m_redo.clear();

    m_bytes += before.byteSize();
    m_undo.push_back({std::move(before), label, m_nextId++});

    // The undo stack's bottom state becomes the evicted entry's resulting state.
    while (m_bytes > m_budget && !m_undo.empty()) {
        m_bytes -= m_undo.front().tiles.byteSize();
        m_baseId = m_undo.front().id;
        m_undo.pop_front();
    }
}

QRect UndoHistory::undo(QImage &image)
{
    if (m_undo.empty())
        return {};
    Entry entry = std::move(m_undo.back());
    m_undo.pop_back();
    const QRect changed = entry.tiles.swapWith(image);
    m_redo.push_back(std::move(entry));
    return changed;
}

QRect UndoHistory::redo(QImage &image)
{
    if (m_redo.empty())
        return {};
    Entry entry = std::move(m_redo.back());
    m_redo.pop_back();
    const QRect changed = entry.tiles.swapWith(image);
    m_undo.push_back(std::move(entry));
    return changed;
}

void UndoHistory::clear()
{
    m_undo.clear();
    m_redo.clear();
    m_bytes = 0;
    m_baseId = m_nextId++;
}

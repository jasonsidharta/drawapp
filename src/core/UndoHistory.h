#pragma once

#include "TileSnapshot.h"

#include <QImage>
#include <QRect>
#include <QString>
#include <deque>

// Linear undo/redo of artwork edits under one memory budget shared by undo and redo entries.
// Each entry holds the tiles one edit changed; undo and redo swap them with the document, so an
// entry costs the same memory on either side. Pushing an edit drops the redo branch, then evicts
// the oldest undo entries until the total fits the budget. An edit larger than the whole budget
// is applied but cannot be undone.
class UndoHistory
{
public:
    static constexpr qsizetype DefaultBudgetBytes = qsizetype(128) * 1024 * 1024;

    explicit UndoHistory(qsizetype budgetBytes = DefaultBudgetBytes);

    // Records an edit already applied to the document. `before` holds the changed tiles' old pixels.
    void push(TileSnapshot before, const QString &label);
    // Swaps the newest undo entry into image. Returns the changed rect (empty if nothing to undo).
    QRect undo(QImage &image);
    QRect redo(QImage &image);
    // Forgets everything (new or opened document) and starts a fresh state id.
    void clear();

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    QString undoLabel() const { return m_undo.empty() ? QString() : m_undo.back().label; }
    QString redoLabel() const { return m_redo.empty() ? QString() : m_redo.back().label; }
    int undoCount() const { return int(m_undo.size()); }
    int redoCount() const { return int(m_redo.size()); }
    qsizetype bytesUsed() const { return m_bytes; }
    qsizetype budgetBytes() const { return m_budget; }

    // Identifies the artwork state reached through history. Equal ids mean identical pixels, so
    // comparing with the id recorded at save time tells whether there are unsaved changes.
    quint64 stateId() const { return m_undo.empty() ? m_baseId : m_undo.back().id; }

private:
    struct Entry
    {
        TileSnapshot tiles;
        QString label;
        quint64 id;
    };

    qsizetype m_budget;
    std::deque<Entry> m_undo;
    std::deque<Entry> m_redo;
    qsizetype m_bytes = 0;
    quint64 m_nextId = 1;
    quint64 m_baseId = 0;
};

#pragma once

#include "DocumentData.h"
#include "TileSnapshot.h"

#include <QRect>
#include <QString>
#include <deque>
#include <optional>

class LayeredDocument;

struct LayerProperties
{
    QString name;
    bool visible = true;
    qreal opacity = 1.0;
    bool locked = false;
};

// One undoable edit, already applied to the document when it is recorded. swap() exchanges the
// document's state with the state the command holds, so the same call undoes and redoes.
// Commands find their layer by stable id, never by position.
class UndoCommand
{
public:
    // Pixels of one layer changed; before holds the changed tiles' previous pixels.
    static UndoCommand pixels(quint64 layerId, TileSnapshot before, const QString &label);
    // A layer was inserted (add, duplicate) at index; the document holds it.
    static UndoCommand layerInserted(quint64 layerId, int index, const QString &label);
    // A layer was removed from index; the command keeps it.
    static UndoCommand layerRemoved(LayerData removed, int index, const QString &label);
    // Name/visibility/opacity/lock changed; before holds the previous values.
    static UndoCommand properties(quint64 layerId, const LayerProperties &before, const QString &label);
    // A layer moved; fromIndex is where it was.
    static UndoCommand moved(quint64 layerId, int fromIndex, const QString &label);

    struct Result
    {
        QRect dirty;           // document area whose composite changed
        bool structure = false; // layers were added, removed, reordered or changed properties
        quint64 activate = 0;   // layer to make active, 0 = keep the current choice
    };
    Result swap(LayeredDocument &doc);

    QString label() const { return m_label; }
    // Pixel memory this command retains right now (tile copies or a removed layer's image).
    qsizetype byteSize() const;

private:
    enum class Kind { Pixels, Presence, Properties, Move };
    Kind m_kind = Kind::Pixels;
    QString m_label;
    quint64 m_layerId = 0;
    TileSnapshot m_tiles;
    std::optional<LayerData> m_stored; // Presence: the layer while it is not in the document
    int m_index = 0;                   // Presence: position to restore; Move: the other position
    LayerProperties m_props;
};

// Linear undo/redo under one memory budget shared by undo and redo entries. Pushing an edit drops
// the redo branch. Whenever the total exceeds the budget (after a push, undo or redo), the oldest
// undo entries are evicted first, then the farthest redo entries. An edit larger than the whole
// budget is applied but cannot be undone.
class UndoHistory
{
public:
    static constexpr qsizetype DefaultBudgetBytes = qsizetype(128) * 1024 * 1024;

    explicit UndoHistory(qsizetype budgetBytes = DefaultBudgetBytes);

    void push(UndoCommand command);
    UndoCommand::Result undo(LayeredDocument &doc);
    UndoCommand::Result redo(LayeredDocument &doc);
    void clear();

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    QString undoLabel() const { return m_undo.empty() ? QString() : m_undo.back().command.label(); }
    QString redoLabel() const { return m_redo.empty() ? QString() : m_redo.back().command.label(); }
    int undoCount() const { return int(m_undo.size()); }
    int redoCount() const { return int(m_redo.size()); }
    qsizetype bytesUsed() const { return m_bytes; }
    qsizetype budgetBytes() const { return m_budget; }

    // Identifies the document state reached through history; compared with the id recorded at
    // save time to tell whether there are unsaved changes.
    quint64 stateId() const { return m_undo.empty() ? m_baseId : m_undo.back().id; }

private:
    struct Entry
    {
        UndoCommand command;
        quint64 id;
        qsizetype bytes;
    };
    void enforceBudget();

    qsizetype m_budget;
    std::deque<Entry> m_undo;
    std::deque<Entry> m_redo;
    qsizetype m_bytes = 0;
    quint64 m_nextId = 1;
    quint64 m_baseId = 0;
};

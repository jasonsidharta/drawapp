#pragma once

#include "DocumentData.h"
#include "Selection.h"
#include "TileSnapshot.h"

#include <QRect>
#include <QString>
#include <deque>
#include <optional>
#include <vector>

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
    // The selection changed; before is the previous selection. Selection changes do not change
    // artwork, so they never make the document "unsaved" (see UndoHistory::stateId).
    static UndoCommand selection(const Selection &before, const QString &label);
    // Adds a selection change to a pixel or group command, undone and redone with it.
    UndoCommand &withSelection(const Selection &before);
    // Several commands as one undo step (a multi-layer transform, cut or paste). Undo swaps them
    // in reverse order, redo in order.
    static UndoCommand group(std::vector<UndoCommand> children, const QString &label);

    struct Result
    {
        QRect dirty;           // document area whose composite changed
        bool structure = false; // layers were added, removed, reordered or changed properties
        quint64 activate = 0;   // layer to make active, 0 = keep the current choice
        bool selection = false; // the selection changed
    };
    Result swap(LayeredDocument &doc);

    QString label() const { return m_label; }
    void setLabel(const QString &label) { m_label = label; }
    // Pixel memory this command retains right now (tile copies, a removed layer's image or a
    // selection mask).
    qsizetype byteSize() const;
    // False for selection-only commands.
    bool changesArtwork() const;

private:
    enum class Kind { Pixels, Presence, Properties, Move, Selection, Group };
    Kind m_kind = Kind::Pixels;
    QString m_label;
    quint64 m_layerId = 0;
    TileSnapshot m_tiles;
    std::optional<LayerData> m_stored; // Presence: the layer while it is not in the document
    int m_index = 0;                   // Presence: position to restore; Move: the other position
    LayerProperties m_props;
    std::optional<Selection> m_selection; // Selection, or Pixels/Group with a selection change
    std::vector<UndoCommand> m_children;  // Group
    bool m_applied = true;                // Group: the next swap undoes
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
    // Renames the newest undo entry (for an action built from another, such as Cut from Clear).
    void relabelLast(const QString &label);
    int undoCount() const { return int(m_undo.size()); }
    int redoCount() const { return int(m_redo.size()); }
    qsizetype bytesUsed() const { return m_bytes; }
    qsizetype budgetBytes() const { return m_budget; }

    // Identifies the artwork state reached through history; compared with the id recorded at
    // save time to tell whether there are unsaved changes. Selection-only entries are skipped:
    // selections are not saved, so changing one does not make the document unsaved.
    quint64 stateId() const;

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

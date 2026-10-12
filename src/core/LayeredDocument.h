#pragma once

#include "DocumentData.h"
#include "Selection.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <vector>

// The open document: an ordered stack of raster layers (index 0 = bottom), an active layer, and
// a cached composite of the visible layers that the view draws. Only this class and UndoCommand
// mutate layers; callers refresh the composite for the area they changed.
class LayeredDocument
{
public:
    static constexpr int DefaultWidth = 2048;
    static constexpr int DefaultHeight = 1536;
    static constexpr int MaxLayers = DocumentLimits::MaxLayers;
    // Layers are sparse, so the count no longer depends on the canvas size; memory is checked by
    // MemoryBudget when pixels are allocated (milestone 10).
    static int maxLayersFor(QSize size);

    // One transparent layer named "Layer 1".
    explicit LayeredDocument(QSize size = QSize(DefaultWidth, DefaultHeight));
    // Takes a validated document (unique non-zero ids, matching image sizes, at least one layer).
    explicit LayeredDocument(const DocumentData &data);

    QSize size() const { return m_size; }
    int count() const { return int(m_layers.size()); }
    const LayerData &layer(int index) const { return m_layers[size_t(index)]; }
    int indexOf(quint64 id) const;
    const std::vector<LayerData> &layers() const { return m_layers; }

    quint64 activeLayerId() const { return m_activeId; }
    int activeIndex() const { return indexOf(m_activeId); }
    void setActiveLayer(quint64 id);

    // The composite of the visible layers (tiled, sparse: tiles where nothing is visible stay
    // unallocated). Refreshing returns false if the memory budget refused composite tiles.
    const TiledImage &composite() const { return m_composite; }
    bool refreshComposite(const QRect &rect);
    bool refreshComposite() { return refreshComposite(m_composite.rect()); }

    // Shallow copy for saving and recovery: images are shared until either side writes. The
    // selection is not part of it: selections are not saved in project or recovery files.
    DocumentData snapshot() const;

    // The selection (document space, separate from layer pixels). Changing it never changes
    // artwork; record a selection undo command when the user changes it.
    const Selection &selection() const { return m_selection; }
    void setSelection(const Selection &selection) { m_selection = selection; }

    // --- mutations (no history; callers record undo commands) ---
    TiledImage &pixels(int index) { return m_layers[size_t(index)].image; }
    LayerData &layerRef(int index) { return m_layers[size_t(index)]; }
    quint64 allocateId() { return m_nextId++; }
    QString nextLayerName() const;
    void insertLayer(int index, LayerData layer);
    // Removes and returns a layer. If it was active, the layer below becomes active (or, for the
    // bottom layer, the one that is now at the bottom).
    LayerData takeLayer(int index);
    void moveLayer(int from, int to);

private:
    QSize m_size;
    std::vector<LayerData> m_layers;
    quint64 m_activeId = 0;
    quint64 m_nextId = 1;
    TiledImage m_composite;
    Selection m_selection;
};

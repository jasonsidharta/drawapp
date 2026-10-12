#include "LayeredDocument.h"

#include "RasterDocument.h"

#include <algorithm>

int LayeredDocument::maxLayersFor(QSize size)
{
    const qint64 layerBytes = qint64(std::max(1, size.width())) * std::max(1, size.height()) * 4;
    return int(std::clamp<qint64>(MaxPixelBytes / layerBytes, 1, MaxLayers));
}

LayeredDocument::LayeredDocument(QSize size)
    : m_size(size)
    , m_composite(RasterDocument::blank(size))
{
    LayerData layer;
    layer.id = allocateId();
    layer.name = QStringLiteral("Layer 1");
    layer.image = RasterDocument::blank(size);
    m_layers.push_back(layer);
    m_activeId = layer.id;
}

LayeredDocument::LayeredDocument(const DocumentData &data)
    : m_size(data.size)
    , m_layers(data.layers)
    , m_composite(data.size, QImage::Format_ARGB32_Premultiplied)
{
    for (const LayerData &layer : m_layers)
        m_nextId = std::max(m_nextId, layer.id + 1);
    m_activeId = indexOf(data.activeLayerId) >= 0 ? data.activeLayerId : m_layers.back().id;
    refreshComposite();
}

int LayeredDocument::indexOf(quint64 id) const
{
    for (size_t i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].id == id)
            return int(i);
    }
    return -1;
}

void LayeredDocument::setActiveLayer(quint64 id)
{
    if (indexOf(id) >= 0)
        m_activeId = id;
}

void LayeredDocument::refreshComposite(const QRect &rect)
{
    compositeLayers(m_composite, rect, m_layers);
}

DocumentData LayeredDocument::snapshot() const
{
    DocumentData data;
    data.size = m_size;
    data.layers = m_layers;
    data.activeLayerId = m_activeId;
    return data;
}

QString LayeredDocument::nextLayerName() const
{
    for (int n = count() + 1;; ++n) {
        const QString name = QStringLiteral("Layer %1").arg(n);
        if (std::none_of(m_layers.begin(), m_layers.end(), [&](const LayerData &l) { return l.name == name; }))
            return name;
    }
}

void LayeredDocument::insertLayer(int index, LayerData layer)
{
    index = std::clamp(index, 0, count());
    m_nextId = std::max(m_nextId, layer.id + 1);
    m_layers.insert(m_layers.begin() + index, std::move(layer));
}

LayerData LayeredDocument::takeLayer(int index)
{
    LayerData layer = std::move(m_layers[size_t(index)]);
    m_layers.erase(m_layers.begin() + index);
    if (layer.id == m_activeId && !m_layers.empty())
        m_activeId = m_layers[size_t(std::max(0, index - 1))].id;
    return layer;
}

void LayeredDocument::moveLayer(int from, int to)
{
    to = std::clamp(to, 0, count() - 1);
    if (from == to)
        return;
    LayerData layer = std::move(m_layers[size_t(from)]);
    m_layers.erase(m_layers.begin() + from);
    m_layers.insert(m_layers.begin() + to, std::move(layer));
}

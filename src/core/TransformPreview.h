#pragma once

#include "DocumentData.h"
#include "TransformSession.h"

// A viewport-sized, per-layer preview. Static layer remainders are filtered once
// per viewport; moving sources are sampled directly at the destination LOD.
class TransformPreview
{
public:
    bool prepare(TransformSession &session, const std::vector<LayerData> &layers,
                 const std::vector<quint64> &ids, const QRect &levelArea, int level);
    QImage render(const TransformSession &session, TransformSession::Quality quality) const;
    QRect area() const { return m_area; }
    int level() const { return m_level; }
    qsizetype byteSize() const;
private:
    struct Layer { QImage base; int part = -1; qreal opacity = 1; };
    std::vector<Layer> m_layers;
    QImage m_identity;
    QRect m_area;
    int m_level = 0;
};

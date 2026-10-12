#include "DocumentData.h"

#include <QPainter>

DocumentData DocumentData::fromImage(const QImage &image, const QString &layerName)
{
    DocumentData doc;
    doc.size = image.size();
    LayerData layer;
    layer.id = 1;
    layer.name = layerName;
    layer.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    doc.layers.push_back(layer);
    doc.activeLayerId = 1;
    return doc;
}

QImage DocumentData::composite() const
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    compositeLayers(out, out.rect(), layers);
    return out;
}

void compositeLayers(QImage &target, const QRect &rect, const std::vector<LayerData> &layers)
{
    const QRect r = rect & target.rect();
    if (r.isEmpty())
        return;
    QPainter painter(&target);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(r, Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (const LayerData &layer : layers) {
        if (!layer.visible || layer.opacity <= 0.0)
            continue;
        painter.setOpacity(layer.opacity);
        painter.drawImage(r.topLeft(), layer.image, r);
    }
}

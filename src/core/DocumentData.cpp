#include "DocumentData.h"

#include <QPainter>

DocumentData DocumentData::fromImage(const QImage &image, const QString &layerName)
{
    DocumentData doc;
    doc.size = image.size();
    LayerData layer;
    layer.id = 1;
    layer.name = layerName;
    layer.image = TiledImage::fromImage(image);
    doc.layers.push_back(layer);
    doc.activeLayerId = 1;
    return doc;
}

QImage DocumentData::composite() const
{
    const qint64 bytes = qint64(size.width()) * size.height() * 4;
    if (!MemoryBudget::tryReserve(bytes, MemoryBudget::Category::Temporary))
        return QImage();
    MemoryBudget::release(bytes, MemoryBudget::Category::Temporary);
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    if (out.isNull())
        return out;
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
        if (!layer.visible || layer.opacity <= 0.0 || layer.image.isNull())
            continue;
        painter.setOpacity(layer.opacity);
        const TiledImage &img = layer.image;
        const QRect inside = r & img.rect();
        if (inside.isEmpty())
            continue;
        for (int row = img.rowOf(inside.top()); row <= img.rowOf(inside.bottom()); ++row)
            for (int column = img.columnOf(inside.left()); column <= img.columnOf(inside.right()); ++column) {
                const QImage tile = img.tileImage(column, row);
                if (tile.isNull())
                    continue;
                const QRect part = img.tileRect(column, row) & inside;
                painter.drawImage(part.topLeft(), tile, part.translated(-column * img.tileSize(), -row * img.tileSize()));
            }
    }
}

bool compositeLayers(TiledImage &target, const QRect &rect, const std::vector<LayerData> &layers)
{
    const QRect r = rect & target.rect();
    if (r.isEmpty())
        return true;
    const int ts = target.tileSize();
    bool ok = true;
    for (int row = target.rowOf(r.top()); row <= target.rowOf(r.bottom()); ++row) {
        for (int column = target.columnOf(r.left()); column <= target.columnOf(r.right()); ++column) {
            const QRect tileRect = target.tileRect(column, row);
            const QRect part = tileRect & r;
            // Which visible layers have paint here (same tile grid as the target).
            bool any = false;
            for (const LayerData &layer : layers)
                if (layer.visible && layer.opacity > 0.0 && layer.image.hasTile(column, row)) {
                    any = true;
                    break;
                }
            if (!any) {
                if (!target.hasTile(column, row))
                    continue;
                if (part == tileRect) {
                    target.releaseTile(column, row);
                    continue;
                }
                ok = target.fillRect(part, 0) && ok;
                continue;
            }
            QImage out = target.writableTileImage(column, row);
            if (out.isNull()) {
                ok = false;
                continue;
            }
            const QRect local = part.translated(-column * ts, -row * ts);
            QPainter painter(&out);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(local, Qt::transparent);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            for (const LayerData &layer : layers) {
                if (!layer.visible || layer.opacity <= 0.0)
                    continue;
                const QImage tile = layer.image.tileImage(column, row);
                if (tile.isNull())
                    continue;
                painter.setOpacity(layer.opacity);
                painter.drawImage(local.topLeft(), tile, local);
            }
        }
    }
    return ok;
}

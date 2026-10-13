#include "DocumentData.h"

#include <QPainter>
#include <cstring>

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

namespace {

// A small area spanning several tiles (a brush event): composited once into a dense buffer with one
// painter and one draw per layer, as milestone 9 did for its rectangles, then copied into the
// tiles. The drawing operations per pixel are the same as compositing tile by tile, so the pixels
// are too (tst_tiles checks both against each other).
bool compositeSmall(TiledImage &target, const QRect &r, const std::vector<LayerData> &layers)
{
    static thread_local QImage buffer, layerPixels;
    if (buffer.isNull() || layerPixels.isNull() || buffer.width() < r.width() || buffer.height() < r.height()) {
        buffer = QImage(std::max(buffer.width(), r.width()), std::max(buffer.height(), r.height()),
                        QImage::Format_ARGB32_Premultiplied);
        layerPixels = QImage(buffer.size(), QImage::Format_ARGB32_Premultiplied);
        if (buffer.isNull() || layerPixels.isNull())
            return false;
    }
    const qsizetype bpl = buffer.bytesPerLine();
    QImage out(buffer.bits(), r.width(), r.height(), bpl, QImage::Format_ARGB32_Premultiplied);
    QImage in(layerPixels.bits(), r.width(), r.height(), bpl, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter painter(&out);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        for (const LayerData &layer : layers) {
            if (!layer.visible || layer.opacity <= 0.0 || layer.image.allocatedBytes(r) == 0)
                continue;
            layer.image.readInto(r, in.bits(), bpl);
            painter.setOpacity(layer.opacity);
            painter.drawImage(QPoint(0, 0), in);
        }
    }
    return target.write(r.topLeft(), out) && (target.pruneTransparent(r), true);
}

} // namespace

bool compositeLayers(TiledImage &target, const QRect &rect, const std::vector<LayerData> &layers)
{
    const QRect r = rect & target.rect();
    if (r.isEmpty())
        return true;
    const int ts = target.tileSize();
    // Brush-sized areas over several tiles: one painter instead of one per tile.
    if (qint64(r.width()) * r.height() <= 192 * 192 && (target.columnOf(r.left()) != target.columnOf(r.right())
                                                        || target.rowOf(r.top()) != target.rowOf(r.bottom())))
        return compositeSmall(target, r, layers);
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
            uchar *dst = target.writableTileData(column, row);
            if (!dst) {
                ok = false;
                continue;
            }
            // Drawn into one reused scratch tile, then copied: a QPainter on a new QImage wrapper
            // would build a new raster engine for every tile (measured as most of the cost).
            static thread_local QImage scratch;
            if (scratch.width() != ts)
                scratch = QImage(ts, ts, QImage::Format_ARGB32_Premultiplied);
            if (scratch.isNull()) {
                ok = false;
                continue;
            }
            const QRect local = part.translated(-column * ts, -row * ts);
            {
                QPainter painter(&scratch);
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
            const int stride = target.tileStride();
            for (int y = local.top(); y <= local.bottom(); ++y)
                std::memcpy(dst + qsizetype(y) * stride + local.left() * 4, scratch.constScanLine(y) + local.left() * 4,
                            size_t(local.width()) * 4);
        }
    }
    return ok;
}

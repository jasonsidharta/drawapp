#include "TransformPreview.h"

#include "BudgetImage.h"
#include "ViewPyramid.h"
#include <QPainter>
#include <algorithm>
#include <cstring>

bool TransformPreview::prepare(TransformSession &session, const std::vector<LayerData> &layers,
                               const std::vector<quint64> &ids, const QRect &area, int level)
try {
    MemoryBudget::PreserveHistory preserve;
    session.prepareDisplaySources();
    TransformPreview next;
    next.m_area = area;
    next.m_level = level;
    next.m_identity = BudgetImage::create(area.size(), QImage::Format_ARGB32_Premultiplied,
                                          MemoryBudget::Category::Preview);
    if (next.m_identity.isNull())
        return false;
    next.m_identity.fill(Qt::transparent);
    QPainter identity(&next.m_identity);
    for (const LayerData &layer : layers) {
        if (!layer.visible || layer.opacity <= 0)
            continue;
        ViewPyramid pyramid;
        const QImage original = pyramid.pixels(layer.image, level, area);
        if (original.isNull())
            return false;
        identity.setOpacity(layer.opacity);
        identity.drawImage(QPoint(), original);
        const auto found = std::find(ids.begin(), ids.end(), layer.id);
        const int part = found == ids.end() ? -1 : int(found - ids.begin());
        QImage base;
        if (part < 0) {
            base = BudgetImage::copy(original, original.rect(), MemoryBudget::Category::Preview);
        } else if (!session.hasSelection()) {
            // Without a selection every painted pixel is part of the source.
            base = BudgetImage::create(area.size(), QImage::Format_ARGB32_Premultiplied,
                                       MemoryBudget::Category::Preview);
            if (!base.isNull())
                base.fill(Qt::transparent);
        } else {
            const TiledImage remainder = session.baseLayer(part);
            if (remainder.isNull())
                return false;
            const QImage pixels = pyramid.pixels(remainder, level, area);
            base = BudgetImage::copy(pixels, pixels.rect(), MemoryBudget::Category::Preview);
        }
        if (base.isNull())
            return false;
        next.m_layers.push_back({std::move(base), part, layer.opacity});
    }
    identity.end();
    *this = std::move(next);
    return true;
} catch (const std::bad_alloc &) {
    return false;
}

QImage TransformPreview::render(const TransformSession &session, TransformSession::Quality quality) const
{
    if (session.isIdentity())
        return m_identity;
    QImage out = BudgetImage::create(m_area.size(), QImage::Format_ARGB32_Premultiplied,
                                    MemoryBudget::Category::Preview);
    QImage work = BudgetImage::create(m_area.size(), QImage::Format_ARGB32_Premultiplied,
                                     MemoryBudget::Category::Preview);
    if (out.isNull() || work.isNull())
        return {};
    out.fill(Qt::transparent);
    QPainter painter(&out);
    for (const Layer &layer : m_layers) {
        const QImage *pixels = &layer.base;
        if (layer.part >= 0) {
            std::memcpy(work.bits(), layer.base.constBits(), size_t(work.sizeInBytes()));
            session.renderDisplaySource(layer.part, work, m_area, m_level, quality);
            pixels = &work;
        }
        painter.setOpacity(layer.opacity);
        painter.drawImage(QPoint(), *pixels);
    }
    painter.end();
    return out;
}

qsizetype TransformPreview::byteSize() const
{
    qsizetype bytes = m_identity.sizeInBytes();
    for (const Layer &layer : m_layers)
        bytes += layer.base.sizeInBytes();
    return bytes;
}

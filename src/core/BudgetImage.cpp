#include "BudgetImage.h"

#include <cstring>
#include <memory>

namespace {
struct Storage
{
    QImage pixels;
    qint64 bytes = 0;
    MemoryBudget::Category category;
    ~Storage()
    {
        pixels = QImage();
        MemoryBudget::release(bytes, category);
    }
};
}

QImage BudgetImage::create(QSize size, QImage::Format format, MemoryBudget::Category category)
{
    if (size.isEmpty() || (format != QImage::Format_ARGB32_Premultiplied && format != QImage::Format_Grayscale8))
        return {};
    const qint64 stride = (qint64(size.width()) * (format == QImage::Format_Grayscale8 ? 1 : 4) + 3) & ~qint64(3);
    const qint64 bytes = stride * size.height();
    auto storage = std::make_unique<Storage>();
    storage->category = category;
    if (!MemoryBudget::tryReserve(bytes, category))
        return {};
    storage->bytes = bytes;
    storage->pixels = QImage(size, format);
    if (storage->pixels.isNull())
        return {};
    QImage out(storage->pixels.bits(), size.width(), size.height(), stride, format,
               [](void *p) { delete static_cast<Storage *>(p); }, storage.get());
    if (out.isNull())
        return {};
    storage.release();
    return out;
}

QImage BudgetImage::copy(const QImage &source, const QRect &area, MemoryBudget::Category category)
{
    const QRect r = area & source.rect();
    QImage out = create(r.size(), source.format(), category);
    if (out.isNull())
        return {};
    const int pixelBytes = source.depth() / 8;
    for (int y = 0; y < r.height(); ++y)
        std::memcpy(out.scanLine(y), source.constScanLine(y + r.top()) + qsizetype(r.left()) * pixelBytes,
                    size_t(r.width()) * pixelBytes);
    return out;
}

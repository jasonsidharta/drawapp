#pragma once

#include "MemoryBudget.h"
#include <QImage>

// Dense storage charged until its last QImage reader releases it. Shared images
// are immutable; callers needing a writable copy must allocate another here.
namespace BudgetImage {
QImage create(QSize size, QImage::Format format, MemoryBudget::Category category);
QImage copy(const QImage &source, const QRect &area, MemoryBudget::Category category);
}

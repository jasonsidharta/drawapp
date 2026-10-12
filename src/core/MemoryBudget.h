#pragma once

#include <QtGlobal>
#include <functional>

// The process-wide budget for large pixel allocations (milestone 10, docs/m10-memory-budget.md).
//
// Every tile of every TiledImage and every large temporary buffer (transform sources) reserves
// its bytes here before it is allocated and releases them when it is freed. Shared tiles are one
// allocation, so they are counted once. A reservation that would exceed the limit first asks the
// registered evictors (reconstructible caches) for memory, but only on the thread that owns them;
// if that is not enough it fails, and the caller refuses the operation instead of crashing.
//
// The limit defaults to a share of physical memory (50 % on desktop, 25 % on Android, capped on
// 32-bit builds). DRAWAPP_MEMORY_LIMIT_MB overrides it; tests set it directly to provoke
// allocation failures deterministically.
namespace MemoryBudget {

enum class Category {
    Document,  // layer tiles (also when undo history is the last owner)
    Cache,     // reconstructible: composite pyramid, view caches
    Composite, // the composite (projection) tiles
    Preview,   // transform previews and their sources
    Temporary, // short-lived processing buffers
    Count
};
constexpr int CategoryCount = int(Category::Count);

// Reserves bytes in a category. False (nothing reserved) if the limit would be exceeded after
// eviction. Thread-safe.
bool tryReserve(qint64 bytes, Category category);
void release(qint64 bytes, Category category);

qint64 used(Category category);
qint64 usedTotal();
qint64 peakTotal();
void resetPeak();

qint64 limit();
void setLimit(qint64 bytes); // <= 0 restores the default
qint64 defaultLimit();
qint64 physicalMemory(); // bytes, 0 if unknown

// Frees reconstructible memory: called with the number of bytes wanted, returns bytes freed.
// Evictors run only on the thread that registered them (the UI thread), never from a worker.
using Evictor = std::function<qint64(qint64 wanted)>;
// Lower `order` runs first: reconstructible caches (0) before undo history (10).
int addEvictor(Evictor evictor, int order = 0);
void removeEvictor(int id);
// Runs every evictor registered on the calling thread for at least `wanted` bytes (or all it
// can); returns bytes freed. Used before starting large background work.
qint64 evict(qint64 wanted);

// Tests: make the next `count` reservations fail after `after` successful ones (-1 = off).
void failReservationsForTesting(int after, int count);

} // namespace MemoryBudget

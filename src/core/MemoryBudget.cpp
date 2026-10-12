#include "MemoryBudget.h"

#include <algorithm>

#include <QMutex>
#include <QThread>
#include <QtGlobal>
#include <array>
#include <atomic>
#include <cstdlib>
#include <vector>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace {

std::array<std::atomic<qint64>, MemoryBudget::CategoryCount> s_used{};
std::atomic<qint64> s_total{0};
std::atomic<qint64> s_peak{0};
std::atomic<qint64> s_limit{0}; // 0 = not initialised yet

// Test hook: reservations to let through before failing, and how many to fail.
std::atomic<int> s_failAfter{-1};
std::atomic<int> s_failCount{0};

struct EvictorEntry
{
    int id;
    QThread *thread;
    MemoryBudget::Evictor evictor;
    int order = 0;
};
QMutex s_evictorMutex;
std::vector<EvictorEntry> s_evictors;
int s_nextEvictorId = 1;

qint64 effectiveLimit()
{
    qint64 limit = s_limit.load(std::memory_order_relaxed);
    if (limit <= 0) {
        limit = MemoryBudget::defaultLimit();
        s_limit.store(limit, std::memory_order_relaxed);
    }
    return limit;
}

bool testFailure()
{
    if (s_failAfter.load() < 0)
        return false;
    if (s_failAfter.load() > 0) {
        --s_failAfter;
        return false;
    }
    if (s_failCount.load() > 0) {
        if (--s_failCount == 0)
            s_failAfter = -1;
        return true;
    }
    s_failAfter = -1;
    return false;
}

bool reserve(qint64 bytes, MemoryBudget::Category category)
{
    const qint64 limit = effectiveLimit();
    qint64 total = s_total.load();
    do {
        if (total + bytes > limit)
            return false;
    } while (!s_total.compare_exchange_weak(total, total + bytes));
    s_used[size_t(category)] += bytes;
    const qint64 now = total + bytes;
    qint64 peak = s_peak.load();
    while (now > peak && !s_peak.compare_exchange_weak(peak, now)) {
    }
    return true;
}

} // namespace

bool MemoryBudget::tryReserve(qint64 bytes, Category category)
{
    if (bytes <= 0)
        return true;
    if (testFailure())
        return false;
    if (reserve(bytes, category))
        return true;
    // Over the limit: free reconstructible caches owned by this thread, then try again.
    if (evict(bytes) > 0 && reserve(bytes, category))
        return true;
    return false;
}

void MemoryBudget::release(qint64 bytes, Category category)
{
    if (bytes <= 0)
        return;
    s_used[size_t(category)] -= bytes;
    s_total -= bytes;
}

qint64 MemoryBudget::used(Category category)
{
    return s_used[size_t(category)].load();
}

qint64 MemoryBudget::usedTotal()
{
    return s_total.load();
}

qint64 MemoryBudget::peakTotal()
{
    return s_peak.load();
}

void MemoryBudget::resetPeak()
{
    s_peak = s_total.load();
}

qint64 MemoryBudget::limit()
{
    return effectiveLimit();
}

void MemoryBudget::setLimit(qint64 bytes)
{
    s_limit = bytes > 0 ? bytes : defaultLimit();
}

qint64 MemoryBudget::physicalMemory()
{
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
        return qint64(status.ullTotalPhys);
    return 0;
#elif defined(Q_OS_UNIX) && defined(_SC_PHYS_PAGES)
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && page > 0 ? qint64(pages) * page : 0;
#else
    return 0;
#endif
}

qint64 MemoryBudget::defaultLimit()
{
    if (const char *env = std::getenv("DRAWAPP_MEMORY_LIMIT_MB")) {
        const qint64 mb = std::atoll(env);
        if (mb > 0)
            return mb * 1024 * 1024;
    }
    const qint64 physical = physicalMemory();
#ifdef Q_OS_ANDROID
    // Native memory is not limited by the Java heap, but the system kills large apps first.
    qint64 limit = physical > 0 ? physical / 4 : qint64(768) * 1024 * 1024;
#else
    qint64 limit = physical > 0 ? physical / 2 : qint64(2048) * 1024 * 1024;
#endif
    if constexpr (sizeof(void *) == 4)
        limit = std::min<qint64>(limit, qint64(1200) * 1024 * 1024); // address space
    return std::max<qint64>(limit, qint64(256) * 1024 * 1024);
}

int MemoryBudget::addEvictor(Evictor evictor, int order)
{
    QMutexLocker lock(&s_evictorMutex);
    const int id = s_nextEvictorId++;
    s_evictors.push_back({id, QThread::currentThread(), std::move(evictor), order});
    std::stable_sort(s_evictors.begin(), s_evictors.end(),
                     [](const EvictorEntry &a, const EvictorEntry &b) { return a.order < b.order; });
    return id;
}

void MemoryBudget::removeEvictor(int id)
{
    QMutexLocker lock(&s_evictorMutex);
    for (auto it = s_evictors.begin(); it != s_evictors.end(); ++it)
        if (it->id == id) {
            s_evictors.erase(it);
            return;
        }
}

qint64 MemoryBudget::evict(qint64 wanted)
{
    // Copy the callbacks for this thread, then call them without the lock (they release memory,
    // which calls release()).
    std::vector<Evictor> mine;
    {
        QMutexLocker lock(&s_evictorMutex);
        for (const EvictorEntry &e : s_evictors)
            if (e.thread == QThread::currentThread())
                mine.push_back(e.evictor);
    }
    qint64 freed = 0;
    for (const Evictor &e : mine) {
        freed += e(wanted - freed);
        if (freed >= wanted)
            break;
    }
    return freed;
}

void MemoryBudget::failReservationsForTesting(int after, int count)
{
    s_failCount = count;
    s_failAfter = count > 0 ? after : -1;
}

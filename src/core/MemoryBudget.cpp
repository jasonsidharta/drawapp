#include "MemoryBudget.h"

#include <algorithm>

#include <QMutex>
#include <QThread>
#include <QtGlobal>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
std::atomic<qint64> s_floor{0}; // 0 = default
std::atomic<qint64> s_nextCheck{0}; // total from which the device's free memory is checked again
std::atomic<bool> s_lastRefusalSystem{false};
std::atomic<qint64> s_lastAvailable{-1};
constexpr qint64 CheckEvery = qint64(64) * 1024 * 1024;

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

namespace {

// True when the device has room for `bytes` more above the floor. Checked every CheckEvery bytes
// of growth (a system call per tile would be too costly).
bool deviceHasRoom(qint64 bytes)
{
    const qint64 total = s_total.load();
    if (total + bytes < s_nextCheck.load())
        return true;
    s_nextCheck.store(total + bytes + CheckEvery);
    const qint64 available = MemoryBudget::availablePhysicalMemory();
    s_lastAvailable.store(available);
    return available < 0 || available - bytes >= MemoryBudget::physicalFloor();
}

} // namespace

bool MemoryBudget::tryReserve(qint64 bytes, Category category)
{
    if (bytes <= 0)
        return true;
    if (testFailure()) {
        s_lastRefusalSystem = false;
        return false;
    }
    // More than the whole limit can never fit: refused without giving anything up (evicting
    // caches and undo steps for a request that fails anyway would only lose them).
    if (bytes > limit()) {
        s_lastRefusalSystem = false;
        return false;
    }
    if (!deviceHasRoom(bytes)) {
        // The device is low on memory (other programs count too): give up what is needed of the
        // caches and old undo steps, look again.
        const qint64 shortfall = std::max<qint64>(bytes, physicalFloor() + bytes - std::max<qint64>(0, s_lastAvailable.load()));
        evict(shortfall);
        s_nextCheck.store(0);
        if (!deviceHasRoom(bytes)) {
            s_lastRefusalSystem = true;
            s_nextCheck.store(0); // keep checking while it is low
            return false;
        }
    }
    if (reserve(bytes, category))
        return true;
    // Over the limit: ask this thread's evictors for exactly the shortfall, then try again.
    const qint64 shortfall = s_total.load() + bytes - limit();
    if (shortfall > 0 && evict(shortfall) > 0 && reserve(bytes, category))
        return true;
    s_lastRefusalSystem = false;
    return false;
}

qint64 MemoryBudget::availablePhysicalMemory()
{
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
        return qint64(status.ullAvailPhys);
    return -1;
#elif defined(Q_OS_UNIX)
    // Linux and Android: MemAvailable counts reclaimable caches; sysconf(_SC_AVPHYS_PAGES) is only
    // MemFree, which on the Android emulator was 325 MiB with 1980 MiB actually available, so the
    // guard refused ordinary work.
    if (FILE *f = std::fopen("/proc/meminfo", "r")) {
        char line[128];
        qint64 kb = -1;
        while (std::fgets(line, sizeof(line), f)) {
            if (std::strncmp(line, "MemAvailable:", 13) == 0) {
                kb = std::atoll(line + 13);
                break;
            }
        }
        std::fclose(f);
        if (kb >= 0)
            return kb * 1024;
    }
#if defined(_SC_AVPHYS_PAGES)
    const long pages = sysconf(_SC_AVPHYS_PAGES), size = sysconf(_SC_PAGESIZE);
    return pages > 0 && size > 0 ? qint64(pages) * size : -1;
#else
    return -1;
#endif
#else
    return -1;
#endif
}

qint64 MemoryBudget::physicalFloor()
{
    const qint64 set = s_floor.load();
    if (set > 0)
        return set;
    if (const char *env = std::getenv("DRAWAPP_MEMORY_FLOOR_MB")) {
        const qint64 mb = std::atoll(env);
        if (mb >= 0)
            return mb * 1024 * 1024;
    }
    return std::max(qint64(256) * 1024 * 1024, physicalMemory() / 32);
}

void MemoryBudget::setPhysicalFloorForTesting(qint64 bytes)
{
    s_floor.store(bytes > 0 ? bytes : 0);
    s_nextCheck.store(0);
}

QString MemoryBudget::shortageText()
{
    if (s_lastRefusalSystem.load()) {
        const qint64 available = s_lastAvailable.load();
        return available >= 0 ? QStringLiteral("the device's free memory, %1 MiB left").arg(available / (1024 * 1024))
                              : QStringLiteral("the device's free memory");
    }
    return QStringLiteral("the memory limit of %1 MiB").arg(limit() / (1024 * 1024));
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

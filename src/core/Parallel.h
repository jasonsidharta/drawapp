#pragma once

#include <QSemaphore>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <atomic>

// Runs fn(i) for i in [begin, end) on a pool of DrawApp's own (so a busy global pool, e.g. a
// save, cannot stall the caller); the calling thread takes part, so the work finishes even if no
// pool thread starts. Items are taken in batches of `batch`. fn must be safe to call concurrently
// for different i. Used for transform rows and for tile coding in project files (milestone 10).
template <typename F>
void parallelFor(int begin, int end, int batch, F &&fn)
{
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
        return true;
    }();
    Q_UNUSED(configured);
    batch = std::max(1, batch);
    const int items = end - begin;
    const int helpers = std::min(pool.maxThreadCount(), items / batch - 1);
    if (helpers <= 0) {
        for (int i = begin; i < end; ++i)
            fn(i);
        return;
    }
    std::atomic<int> next{begin};
    auto work = [&] {
        for (;;) {
            const int start = next.fetch_add(batch);
            if (start >= end)
                break;
            for (int i = start; i < std::min(end, start + batch); ++i)
                fn(i);
        }
    };
    QSemaphore done;
    int started = 0;
    for (int i = 0; i < helpers; ++i) {
        if (pool.tryStart([&] {
                work();
                done.release();
            }))
            ++started;
    }
    work();
    done.acquire(started);
}

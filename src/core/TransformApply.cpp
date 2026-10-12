#include "TransformApply.h"

#include <QMetaObject>
#include <QThread>

TransformApply::TransformApply(QObject *parent)
    : QObject(parent)
{
}

TransformApply::~TransformApply()
{
    cancel();
}

TransformApply::Result TransformApply::run(Input input, const std::atomic<bool> *cancel, std::atomic<qint64> *rowsDone)
{
    return runGated(std::move(input), cancel, rowsDone, nullptr);
}

TransformApply::Result TransformApply::runGated(Input input, const std::atomic<bool> *cancel, std::atomic<qint64> *rowsDone,
                                                TransformApply *gate)
{
    QElapsedTimer timer;
    timer.start();
    Result r;
    r.layerIds = input.layerIds;
    r.versions = input.versions;
    r.area = input.area;
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    for (size_t i = 0; i < input.layers.size(); ++i) {
        if (gate)
            gate->waitAtGate(cancel);
        if (cancelled())
            return r;
        TiledImage out = input.layers[i]; // shares the layer's tiles; changed tiles are copied
        if (!input.session.render(int(i), out, input.area, TransformSession::Quality::Final, cancel, rowsDone)) {
            r.outOfMemory = !cancelled();
            return r;
        }
        out.pruneTransparent(input.area);
        r.layers.push_back(std::move(out));
    }
    r.ok = true;
    r.workerMs = timer.elapsed();
    return r;
}

quint64 TransformApply::start(Input input)
{
    cancel();
    const quint64 generation = ++m_generation;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_rowsDone = std::make_shared<std::atomic<qint64>>(0);
    m_rowsTotal = qint64(input.area.height()) * qint64(input.layers.size());
    m_running = true;
    m_thread = std::thread([this, generation, cancel = m_cancel, rows = m_rowsDone, input = std::move(input)]() mutable {
        Result r = runGated(std::move(input), cancel.get(), rows.get(), this);
        r.generation = generation;
        if (cancel->load())
            return; // delivers nothing
        {
            QMutexLocker lock(&m_resultMutex);
            m_result = std::move(r);
        }
        // Queued to this object's thread. If this object is destroyed first, its destructor has
        // already joined this thread, and Qt drops the call with the object.
        QMetaObject::invokeMethod(this, &TransformApply::deliverPending, Qt::QueuedConnection);
    });
    return generation;
}

void TransformApply::deliverPending()
{
    std::optional<Result> result;
    {
        QMutexLocker lock(&m_resultMutex);
        result.swap(m_result);
    }
    if (!result || result->generation != m_generation || !m_running)
        return; // already delivered, cancelled or superseded
    join();
    m_running = false;
    emit finished(*result);
}

void TransformApply::finishNow()
{
    if (!m_running)
        return;
    join();
    deliverPending();
}

void TransformApply::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
    m_permits.store(1 << 30); // a held test worker continues to its cancellation check
    join();
    m_permits.store(0);
    {
        QMutexLocker lock(&m_resultMutex);
        m_result.reset();
    }
    if (m_running) {
        m_running = false;
        ++m_generation; // a result already queued is now stale
    }
}

void TransformApply::join()
{
    if (m_thread.joinable())
        m_thread.join();
}

double TransformApply::progress() const
{
    if (!m_running || !m_rowsDone || m_rowsTotal <= 0)
        return 0.0;
    return std::min(1.0, double(m_rowsDone->load()) / double(m_rowsTotal));
}

void TransformApply::holdForTesting(bool hold)
{
    m_hold.store(hold);
}

void TransformApply::releaseOneStepForTesting()
{
    m_permits.fetch_add(1);
}

void TransformApply::waitAtGate(const std::atomic<bool> *cancel)
{
    if (!m_hold.load())
        return;
    m_waiting.fetch_add(1);
    while (!(cancel && cancel->load())) {
        int p = m_permits.load();
        if (p > 0 && m_permits.compare_exchange_weak(p, p - 1))
            break;
        QThread::msleep(1);
    }
    m_waiting.fetch_sub(1);
}

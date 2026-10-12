#pragma once

#include "TiledImage.h"
#include "TransformSession.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QRect>
#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

// Prepares a transform's Apply on a worker thread (milestone 10; the rules are
// research/m10-concurrency-design.md):
//  - the inputs are copies made on the UI thread (the session's worker copy and each layer, which
//    share their pixels), so the worker never touches the live document;
//  - each result starts as a copy of its layer and only the transformed area's tiles are
//    rewritten, so committing it means exchanging tile references (CanvasItem);
//  - cancellation is checked between row bands; a cancelled job delivers nothing, and a job whose
//    generation is no longer current is discarded when it arrives;
//  - results arrive on the thread that owns this object through a queued call, never directly.
// Destroying the object cancels a running job and waits for it.
class TransformApply : public QObject
{
    Q_OBJECT

public:
    struct Input
    {
        TransformSession session;     // TransformSession::workerCopy()
        std::vector<quint64> layerIds; // part i belongs to layerIds[i]
        std::vector<TiledImage> layers;
        std::vector<quint64> versions; // each layer's TiledImage::version() when the job started
        QRect area;                    // the document area that changes
    };
    struct Result
    {
        quint64 generation = 0;
        std::vector<quint64> layerIds;
        std::vector<TiledImage> layers; // complete new layers (empty unless ok)
        std::vector<quint64> versions;
        QRect area;
        bool ok = false;
        bool outOfMemory = false;
        qint64 workerMs = 0;
    };

    explicit TransformApply(QObject *parent = nullptr);
    ~TransformApply() override;

    // Starts a job; returns its generation. A job already running is cancelled first.
    quint64 start(Input input);
    // Runs a job on the calling thread and returns its result (small transforms, tests).
    static Result run(Input input, const std::atomic<bool> *cancel = nullptr, std::atomic<qint64> *rowsDone = nullptr);
    // Cancels the running job (it delivers nothing) and waits until its thread has stopped.
    void cancel();
    // Waits for the running job and delivers its result now (finished() is emitted before this
    // returns). For callers that need the transform applied before they continue.
    void finishNow();
    bool isRunning() const { return m_running; }
    quint64 generation() const { return m_generation; }
    // 0..1 for the running job.
    double progress() const;

    // Tests: makes the worker wait before each layer until released, so cancellation can be
    // tested at a known point.
    void holdForTesting(bool hold);
    bool waitingForTesting() const { return m_waiting.load() > 0; }
    void releaseOneStepForTesting();

signals:
    // Delivered on this object's thread for the current generation only.
    void finished(const TransformApply::Result &result);

private:
    static Result runGated(Input input, const std::atomic<bool> *cancel, std::atomic<qint64> *rowsDone,
                           TransformApply *gate);
    void deliverPending();
    void waitAtGate(const std::atomic<bool> *cancel);
    void join();

    quint64 m_generation = 0;
    bool m_running = false;
    std::thread m_thread;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    std::shared_ptr<std::atomic<qint64>> m_rowsDone;
    qint64 m_rowsTotal = 0;
    QMutex m_resultMutex;
    std::optional<Result> m_result; // set by the worker, taken on this object's thread
    // Test gate.
    std::atomic<bool> m_hold{false};
    std::atomic<int> m_waiting{0};
    std::atomic<int> m_permits{0};
};

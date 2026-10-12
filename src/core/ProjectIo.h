#pragma once

#include "DocumentData.h"
#include "ProjectFile.h"
#include "SafeSave.h"

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <functional>

class DocumentProvider;

// Runs project saves, opens and PNG exports on one worker thread, one at a time, in request
// order. Saves and exports take a DocumentData snapshot (copy-on-write images, see
// LayeredDocument::snapshot), so the UI thread can keep editing while the file is encoded and
// written (SafeSave). Results are delivered on the thread that owns this object. The destructor
// waits for a running operation, so a save is never cut off by closing the app.
class ProjectIo : public QObject
{
    Q_OBJECT

public:
    explicit ProjectIo(QObject *parent = nullptr);
    ~ProjectIo() override;

    bool isBusy() const { return m_busy; }

    // Start an operation; returns its id, or 0 if another operation is still running.
    // options.provider is filled in with provider() when empty.
    quint64 save(const DocumentData &snapshot, const QString &path, SafeSave::Options options);
    quint64 open(const QString &path);
    quint64 exportPng(const DocumentData &snapshot, const QString &path, SafeSave::Options options);

    // Storage-provider access for content:// paths (DocumentProvider::platform() by default).
    DocumentProvider *provider() const { return m_provider; }
    void setProviderForTesting(DocumentProvider *provider);
    // Called on the worker after the operation's work and before its result is delivered.
    void setBeforeFinishHookForTesting(std::function<void()> hook) { m_beforeFinish = std::move(hook); }

signals:
    void saveFinished(quint64 id, const QString &path, const SafeSave::Result &result, qint64 workerMs);
    void openFinished(quint64 id, const QString &path, const ProjectFile::LoadResult &result, qint64 workerMs);
    void exportFinished(quint64 id, const QString &path, const SafeSave::Result &result, qint64 workerMs);

private:
    template <typename Work, typename Deliver>
    quint64 run(Work work, Deliver deliver);

    QThreadPool m_pool;
    bool m_busy = false;
    quint64 m_nextId = 1;
    DocumentProvider *m_provider;
    std::function<void()> m_beforeFinish;
};

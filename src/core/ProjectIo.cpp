#include "ProjectIo.h"

#include <QElapsedTimer>

ProjectIo::ProjectIo(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

ProjectIo::~ProjectIo()
{
    m_pool.waitForDone();
}

quint64 ProjectIo::save(const DocumentData &snapshot, const QString &path)
{
    if (m_busy)
        return 0;
    m_busy = true;
    const quint64 id = m_nextId++;
    m_pool.start([this, snapshot, path, id] {
        QElapsedTimer timer;
        timer.start();
        const QString error = ProjectFile::save(snapshot, path);
        const qint64 ms = timer.elapsed();
        if (m_beforeFinish)
            m_beforeFinish();
        QMetaObject::invokeMethod(
            this,
            [this, id, path, error, ms] {
                m_busy = false;
                emit saveFinished(id, path, error, ms);
            },
            Qt::QueuedConnection);
    });
    return id;
}

quint64 ProjectIo::open(const QString &path)
{
    if (m_busy)
        return 0;
    m_busy = true;
    const quint64 id = m_nextId++;
    m_pool.start([this, path, id] {
        QElapsedTimer timer;
        timer.start();
        ProjectFile::LoadResult result = ProjectFile::load(path);
        const qint64 ms = timer.elapsed();
        if (m_beforeFinish)
            m_beforeFinish();
        QMetaObject::invokeMethod(
            this,
            [this, id, path, result, ms] {
                m_busy = false;
                emit openFinished(id, path, result, ms);
            },
            Qt::QueuedConnection);
    });
    return id;
}

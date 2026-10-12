#include "ProjectIo.h"

#include "DocumentProvider.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImageWriter>

ProjectIo::ProjectIo(QObject *parent)
    : QObject(parent)
    , m_provider(DocumentProvider::platform())
{
    m_pool.setMaxThreadCount(1);
}

ProjectIo::~ProjectIo()
{
    m_pool.waitForDone();
}

void ProjectIo::setProviderForTesting(DocumentProvider *provider)
{
    m_provider = provider ? provider : DocumentProvider::platform();
}

// work() runs on the worker and returns the result; deliver(id, result, ms) runs on this object's
// thread afterwards.
template <typename Work, typename Deliver>
quint64 ProjectIo::run(Work work, Deliver deliver)
{
    if (m_busy)
        return 0;
    m_busy = true;
    const quint64 id = m_nextId++;
    m_pool.start([this, id, work = std::move(work), deliver = std::move(deliver)] {
        QElapsedTimer timer;
        timer.start();
        auto result = work();
        const qint64 ms = timer.elapsed();
        if (m_beforeFinish)
            m_beforeFinish();
        QMetaObject::invokeMethod(
            this,
            [this, id, result = std::move(result), ms, deliver] {
                m_busy = false;
                deliver(id, result, ms);
            },
            Qt::QueuedConnection);
    });
    return id;
}

quint64 ProjectIo::save(const DocumentData &snapshot, const QString &path, SafeSave::Options options)
{
    if (!options.provider)
        options.provider = m_provider;
    return run([snapshot, path, options] { return SafeSave::saveProject(snapshot, path, options); },
               [this, path](quint64 id, const SafeSave::Result &result, qint64 ms) {
                   emit saveFinished(id, path, result, ms);
               });
}

quint64 ProjectIo::open(const QString &path)
{
    return run([path] { return ProjectFile::load(path); },
               [this, path](quint64 id, const ProjectFile::LoadResult &result, qint64 ms) {
                   emit openFinished(id, path, result, ms);
               });
}

quint64 ProjectIo::exportPng(const DocumentData &snapshot, const QString &path, SafeSave::Options options)
{
    if (!options.provider)
        options.provider = m_provider;
    return run(
        [snapshot, path, options] {
            // A fresh composite of the visible layers (not the view cache), straight alpha.
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            QImageWriter writer(&buffer, "png");
            if (!writer.write(snapshot.composite().convertToFormat(QImage::Format_ARGB32))) {
                SafeSave::Result failed;
                failed.error = QStringLiteral("Could not encode the PNG image: %1").arg(writer.errorString());
                return failed;
            }
            return SafeSave::writeExport(bytes, path, options);
        },
        [this, path](quint64 id, const SafeSave::Result &result, qint64 ms) { emit exportFinished(id, path, result, ms); });
}

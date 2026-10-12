#include "RecoveryManager.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

namespace {

const QString RecoverySuffix = QStringLiteral(".") + QString::fromLatin1(RecoveryFile::Extension);

QString lockPathFor(const QString &recoveryPath)
{
    return recoveryPath.left(recoveryPath.size() - RecoverySuffix.size()) + QStringLiteral(".lock");
}

// True if no live process owns the lock. A lock whose owner process has died is stale; an
// unreadable lock file (cut short by a crash) older than a minute is treated as abandoned.
bool ownerIsGone(const QString &lockPath)
{
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0); // only a dead owner makes a readable lock stale, never its age
    if (lock.tryLock(0)) {
        lock.unlock();
        return true;
    }
    qint64 pid = 0;
    QString host, app;
    if (!lock.getLockInfo(&pid, &host, &app)
        && QFileInfo(lockPath).lastModified().secsTo(QDateTime::currentDateTime()) > 60) {
        return lock.removeStaleLockFile();
    }
    return false;
}

// QSaveFile's temporary files are named "<target>.<random>". Set-aside ".damaged" files are kept.
void removeTemporaryLeftovers(QDir &dir, const QString &recoveryFileName)
{
    const QStringList leftovers = dir.entryList({recoveryFileName + QStringLiteral(".*")}, QDir::Files);
    for (const QString &leftover : leftovers) {
        if (!leftover.contains(QStringLiteral(".damaged")))
            dir.remove(leftover);
    }
}

} // namespace

RecoveryManager::RecoveryManager(const QString &directory, QObject *parent)
    : QObject(parent)
    , m_directory(directory)
    , m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_pool.setMaxThreadCount(1);
}

RecoveryManager::~RecoveryManager()
{
    m_pool.waitForDone();
}

QString RecoveryManager::sessionBase() const
{
    return QDir(m_directory).filePath(QStringLiteral("session-") + m_sessionId);
}

QString RecoveryManager::recoveryFilePath() const
{
    return sessionBase() + RecoverySuffix;
}

QString RecoveryManager::start()
{
    if (m_lock)
        return {};
    if (!QDir().mkpath(m_directory))
        return QStringLiteral("Could not create the recovery folder \"%1\".").arg(m_directory);
    auto lock = std::make_unique<QLockFile>(sessionBase() + QStringLiteral(".lock"));
    lock->setStaleLockTime(0); // only a dead owner process makes a lock stale, never its age
    if (!lock->tryLock(0))
        return QStringLiteral("Could not create a recovery lock in \"%1\".").arg(m_directory);
    m_lock = std::move(lock);
    return {};
}

bool RecoveryManager::hasRecoveryFile() const
{
    QMutexLocker locker(&m_commitMutex);
    return m_fileExists;
}

bool RecoveryManager::requestWrite(const DocumentData &snapshot, const RecoveryFile::Metadata &metadata)
{
    if (!m_lock || m_writing)
        return false;
    quint64 generation;
    {
        QMutexLocker locker(&m_commitMutex);
        generation = m_generation;
    }
    const quint64 sequence = m_nextSequence++;
    m_writing = true;
    m_pool.start([this, snapshot, metadata, generation, sequence] {
        QElapsedTimer timer;
        timer.start();
        const WriteOutcome outcome = writeAndCommit(snapshot, metadata, generation, sequence);
        const qint64 ms = timer.elapsed();
        QMetaObject::invokeMethod(
            this,
            [this, outcome, metadata, ms] {
                m_writing = false;
                emit writeFinished(outcome.committed, outcome.stale, outcome.error, metadata.documentId,
                                   metadata.revision, ms);
            },
            Qt::QueuedConnection);
    });
    return true;
}

QString RecoveryManager::writeNow(const DocumentData &snapshot, const RecoveryFile::Metadata &metadata)
{
    if (!m_lock)
        return QStringLiteral("Crash recovery is not running.");
    m_pool.waitForDone();
    quint64 generation;
    {
        QMutexLocker locker(&m_commitMutex);
        generation = m_generation;
    }
    return writeAndCommit(snapshot, metadata, generation, m_nextSequence++).error;
}

RecoveryManager::WriteToken RecoveryManager::reserveWrite()
{
    WriteToken token;
    if (!m_lock)
        return token;
    QMutexLocker locker(&m_commitMutex);
    token.generation = m_generation;
    token.sequence = m_nextSequence++;
    token.valid = true;
    return token;
}

QString RecoveryManager::writeProject(const WriteToken &token, const QByteArray &project,
                                      const RecoveryFile::Metadata &metadata)
{
    if (!token.valid)
        return QStringLiteral("crash recovery is not running");
    const WriteOutcome outcome =
        commitBytes(RecoveryFile::encodeProject(project, metadata), token.generation, token.sequence, true);
    if (!outcome.error.isEmpty())
        return outcome.error;
    if (outcome.discarded)
        return QStringLiteral("the recovery data was discarded while saving");
    return {}; // committed, or a newer write of this document already replaced it
}

RecoveryManager::WriteOutcome RecoveryManager::writeAndCommit(const DocumentData &snapshot,
                                                              const RecoveryFile::Metadata &metadata,
                                                              quint64 generation, quint64 sequence)
{
    return commitBytes(RecoveryFile::encode(snapshot, metadata), generation, sequence, false);
}

RecoveryManager::WriteOutcome RecoveryManager::commitBytes(const QByteArray &bytes, quint64 generation,
                                                           quint64 sequence, bool readBack)
{
    WriteOutcome outcome;
    // The temporary file lives next to the recovery file; the previous recovery file stays
    // valid until commit() atomically replaces it.
    QSaveFile file(recoveryFilePath());
    bool written = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    if (!written)
        outcome.error = QStringLiteral("Could not write the recovery file: %1").arg(file.errorString());

    if (m_beforeCommitHook)
        m_beforeCommitHook();

    QMutexLocker locker(&m_commitMutex);
    if (!written) {
        file.cancelWriting();
    } else if (generation != m_generation || sequence <= m_lastCommittedSequence) {
        file.cancelWriting();
        outcome.stale = true;
        outcome.discarded = generation != m_generation;
    } else if (!file.commit()) {
        outcome.error = QStringLiteral("Could not replace the recovery file: %1").arg(file.errorString());
    } else {
        outcome.committed = true;
        m_lastCommittedSequence = sequence;
        m_fileExists = true;
        if (readBack) {
            // Still under the lock, so no other write can replace the file in between.
            QFile check(recoveryFilePath());
            if (!check.open(QIODevice::ReadOnly) || check.readAll() != bytes)
                outcome.error = QStringLiteral("the recovery file could not be read back correctly");
        }
    }
    return outcome;
}

void RecoveryManager::discard()
{
    QMutexLocker locker(&m_commitMutex);
    ++m_generation;
    QFile::remove(recoveryFilePath());
    m_fileExists = false;
}

void RecoveryManager::finishCleanly()
{
    m_pool.waitForDone();
    discard();
    m_lock.reset(); // QLockFile's destructor removes the lock file
}

void RecoveryManager::releaseKeepingFile()
{
    m_pool.waitForDone();
    m_lock.reset();
}

QList<RecoveryManager::Orphan> RecoveryManager::findOrphans()
{
    QList<Orphan> orphans;
    QDir dir(m_directory);
    const QStringList files = dir.entryList({QStringLiteral("*") + RecoverySuffix}, QDir::Files);
    for (const QString &name : files) {
        const QString path = dir.filePath(name);
        if (QFileInfo(path) == QFileInfo(recoveryFilePath()))
            continue;
        if (!ownerIsGone(lockPathFor(path)))
            continue; // the owning session is still running
        removeTemporaryLeftovers(dir, name);
        Orphan orphan{path, RecoveryFile::load(path)};
        orphans.append(orphan);
    }

    // Sessions that crashed before their first recovery write leave only a lock file and maybe
    // an unfinished temporary file.
    const QStringList locks = dir.entryList({QStringLiteral("session-*.lock")}, QDir::Files);
    for (const QString &name : locks) {
        const QString base = name.left(name.size() - 5);
        if (dir.exists(base + RecoverySuffix) || dir.filePath(name) == sessionBase() + QStringLiteral(".lock"))
            continue;
        if (!ownerIsGone(dir.filePath(name)))
            continue;
        removeTemporaryLeftovers(dir, base + RecoverySuffix);
    }
    std::sort(orphans.begin(), orphans.end(), [](const Orphan &a, const Orphan &b) {
        const QDateTime ta = a.contents.ok() ? a.contents.metadata.savedAt : QFileInfo(a.path).lastModified();
        const QDateTime tb = b.contents.ok() ? b.contents.metadata.savedAt : QFileInfo(b.path).lastModified();
        return ta > tb;
    });
    return orphans;
}

QString RecoveryManager::adopt(const Orphan &orphan)
{
    if (!m_lock)
        return QStringLiteral("Crash recovery is not running.");
    m_pool.waitForDone();
    QMutexLocker locker(&m_commitMutex);
    ++m_generation;
    QFile::remove(recoveryFilePath());
    m_fileExists = false;
    if (!QFile::rename(orphan.path, recoveryFilePath()))
        return QStringLiteral("Could not take over the recovery file \"%1\".").arg(orphan.path);
    m_fileExists = true;
    return {};
}

void RecoveryManager::removeOrphan(const Orphan &orphan)
{
    QFile::remove(orphan.path);
    QFile::remove(lockPathFor(orphan.path));
}

QString RecoveryManager::setAside(const Orphan &orphan)
{
    QString target = orphan.path + QStringLiteral(".damaged");
    for (int i = 2; QFile::exists(target); ++i)
        target = orphan.path + QStringLiteral(".damaged-%1").arg(i);
    if (!QFile::rename(orphan.path, target)) {
        QFile::remove(orphan.path);
        return {};
    }
    return target;
}

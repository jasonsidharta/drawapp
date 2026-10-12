#pragma once

#include "RecoveryFile.h"

#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <functional>
#include <memory>

class QLockFile;

// Owns this session's recovery file in a recovery folder (the app's data folder by default).
// Each running app instance holds a lock file; recovery files whose lock is not held by a live
// process belong to sessions that ended without cleaning up (crashes) and are offered on start.
//
// Writes run on one worker thread, one at a time. The worker only touches the private image copy
// it was given. A write commits (atomic replace of the recovery file) only if no discard happened
// since it was requested, so an outdated write can neither replace newer data nor recreate a file
// that was cleaned up. All public methods are for the UI thread.
class RecoveryManager : public QObject
{
    Q_OBJECT

public:
    struct Orphan
    {
        QString path;                    // the orphaned recovery file
        RecoveryFile::Contents contents; // decoded; contents.error is set if unusable
    };

    explicit RecoveryManager(const QString &directory, QObject *parent = nullptr);
    // Waits for an in-flight write. Leaves files in place (like a crash) unless
    // finishCleanly() or releaseKeepingFile() was called.
    ~RecoveryManager() override;

    // Creates the folder and takes this session's lock. Returns an error message on failure.
    QString start();
    bool isStarted() const { return m_lock != nullptr; }
    QString directory() const { return m_directory; }
    QString recoveryFilePath() const;

    bool isWriting() const { return m_writing; }
    bool hasRecoveryFile() const;

    // Starts an asynchronous write. snapshot must be a private copy (not shared with the canvas).
    // Returns false if a write is already running or the manager is not started.
    bool requestWrite(const QImage &snapshot, const RecoveryFile::Metadata &metadata);
    // Writes synchronously in this thread after any in-flight write ends (used at shutdown).
    QString writeNow(const QImage &snapshot, const RecoveryFile::Metadata &metadata);
    // Invalidates in-flight writes and deletes this session's recovery file.
    void discard();
    // Normal exit with nothing to recover: discard, then remove the lock.
    void finishCleanly();
    // Exit that keeps unsaved work recoverable: remove the lock, keep the recovery file.
    void releaseKeepingFile();

    // Recovery files of ended sessions, newest first. Damaged ones have contents.error set.
    QList<Orphan> findOrphans();
    // Takes over an orphan as this session's recovery file. Returns an error message on failure.
    QString adopt(const Orphan &orphan);
    void removeOrphan(const Orphan &orphan);
    // Renames a damaged recovery file so it is not offered again. Returns the new path.
    QString setAside(const Orphan &orphan);

    // Called on the worker after writing the temporary file and before the commit decision.
    void setBeforeCommitHookForTesting(std::function<void()> hook) { m_beforeCommitHook = std::move(hook); }

signals:
    // committed: the recovery file now holds this snapshot. stale: skipped because a discard
    // happened after the request. error: the write failed (the previous recovery file is intact).
    void writeFinished(bool committed, bool stale, const QString &error, quint64 documentId,
                       quint64 revision, qint64 workerMs);

private:
    struct WriteOutcome
    {
        bool committed = false;
        bool stale = false;
        QString error;
    };
    WriteOutcome writeAndCommit(const QImage &snapshot, const RecoveryFile::Metadata &metadata,
                                quint64 generation, quint64 sequence);
    QString sessionBase() const;

    QString m_directory;
    QString m_sessionId;
    std::unique_ptr<QLockFile> m_lock;
    QThreadPool m_pool;
    bool m_writing = false;
    quint64 m_nextSequence = 1;
    std::function<void()> m_beforeCommitHook;

    mutable QMutex m_commitMutex; // guards the fields below and every change of the recovery file
    quint64 m_generation = 0;
    quint64 m_lastCommittedSequence = 0;
    bool m_fileExists = false;
};

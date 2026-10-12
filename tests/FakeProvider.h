#pragma once

// An in-memory stand-in for an Android storage provider (content:// documents) with injectable
// failures, so the save path for documents that cannot be replaced atomically can be tested on
// the desktop. Called from the file worker thread; the test thread configures it.

#include "DocumentProvider.h"

#include <QHash>
#include <QMutex>
#include <QSet>
#include <functional>

class FakeProvider : public DocumentProvider
{
public:
    QHash<QString, QByteArray> documents; // uri -> contents ("" = just created by a picker)
    QSet<QString> revoked;                // access withdrawn (Android: SecurityException)
    qint64 freeBytes = -1;                // reported free space, -1 = unknown
    bool failReservation = false;         // running out of space found before writing (untouched)
    qint64 failAfterBytes = -1;           // writing stops after this many bytes (partly written)
    bool corruptReadBack = false;
    int writes = 0;
    std::function<void(const QString &uri)> beforeWrite; // runs on the worker before anything changes

    Info probe(const QString &uri) override
    {
        QMutexLocker locker(&m_mutex);
        Info info;
        if (revoked.contains(uri)) {
            info.error = QStringLiteral("DrawApp no longer has permission to use this file (simulated).");
            return info;
        }
        if (!documents.contains(uri)) {
            info.error = QStringLiteral("The file is no longer available (simulated).");
            return info;
        }
        info.size = documents.value(uri).size();
        info.freeBytes = freeBytes;
        info.regularFile = true;
        info.flags = FlagSupportsWrite | FlagSupportsDelete | FlagSupportsRename;
        return info;
    }

    QString write(const QString &uri, const QByteArray &bytes, bool *touched) override
    {
        *touched = false;
        if (beforeWrite)
            beforeWrite(uri);
        QMutexLocker locker(&m_mutex);
        ++writes;
        if (revoked.contains(uri))
            return QStringLiteral("Permission denied (simulated).");
        if (failReservation)
            return QStringLiteral("There is not enough free space on the storage that holds this file (simulated).");
        QByteArray &doc = documents[uri];
        *touched = true;
        if (failAfterBytes >= 0) {
            const QByteArray old = doc;
            doc = bytes.left(failAfterBytes) + old.mid(failAfterBytes);
            return QStringLiteral("Writing stopped (simulated I/O error).");
        }
        doc = bytes;
        return {};
    }

    QByteArray read(const QString &uri, qint64 maxBytes, QString *error) override
    {
        QMutexLocker locker(&m_mutex);
        if (revoked.contains(uri) || !documents.contains(uri)) {
            *error = QStringLiteral("not readable (simulated)");
            return {};
        }
        QByteArray data = documents.value(uri).left(maxBytes + 1);
        if (corruptReadBack && !data.isEmpty())
            data[data.size() / 2] = char(data[data.size() / 2] ^ 0x01);
        return data;
    }

    QByteArray contents(const QString &uri)
    {
        QMutexLocker locker(&m_mutex);
        return documents.value(uri);
    }

private:
    QMutex m_mutex;
};

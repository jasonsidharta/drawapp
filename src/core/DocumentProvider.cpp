#include "DocumentProvider.h"

#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

QString DocumentProvider::describe(const Info &info)
{
    if (!info.ok())
        return QStringLiteral("unreachable (%1)").arg(info.error);
    QStringList parts;
    parts << (info.size < 0 ? QStringLiteral("size unknown") : QStringLiteral("%1 bytes").arg(info.size));
    parts << (info.freeBytes < 0 ? QStringLiteral("free space unknown")
                                 : QStringLiteral("%1 MiB free").arg(info.freeBytes / (1024.0 * 1024.0), 0, 'f', 1));
    parts << (info.regularFile ? QStringLiteral("seekable file") : QStringLiteral("stream only"));
    if (info.flags >= 0) {
        parts << QStringLiteral("flags 0x%1 (write %2, rename %3, delete %4)")
                     .arg(info.flags, 0, 16)
                     .arg(info.flags & FlagSupportsWrite ? "yes" : "no", info.flags & FlagSupportsRename ? "yes" : "no",
                          info.flags & FlagSupportsDelete ? "yes" : "no");
    } else {
        parts << QStringLiteral("flags unknown");
    }
    return parts.join(QStringLiteral(", "));
}

#ifndef Q_OS_ANDROID

namespace {

// Desktop stand-in: there are no storage providers, but the in-place write semantics are the
// same as on Android (no atomic replacement), which keeps the code path testable.
class FileDocumentProvider : public DocumentProvider
{
public:
    Info probe(const QString &uri) override
    {
        Info info;
        const QFileInfo fi(uri);
        info.size = fi.exists() ? fi.size() : 0;
        info.regularFile = true;
        const QStorageInfo storage(fi.absolutePath());
        if (storage.isValid())
            info.freeBytes = storage.bytesAvailable();
        return info;
    }

    QString write(const QString &uri, const QByteArray &bytes, bool *touched) override
    {
        *touched = false;
        QFile file(uri);
        if (!file.open(QIODevice::ReadWrite))
            return QStringLiteral("It could not be opened for writing (%1).").arg(file.errorString());
        *touched = true;
        if (file.write(bytes) != bytes.size() || !file.resize(bytes.size()) || !file.flush())
            return QStringLiteral("Writing stopped (%1).").arg(file.errorString());
        return {};
    }

    QByteArray read(const QString &uri, qint64 maxBytes, QString *error) override
    {
        QFile file(uri);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = file.errorString();
            return {};
        }
        return file.read(maxBytes + 1);
    }
};

} // namespace

DocumentProvider *DocumentProvider::platform()
{
    static FileDocumentProvider provider;
    return &provider;
}

#endif

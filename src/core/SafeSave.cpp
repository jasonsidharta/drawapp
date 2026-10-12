#include "SafeSave.h"

#include "DocumentProvider.h"
#include "ProjectFile.h"

#include <QFileInfo>
#include <QSaveFile>
#include <QStorageInfo>
#include <algorithm>

namespace {

// Headroom kept free on the destination storage.
constexpr qint64 SpaceMargin = 1024 * 1024;

QString megabytes(qint64 bytes)
{
    return QStringLiteral("%1 MB").arg(bytes / 1e6, 0, 'f', 1);
}

SafeSave::Result failed(const QString &error, bool touched = false)
{
    SafeSave::Result r;
    r.status = SafeSave::Status::Failed;
    r.error = error;
    r.destinationTouched = touched;
    return r;
}

SafeSave::Result writeLocal(const QByteArray &bytes, const QString &path, const QString &name)
{
    const QStorageInfo storage(QFileInfo(path).absolutePath());
    // QSaveFile needs room for the whole new file next to the old one.
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < bytes.size() + SpaceMargin)
        return failed(QStringLiteral("There is not enough free space to save \"%1\" (it needs %2, %3 are free). "
                                     "The file was not changed.")
                          .arg(name, megabytes(bytes.size() + SpaceMargin), megabytes(storage.bytesAvailable())));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return failed(QStringLiteral("Could not open \"%1\" for writing: %2. The file was not changed.")
                          .arg(name, file.errorString()));
    if (file.write(bytes) != bytes.size()) {
        const QString reason = file.error() == QFileDevice::ResourceError ? QStringLiteral("the storage is full")
                                                                          : file.errorString();
        file.cancelWriting();
        return failed(QStringLiteral("Could not write \"%1\": %2. Any previous version of the file was left unchanged.")
                          .arg(name, reason));
    }
    if (!file.commit())
        return failed(QStringLiteral("Could not save \"%1\": %2. Any previous version of the file was left unchanged.")
                          .arg(name, file.errorString()));
    SafeSave::Result r;
    r.status = SafeSave::Status::Saved;
    return r;
}

SafeSave::Result writeProvider(const QByteArray &bytes, const QString &path, DocumentProvider *provider,
                               const DocumentProvider::Info &info, bool projectCopyKept, const QString &name)
{
    const QString keptCopy = projectCopyKept
        ? QStringLiteral(" Your drawing is still open and a copy is kept on this device; use Save As to save it "
                         "somewhere else.")
        : QString();
    if (info.freeBytes >= 0 && info.freeBytes + std::max<qint64>(info.size, 0) < bytes.size() + SpaceMargin)
        return failed(QStringLiteral("There is not enough free space on the storage that holds \"%1\" (it needs %2, "
                                     "%3 are free). The file was not changed.")
                          .arg(name, megabytes(bytes.size() + SpaceMargin),
                               megabytes(info.freeBytes + std::max<qint64>(info.size, 0))));
    bool touched = false;
    const QString error = provider->write(path, bytes, &touched);
    if (!error.isEmpty()) {
        return failed(QStringLiteral("Could not save \"%1\": %2 %3")
                          .arg(name, error,
                               touched ? QStringLiteral("The file there may be damaged.") + keptCopy
                                       : QStringLiteral("The file was not changed.")),
                      touched);
    }
    QString readError;
    const QByteArray back = provider->read(path, bytes.size(), &readError);
    if (!readError.isEmpty() || back != bytes)
        return failed(QStringLiteral("\"%1\" was written, but reading it back gave different data%2. The file there "
                                     "may be damaged.%3")
                          .arg(name, readError.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(readError), keptCopy),
                      true);
    SafeSave::Result r;
    r.status = SafeSave::Status::Saved;
    return r;
}

QString nameFor(const QString &path, const SafeSave::Options &options)
{
    return options.name.isEmpty() ? SafeSave::displayName(path) : options.name;
}

} // namespace

bool SafeSave::isProviderPath(const QString &path)
{
    return path.startsWith(QLatin1String("content:"), Qt::CaseInsensitive);
}

QString SafeSave::displayName(const QString &path)
{
    // For a content URI on Android, QFileInfo asks the provider for the display name.
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

SafeSave::Result SafeSave::saveProject(const DocumentData &document, const QString &path, const Options &options)
{
    if (!isProviderPath(path)) {
        const QByteArray bytes = ProjectFile::encode(document);
        const QString invalid = ProjectFile::verify(bytes);
        if (!invalid.isEmpty())
            return failed(QStringLiteral("The project could not be prepared for saving (%1). Nothing was written.").arg(invalid));
        return writeLocal(bytes, path, nameFor(path, options));
    }

    if (!options.provider)
        return failed(QStringLiteral("This location cannot be written."));
    const DocumentProvider::Info info = options.provider->probe(path);
    if (!info.ok()) {
        Result r = failed(QStringLiteral("Could not save \"%1\": %2 The file was not changed.").arg(nameFor(path, options), info.error));
        r.viaProvider = true;
        return r;
    }
    // An empty document is one the picker has just created; anything else would be overwritten.
    if (!options.allowOverwrite && info.size != 0) {
        Result r;
        r.status = Status::NeedsOverwriteConfirmation;
        r.viaProvider = true;
        r.probe = DocumentProvider::describe(info);
        return r;
    }
    const QByteArray bytes = ProjectFile::encode(document);
    const QString invalid = ProjectFile::verify(bytes);
    if (!invalid.isEmpty())
        return failed(QStringLiteral("The project could not be prepared for saving (%1). Nothing was written.").arg(invalid));
    if (!options.localCopy.keep)
        return failed(QStringLiteral("No local copy of the drawing could be kept, so the file was not written."));
    const QString copyError = options.localCopy.keep(bytes);
    if (!copyError.isEmpty())
        return failed(QStringLiteral("Could not keep a copy of the drawing on this device before saving (%1). "
                                     "The file was not changed.").arg(copyError));
    Result r = writeProvider(bytes, path, options.provider, info, true, nameFor(path, options));
    r.viaProvider = true;
    r.probe = DocumentProvider::describe(info);
    if (r.ok() && options.localCopy.release)
        options.localCopy.release();
    return r;
}

SafeSave::Result SafeSave::writeExport(const QByteArray &bytes, const QString &path, const Options &options)
{
    if (!isProviderPath(path))
        return writeLocal(bytes, path, nameFor(path, options));
    if (!options.provider)
        return failed(QStringLiteral("This location cannot be written."));
    const DocumentProvider::Info info = options.provider->probe(path);
    if (!info.ok())
        return failed(QStringLiteral("Could not export \"%1\": %2 The file was not changed.").arg(nameFor(path, options), info.error));
    if (!options.allowOverwrite && info.size != 0) {
        Result r;
        r.status = Status::NeedsOverwriteConfirmation;
        r.viaProvider = true;
        return r;
    }
    Result r = writeProvider(bytes, path, options.provider, info, false, nameFor(path, options));
    r.viaProvider = true;
    r.probe = DocumentProvider::describe(info);
    return r;
}

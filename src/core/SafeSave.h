#pragma once

#include "DocumentData.h"

#include <QByteArray>
#include <QString>
#include <functional>

class DocumentProvider;

// Writing projects and exports to their destination. Runs on the file worker thread.
//
// Local files (desktop paths, app-private paths): the bytes are checked, free space is checked,
// and QSaveFile writes a temporary file next to the destination and renames it over the old file.
// Until that rename, the old file is untouched; an interrupted or failed write leaves it intact.
//
// Android storage-provider documents (content:// URIs) cannot be replaced atomically: the
// storage access framework gives access to that one document, with no way to create a sibling
// and rename it over. The steps are therefore:
//   1. probe the document (access, current size, free space, whether it is a seekable file);
//   2. if it already has contents and the overwrite was not confirmed, stop and ask;
//   3. encode and verify the project in memory, then keep a local copy in private storage
//      (normally the crash-recovery file) and check it by reading it back;
//   4. refuse if the free space reported for the destination is too small;
//   5. write: on a seekable file the full size is reserved first (posix_fallocate), so running
//      out of space is found before the old contents change; then overwrite, cut, fsync;
//   6. read the document back and compare; only then is the local copy released.
// A failure in steps 1–4 leaves the destination untouched. A failure in 5–6 can leave it damaged,
// but the drawing stays open and unsaved and the local copy stays on the device.
namespace SafeSave {

struct LocalCopy
{
    // Writes the encoded project to private local storage and checks it; returns an error message.
    std::function<QString(const QByteArray &project)> keep;
    // Called once the destination was written and verified.
    std::function<void()> release;
};

struct Options
{
    bool allowOverwrite = false;           // the user confirmed overwriting an existing document
    LocalCopy localCopy;                   // required for project saves to a provider document
    DocumentProvider *provider = nullptr;  // for content:// paths
    // Name for messages. A provider cannot name a document it no longer has, so the caller passes
    // the name it knows; empty = ask the provider.
    QString name;
};

enum class Status { Saved, NeedsOverwriteConfirmation, Failed };

struct Result
{
    Status status = Status::Failed;
    QString error;                   // user-facing, for Failed
    bool destinationTouched = false; // the destination may no longer hold its previous contents
    bool viaProvider = false;
    QString probe;                   // what the provider reported (for logs and messages)
    bool ok() const { return status == Status::Saved; }
};

bool isProviderPath(const QString &path);
QString displayName(const QString &path);

Result saveProject(const DocumentData &document, const QString &path, const Options &options);
// Writes already encoded bytes (PNG export). No local copy: the drawing itself is not at risk.
Result writeExport(const QByteArray &bytes, const QString &path, const Options &options);

} // namespace SafeSave

#pragma once

#include <QByteArray>
#include <QString>

// Access to a destination that cannot be replaced atomically: a document of an Android storage
// provider (content:// URI, storage access framework). Local files never go through this; they are
// replaced atomically with QSaveFile (see SafeSave).
//
// All functions block and run on the file worker thread.
class DocumentProvider
{
public:
    struct Info
    {
        QString error;          // set if the document cannot be reached (access revoked, deleted)
        qint64 size = -1;       // current size in bytes, -1 if unknown
        qint64 freeBytes = -1;  // free space where the document is stored, -1 if unknown
        bool regularFile = false; // the provider hands out a seekable file (space can be reserved)
        int flags = -1;          // DocumentsContract.Document.COLUMN_FLAGS, -1 if unknown
        bool ok() const { return error.isEmpty(); }
    };

    // From DocumentsContract.Document.
    static constexpr int FlagSupportsWrite = 0x2;
    static constexpr int FlagSupportsDelete = 0x4;
    static constexpr int FlagSupportsRename = 0x40;

    virtual ~DocumentProvider() = default;

    virtual Info probe(const QString &uri) = 0;
    // Replaces the document's contents with bytes, reserving the space first where the storage
    // allows it. On failure the document may be partly written; the message says whether it was
    // touched at all.
    virtual QString write(const QString &uri, const QByteArray &bytes, bool *touched) = 0;
    // Reads at most maxBytes + 1 bytes.
    virtual QByteArray read(const QString &uri, qint64 maxBytes, QString *error) = 0;
    // A short description of what probe() found, for logs and the overwrite warning.
    static QString describe(const Info &info);

    // The provider for this platform (Android: ContentResolver; elsewhere: QFile).
    static DocumentProvider *platform();
};

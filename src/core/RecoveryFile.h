#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>

// Crash-recovery file, version 1. Never written to the user's project path.
//
//   offset size  field (integers little-endian)
//   0      8     magic "DRAWRCV\n"
//   8      4     recovery format version (1)
//   12     4     metadata length m
//   16     32    SHA-256 of the metadata
//   48     m     metadata: UTF-8 JSON object (see Metadata)
//   48+m   8     embedded project length p
//   56+m   p     a complete .drawapp project file (ProjectFile::encode); the file ends here
namespace RecoveryFile {

inline constexpr char Extension[] = "drawrecovery";
inline constexpr quint32 CurrentVersion = 1;
inline constexpr int MaxMetadataBytes = 64 * 1024;

struct Metadata
{
    quint64 documentId = 0;   // identity of the open document within its session
    quint64 revision = 0;     // pixel revision captured
    QDateTime savedAt;        // UTC time the snapshot was taken
    QString originalPath;     // project file the artwork belongs to, empty if untitled
    QString originalUrl;
};

struct Contents
{
    QImage image;
    Metadata metadata;
    QString error;            // user-facing reason when the file cannot be used
    bool ok() const { return error.isEmpty(); }
};

QByteArray encode(const QImage &image, const Metadata &metadata);
Contents decode(const QByteArray &data);
Contents load(const QString &path);

} // namespace RecoveryFile

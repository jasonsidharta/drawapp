#pragma once

#include "DocumentData.h"

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>

// Crash-recovery file. Never written to the user's project path. Version 2 embeds a layered (v2)
// project; version 1 (milestone 2) embedded a single-layer (v1) project and is still read.
//
//   offset size  field (integers little-endian)
//   0      8     magic "DRAWRCV\n"
//   8      4     recovery format version (2)
//   12     4     metadata length m
//   16     32    SHA-256 of the metadata
//   48     m     metadata: UTF-8 JSON object (see Metadata)
//   48+m   8     embedded project length p
//   56+m   p     a complete .drawapp project file (ProjectFile::encode); the file ends here
namespace RecoveryFile {

inline constexpr char Extension[] = "drawrecovery";
inline constexpr quint32 CurrentVersion = 2;
inline constexpr int MaxMetadataBytes = 64 * 1024;

struct Metadata
{
    quint64 documentId = 0;   // identity of the open document within its session
    quint64 revision = 0;     // pixel revision captured
    QDateTime savedAt;        // UTC time the snapshot was taken
    QString originalPath;     // project file the artwork belongs to, empty if untitled
    QString originalUrl;
    QString displayName;      // name shown to the user (a content URI may no longer resolve to one)
};

struct Contents
{
    DocumentData document;
    Metadata metadata;
    quint32 recoveryVersion = 0; // 1 = single-layer recovery from milestone 2
    int projectVersion = 0;
    QString error;            // user-facing reason when the file cannot be used
    bool ok() const { return error.isEmpty(); }
};

QByteArray encode(const DocumentData &document, const Metadata &metadata);
// The same container around an already encoded version 2 project (ProjectFile::encode output).
QByteArray encodeProject(const QByteArray &project, const Metadata &metadata);
// Writes the version 1 container with a single-layer project (kept to test reading older files).
QByteArray encodeVersion1(const QImage &image, const Metadata &metadata);
Contents decode(const QByteArray &data);
Contents load(const QString &path);

} // namespace RecoveryFile

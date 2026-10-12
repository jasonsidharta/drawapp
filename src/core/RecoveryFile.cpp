#include "RecoveryFile.h"

#include "ProjectFile.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <cstring>

namespace {

constexpr char Magic[8] = {'D', 'R', 'A', 'W', 'R', 'C', 'V', '\n'};
constexpr int FixedHeader = 48;

RecoveryFile::Contents failure(const QString &message)
{
    RecoveryFile::Contents c;
    c.error = message;
    return c;
}

QString damaged(const QString &detail)
{
    return QStringLiteral("The recovery file is damaged (%1).").arg(detail);
}

} // namespace

namespace {

QByteArray container(quint32 version, const RecoveryFile::Metadata &metadata, const QByteArray &project)
{
    QJsonObject json;
    json[QStringLiteral("documentId")] = QString::number(metadata.documentId);
    json[QStringLiteral("revision")] = QString::number(metadata.revision);
    json[QStringLiteral("savedAt")] = metadata.savedAt.toUTC().toString(Qt::ISODateWithMs);
    json[QStringLiteral("originalPath")] = metadata.originalPath;
    json[QStringLiteral("originalUrl")] = metadata.originalUrl;
    if (!metadata.displayName.isEmpty())
        json[QStringLiteral("displayName")] = metadata.displayName;
    const QByteArray meta = QJsonDocument(json).toJson(QJsonDocument::Compact);

    QByteArray out;
    out.reserve(FixedHeader + meta.size() + 8 + project.size());
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData(Magic, sizeof(Magic));
    s << version << quint32(meta.size());
    const QByteArray digest = QCryptographicHash::hash(meta, QCryptographicHash::Sha256);
    s.writeRawData(digest.constData(), int(digest.size()));
    s.writeRawData(meta.constData(), int(meta.size()));
    s << quint64(project.size());
    out.append(project);
    return out;
}

} // namespace

QByteArray RecoveryFile::encode(const DocumentData &document, const Metadata &metadata)
{
    return container(CurrentVersion, metadata, ProjectFile::encode(document));
}

QByteArray RecoveryFile::encodeProject(const QByteArray &project, const Metadata &metadata)
{
    return container(CurrentVersion, metadata, project);
}

QByteArray RecoveryFile::encodeVersion1(const QImage &image, const Metadata &metadata)
{
    return container(1, metadata, ProjectFile::encodeVersion1(image));
}

RecoveryFile::Contents RecoveryFile::decode(const QByteArray &data)
{
    if (data.size() < 8 || std::memcmp(data.constData(), Magic, sizeof(Magic)) != 0)
        return failure(QStringLiteral("This is not a DrawApp recovery file."));
    if (data.size() < FixedHeader)
        return failure(damaged(QStringLiteral("header is cut short")));

    const quint32 version = qFromLittleEndian<quint32>(data.constData() + 8);
    const quint32 metaSize = qFromLittleEndian<quint32>(data.constData() + 12);
    if (version == 0 || version > CurrentVersion)
        return failure(QStringLiteral("The recovery file was written by an unsupported version of DrawApp."));
    if (metaSize == 0 || metaSize > quint32(MaxMetadataBytes) || qint64(FixedHeader) + metaSize + 8 > data.size())
        return failure(damaged(QStringLiteral("invalid metadata length")));

    const QByteArray meta = data.mid(FixedHeader, metaSize);
    if (QCryptographicHash::hash(meta, QCryptographicHash::Sha256) != data.mid(16, 32))
        return failure(damaged(QStringLiteral("metadata checksum mismatch")));
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(meta, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return failure(damaged(QStringLiteral("metadata is not valid JSON")));

    const qint64 projectOffset = qint64(FixedHeader) + metaSize + 8;
    const quint64 projectSize = qFromLittleEndian<quint64>(data.constData() + FixedHeader + metaSize);
    if (projectSize != quint64(data.size() - projectOffset))
        return failure(damaged(QStringLiteral("unexpected file length")));

    ProjectFile::LoadResult project = ProjectFile::decode(QByteArrayView(data).sliced(projectOffset));
    if (!project.ok())
        return failure(damaged(project.error));
    // Version 1 containers come from the single-layer milestone and must embed a version 1 project.
    if (version == 1 && project.version != 1)
        return failure(damaged(QStringLiteral("unexpected project version")));

    const QJsonObject json = doc.object();
    Contents c;
    c.document = project.document;
    c.recoveryVersion = version;
    c.projectVersion = project.version;
    c.metadata.documentId = json.value(QStringLiteral("documentId")).toString().toULongLong();
    c.metadata.revision = json.value(QStringLiteral("revision")).toString().toULongLong();
    c.metadata.savedAt = QDateTime::fromString(json.value(QStringLiteral("savedAt")).toString(), Qt::ISODateWithMs);
    c.metadata.originalPath = json.value(QStringLiteral("originalPath")).toString();
    c.metadata.originalUrl = json.value(QStringLiteral("originalUrl")).toString();
    c.metadata.displayName = json.value(QStringLiteral("displayName")).toString(); // optional, milestone 5
    return c;
}

RecoveryFile::Contents RecoveryFile::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("Could not read the recovery file: %1").arg(file.errorString()));
    const qint64 limit = ProjectFile::MaxFileBytes + FixedHeader + MaxMetadataBytes + 8;
    if (file.size() > limit)
        return failure(damaged(QStringLiteral("file is too large")));
    return decode(file.readAll());
}

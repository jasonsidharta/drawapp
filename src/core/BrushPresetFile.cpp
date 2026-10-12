#include "BrushPresetFile.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace {
const QString FormatName = QStringLiteral("drawapp-brush");
}

QByteArray BrushPresetFile::encode(const BrushSettings &settings)
{
    const QJsonObject root{
        {QStringLiteral("format"), FormatName},
        {QStringLiteral("version"), FormatVersion},
        {QStringLiteral("metadata"),
         QJsonObject{{QStringLiteral("app"), QStringLiteral("DrawApp")},
                     {QStringLiteral("settingsVersion"), BrushSettings::FormatVersion}}},
        {QStringLiteral("settings"), settings.sanitized().toJson()},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool BrushPresetFile::decode(const QByteArray &bytes, BrushSettings *out, QString *error, QStringList *warnings)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (bytes.size() > MaxFileBytes)
        return fail(QStringLiteral("The brush file is too large to be a DrawApp brush."));
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("The brush file is damaged or not a DrawApp brush (%1).").arg(parse.errorString()));
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != FormatName)
        return fail(QStringLiteral("The file is not a DrawApp brush (.drawbrush)."));
    const QJsonValue version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() < 1 || version.toDouble() != std::floor(version.toDouble()))
        return fail(QStringLiteral("The brush file has no valid version."));
    if (version.toInt() > FormatVersion)
        return fail(QStringLiteral("The brush was saved by a newer version of DrawApp (format %1).").arg(version.toInt()));
    const QJsonValue settings = root.value(QStringLiteral("settings"));
    if (!settings.isObject())
        return fail(QStringLiteral("The brush file has no settings."));
    BrushSettings s;
    QString why;
    if (!BrushSettings::fromJson(settings.toObject(), &s, &why, warnings))
        return fail(QStringLiteral("The brush file has an invalid setting: %1").arg(why));
    if (s.name.isEmpty())
        s.name = QStringLiteral("Imported brush");
    *out = s;
    return true;
}

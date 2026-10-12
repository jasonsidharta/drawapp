#include "BrushPresetStore.h"

#include "ProjectFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>

namespace {

const QLatin1String FormatName("drawapp-brushes");

bool validCustomId(const QString &id)
{
    if (!id.startsWith(QLatin1String("custom.")) || id.size() > 64)
        return false;
    for (const QChar c : id)
        if (!(c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-') || c == QLatin1Char('_')))
            return false;
    return true;
}

} // namespace

QByteArray BrushPresetStore::encode(const Data &data)
{
    QJsonArray custom;
    for (const BrushPreset &p : data.custom)
        custom.append(QJsonObject{{QStringLiteral("id"), p.id}, {QStringLiteral("settings"), p.settings.toJson()}});
    QJsonObject overrides;
    for (const auto &[id, settings] : data.overrides)
        overrides.insert(id, settings.toJson());
    QJsonObject root{
        {QStringLiteral("format"), FormatName},
        {QStringLiteral("version"), FormatVersion},
        {QStringLiteral("custom"), custom},
        {QStringLiteral("overrides"), overrides},
        {QStringLiteral("favorites"), QJsonArray::fromStringList(data.favorites)},
        {QStringLiteral("selected"),
         QJsonObject{{QStringLiteral("paint"), data.paintId}, {QStringLiteral("eraser"), data.eraserId}}},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool BrushPresetStore::decode(const QByteArray &bytes, Data *out, QString *error, QStringList *warnings)
{
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("The brush library file is damaged (%1).").arg(parse.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != FormatName) {
        *error = QStringLiteral("The file is not a DrawApp brush library.");
        return false;
    }
    const QJsonValue version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() < 1 || version.toDouble() != std::floor(version.toDouble())) {
        *error = QStringLiteral("The brush library has no valid version.");
        return false;
    }
    if (version.toInt() > FormatVersion) {
        *error = QStringLiteral("The brush library was saved by a newer version of DrawApp (format %1); it is "
                                "left unchanged and the built-in brushes are used.")
                     .arg(version.toInt());
        return false;
    }

    Data data;
    QSet<QString> ids;
    const QJsonArray custom = root.value(QStringLiteral("custom")).toArray();
    for (const QJsonValue &entry : custom) {
        const QJsonObject o = entry.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        BrushSettings settings;
        QString why;
        if (!validCustomId(id) || ids.contains(id)) {
            warnings->append(QStringLiteral("Skipped a custom brush with an invalid or repeated id."));
            continue;
        }
        if (!o.value(QStringLiteral("settings")).isObject()
            || !BrushSettings::fromJson(o.value(QStringLiteral("settings")).toObject(), &settings, &why)) {
            warnings->append(QStringLiteral("Skipped custom brush \"%1\": %2").arg(id, why.isEmpty() ? QStringLiteral("no settings") : why));
            continue;
        }
        if (int(data.custom.size()) >= MaxCustomPresets) {
            warnings->append(QStringLiteral("Only the first %1 custom brushes were loaded.").arg(MaxCustomPresets));
            break;
        }
        if (settings.name.isEmpty())
            settings.name = QStringLiteral("Custom brush");
        ids.insert(id);
        data.custom.push_back(BrushPreset{id, settings, false});
    }

    const QJsonObject overrides = root.value(QStringLiteral("overrides")).toObject();
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        const BrushPreset *builtIn = BrushPresets::builtIn(it.key());
        BrushSettings settings;
        QString why;
        if (!builtIn) {
            warnings->append(QStringLiteral("Skipped saved changes to an unknown built-in brush."));
            continue;
        }
        if (!it.value().isObject() || !BrushSettings::fromJson(it.value().toObject(), &settings, &why)) {
            warnings->append(QStringLiteral("Skipped saved changes to \"%1\": %2").arg(builtIn->settings.name, why));
            continue;
        }
        settings.name = builtIn->settings.name; // built-ins keep their names
        data.overrides[it.key()] = settings;
    }

    auto known = [&](const QString &id) { return ids.contains(id) || BrushPresets::builtIn(id); };
    for (const QJsonValue &v : root.value(QStringLiteral("favorites")).toArray()) {
        const QString id = v.toString();
        if (known(id) && !data.favorites.contains(id))
            data.favorites.append(id);
    }
    const QJsonObject selected = root.value(QStringLiteral("selected")).toObject();
    const QString paint = selected.value(QStringLiteral("paint")).toString();
    const QString eraser = selected.value(QStringLiteral("eraser")).toString();
    if (known(paint))
        data.paintId = paint;
    if (known(eraser))
        data.eraserId = eraser;
    *out = std::move(data);
    return true;
}

bool BrushPresetStore::load(const QString &path, Data *out, QString *error, QStringList *warnings)
{
    QFile file(path);
    if (!file.exists()) {
        *out = Data();
        return true;
    }
    if (file.size() > MaxFileBytes) {
        *error = QStringLiteral("The brush library file is too large to be a brush library; it is left unchanged.");
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("The brush library could not be read: %1").arg(file.errorString());
        return false;
    }
    return decode(file.readAll(), out, error, warnings);
}

QString BrushPresetStore::save(const QString &path, const Data &data)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return QStringLiteral("The folder for the brush library could not be created.");
    return ProjectFile::writeFileSafely(path, encode(data));
}

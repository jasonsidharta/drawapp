#include "BrushLibrary.h"

#include "BrushProperties.h"
#include "BrushThumbnails.h"

#include <QJsonObject>
#include <QJsonValue>

namespace {

const char *blendName(BrushBlend b)
{
    switch (b) {
    case BrushBlend::Erase: return "erase";
    case BrushBlend::Smudge: return "smudge";
    case BrushBlend::WetPaint: return "wet";
    default: return "paint";
    }
}

// Equal apart from the size, which is a temporary per-session value.
bool sameIgnoringSize(BrushSettings a, const BrushSettings &b)
{
    a.size = b.size;
    return a == b;
}

} // namespace

BrushLibrary::BrushLibrary(QObject *parent)
    : QObject(parent)
    , m_paintId(BrushPresets::defaultPaintId())
    , m_eraserId(BrushPresets::defaultEraserId())
{
    // Coalesces quick successive changes (favourites, selections) into one write.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &BrushLibrary::flush);
    for (const BrushPreset &p : BrushPresets::builtIns())
        publishThumbnail(p.id);
}

BrushLibrary::~BrushLibrary()
{
    flush();
}

void BrushLibrary::setStorePath(const QString &path)
{
    flush();
    m_storePath = path;
    m_storeWritable = false;
    m_edits.clear();
    BrushPresetStore::Data data;
    QString error;
    QStringList warnings;
    if (path.isEmpty()) {
        m_data = BrushPresetStore::Data();
    } else if (BrushPresetStore::load(path, &data, &error, &warnings)) {
        m_data = std::move(data);
        m_storeWritable = true;
    } else {
        m_data = BrushPresetStore::Data(); // the file is left untouched
    }
    m_paintId = hasPreset(m_data.paintId) ? m_data.paintId : BrushPresets::defaultPaintId();
    m_eraserId = hasPreset(m_data.eraserId) ? m_data.eraserId : BrushPresets::defaultEraserId();
    for (const BrushPreset &p : BrushPresets::builtIns())
        publishThumbnail(p.id);
    for (const BrushPreset &p : m_data.custom)
        publishThumbnail(p.id);
    setStoreStatus(!error.isEmpty() ? error : warnings.join(QLatin1Char('\n')));
    emit presetsChanged();
    emit currentChanged();
}

void BrushLibrary::setEraserSlot(bool eraser)
{
    if (eraser == m_eraserSlot)
        return;
    m_eraserSlot = eraser;
    emit currentChanged();
    emit presetsChanged(); // "current" moves
}

const BrushPreset *BrushLibrary::customPreset(const QString &id) const
{
    for (const BrushPreset &p : m_data.custom)
        if (p.id == id)
            return &p;
    return nullptr;
}

BrushPreset *BrushLibrary::customPreset(const QString &id)
{
    for (BrushPreset &p : m_data.custom)
        if (p.id == id)
            return &p;
    return nullptr;
}

bool BrushLibrary::hasPreset(const QString &id) const
{
    return BrushPresets::builtIn(id) || customPreset(id);
}

BrushSettings BrushLibrary::savedSettings(const QString &id) const
{
    if (const BrushPreset *custom = customPreset(id))
        return custom->settings;
    const auto override = m_data.overrides.find(id);
    if (override != m_data.overrides.end())
        return override->second;
    if (const BrushPreset *builtIn = BrushPresets::builtIn(id))
        return builtIn->settings;
    return BrushSettings();
}

BrushSettings BrushLibrary::workingSettings(const QString &id) const
{
    const auto edit = m_edits.find(id);
    return edit != m_edits.end() ? edit->second : savedSettings(id);
}

BrushSettings BrushLibrary::settings(bool eraser) const
{
    return workingSettings(selectedId(eraser));
}

QString BrushLibrary::thumbnailUrl(const QString &id) const
{
    const auto it = m_thumbRevision.find(id);
    return QStringLiteral("image://brushthumb/%1/%2").arg(id).arg(it == m_thumbRevision.end() ? 0 : it->second);
}

void BrushLibrary::publishThumbnail(const QString &id)
{
    BrushThumbnails::publish(id, workingSettings(id));
    ++m_thumbRevision[id];
}

QVariantList BrushLibrary::presets() const
{
    QVariantList list;
    auto add = [&](const QString &id, bool builtIn) {
        const BrushSettings working = workingSettings(id);
        list.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), working.name},
            {QStringLiteral("builtIn"), builtIn},
            {QStringLiteral("favorite"), m_data.favorites.contains(id)},
            {QStringLiteral("modified"), !sameIgnoringSize(working, savedSettings(id))},
            {QStringLiteral("savedChanges"), builtIn && m_data.overrides.count(id) > 0},
            {QStringLiteral("blend"), QLatin1String(blendName(working.blend))},
            {QStringLiteral("thumbnail"), thumbnailUrl(id)},
            {QStringLiteral("current"), id == currentId()},
            {QStringLiteral("inUse"), id == m_paintId || id == m_eraserId},
        });
    };
    for (const BrushPreset &p : BrushPresets::builtIns())
        add(p.id, true);
    for (const BrushPreset &p : m_data.custom)
        add(p.id, false);
    return list;
}

QString BrushLibrary::currentName() const
{
    return workingSettings(currentId()).name;
}

QVariantMap BrushLibrary::current() const
{
    return workingSettings(currentId()).toJson().toVariantMap();
}

bool BrushLibrary::currentBuiltIn() const
{
    return BrushPresets::builtIn(currentId()) != nullptr;
}

bool BrushLibrary::currentModified() const
{
    return !sameIgnoringSize(workingSettings(currentId()), savedSettings(currentId()));
}

bool BrushLibrary::currentSizeChanged() const
{
    return workingSettings(currentId()).size != savedSettings(currentId()).size;
}

bool BrushLibrary::currentHasSavedChanges() const
{
    return currentBuiltIn() && m_data.overrides.count(currentId()) > 0;
}

bool BrushLibrary::select(const QString &id)
{
    if (!hasPreset(id))
        return false;
    const bool eraser = workingSettings(id).blend == BrushBlend::Erase;
    QString &slot = eraser ? m_eraserId : m_paintId;
    const bool changed = slot != id || m_eraserSlot != eraser;
    slot = id;
    if (eraser) {
        m_data.eraserId = id;
    } else {
        m_data.paintId = id;
    }
    if (m_eraserSlot != eraser) {
        m_eraserSlot = eraser;
        emit toolRequested(eraser);
    }
    if (changed) {
        scheduleSave();
        emit currentChanged();
        emit presetsChanged();
    }
    return true;
}

void BrushLibrary::setValue(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("blend") || key == QLatin1String("name") || key.isEmpty())
        return;
    const QString id = currentId();
    const BrushSettings before = workingSettings(id);
    BrushSettings after = before;
    if (!applyValue(&after, key, value))
        return;
    after = after.sanitized();
    after.name = before.name;
    after.blend = before.blend;
    if (after != before)
        setWorking(id, after);
}

bool BrushLibrary::applyValue(BrushSettings *settings, const QString &key, const QVariant &value)
{
    // "pressureSize.minimum" sets one field of a pressure response; everything else is a
    // registry id ("dynamics.size" included).
    const int dot = key.indexOf(QLatin1Char('.'));
    const BrushProperty *p = BrushProperties::find(key);
    QVariant v = value;
    if (!p && dot > 0) {
        p = BrushProperties::find(key.left(dot));
        if (!p || p->type != PropertyType::Pressure)
            return false;
        const QString field = key.mid(dot + 1);
        const bool wantBool = field == QLatin1String("enabled");
        if (!wantBool && field != QLatin1String("minimum") && field != QLatin1String("gamma"))
            return false;
        if (wantBool ? value.typeId() != QMetaType::Bool
                     : (value.typeId() == QMetaType::QString || !value.canConvert<double>()))
            return false;
        v = QVariantMap{{field, value}};
    }
    if (!p || !p->implemented)
        return false;
    // The same type rules as reading a preset: a value of the wrong kind changes nothing.
    const int type = value.typeId();
    const bool isText = type == QMetaType::QString;
    switch (p->type) {
    case PropertyType::Number:
    case PropertyType::Integer:
        if (p != BrushProperties::find(key) || isText || !value.canConvert<double>())
            return false;
        break;
    case PropertyType::Bool:
        if (type != QMetaType::Bool)
            return false;
        break;
    case PropertyType::Choice:
        if (!isText || (p->id != QLatin1String("tip") && p->id != QLatin1String("texture") && !p->choices.contains(value.toString())))
            return false;
        break;
    case PropertyType::Text:
        if (!isText)
            return false;
        break;
    case PropertyType::Pressure:
    case PropertyType::Dynamics:
        if (p != BrushProperties::find(key) && p->type == PropertyType::Dynamics)
            return false;
        if (p == BrushProperties::find(key) && !value.canConvert<QVariantMap>())
            return false;
        break;
    case PropertyType::Image:
        break;
    }
    p->set(*settings, v);
    return true;
}

void BrushLibrary::setWorking(const QString &id, const BrushSettings &settings)
{
    if (settings == savedSettings(id))
        m_edits.erase(id);
    else
        m_edits[id] = settings;
    publishThumbnail(id);
    emit currentChanged();
    emit presetsChanged();
}

void BrushLibrary::toggleFavorite(const QString &id)
{
    if (!hasPreset(id))
        return;
    if (m_data.favorites.contains(id))
        m_data.favorites.removeAll(id);
    else
        m_data.favorites.append(id);
    scheduleSave();
    emit presetsChanged();
}

void BrushLibrary::saveCurrent()
{
    const QString id = currentId();
    const auto edit = m_edits.find(id);
    if (edit == m_edits.end())
        return;
    if (BrushPreset *custom = customPreset(id)) {
        custom->settings = edit->second;
    } else if (const BrushPreset *builtIn = BrushPresets::builtIn(id)) {
        if (edit->second == builtIn->settings)
            m_data.overrides.erase(id);
        else
            m_data.overrides[id] = edit->second;
    }
    m_edits.erase(edit);
    flush();
    emit currentChanged();
    emit presetsChanged();
}

QString BrushLibrary::newCustomId() const
{
    int next = 1;
    for (const BrushPreset &p : m_data.custom) {
        bool ok = false;
        const int n = p.id.mid(7).toInt(&ok);
        if (ok)
            next = std::max(next, n + 1);
    }
    return QStringLiteral("custom.%1").arg(next);
}

QString BrushLibrary::saveAsNew(const QString &name)
{
    if (int(m_data.custom.size()) >= BrushPresetStore::MaxCustomPresets) {
        setStoreStatus(QStringLiteral("There are already %1 custom brushes; delete one first.")
                           .arg(BrushPresetStore::MaxCustomPresets));
        return QString();
    }
    BrushSettings settings = workingSettings(currentId());
    const QString trimmed = name.trimmed();
    settings.name = (trimmed.isEmpty() ? settings.name + QStringLiteral(" copy") : trimmed).left(64);
    const QString id = newCustomId();
    m_data.custom.push_back(BrushPreset{id, settings.sanitized(), false});
    publishThumbnail(id);
    flush();
    select(id);
    emit presetsChanged();
    emit currentChanged();
    return id;
}

void BrushLibrary::resetCurrent()
{
    const QString id = currentId();
    const bool hadEdits = m_edits.erase(id) > 0;
    const bool hadOverride = BrushPresets::builtIn(id) && m_data.overrides.erase(id) > 0;
    if (!hadEdits && !hadOverride)
        return;
    publishThumbnail(id);
    if (hadOverride)
        flush();
    emit currentChanged();
    emit presetsChanged();
}

void BrushLibrary::renameCurrent(const QString &name)
{
    BrushPreset *custom = customPreset(currentId());
    const QString trimmed = name.trimmed().left(64);
    if (!custom || trimmed.isEmpty() || trimmed == custom->settings.name)
        return;
    custom->settings.name = trimmed;
    const auto edit = m_edits.find(custom->id);
    if (edit != m_edits.end())
        edit->second.name = trimmed;
    flush();
    emit currentChanged();
    emit presetsChanged();
}

bool BrushLibrary::deleteCurrent()
{
    const QString id = currentId();
    const auto it = std::find_if(m_data.custom.begin(), m_data.custom.end(), [&](const BrushPreset &p) { return p.id == id; });
    if (it == m_data.custom.end())
        return false;
    m_data.custom.erase(it);
    m_edits.erase(id);
    m_data.favorites.removeAll(id);
    if (m_paintId == id)
        m_paintId = m_data.paintId = BrushPresets::defaultPaintId();
    if (m_eraserId == id)
        m_eraserId = m_data.eraserId = BrushPresets::defaultEraserId();
    flush();
    emit currentChanged();
    emit presetsChanged();
    return true;
}

void BrushLibrary::scheduleSave()
{
    if (m_storeWritable)
        m_saveTimer.start();
}

void BrushLibrary::flush()
{
    m_saveTimer.stop();
    if (!m_storeWritable)
        return;
    m_data.paintId = m_paintId;
    m_data.eraserId = m_eraserId;
    const QString error = BrushPresetStore::save(m_storePath, m_data);
    setStoreStatus(error.isEmpty() ? QString() : QStringLiteral("Brushes could not be saved: ") + error);
}

void BrushLibrary::setStoreStatus(const QString &status)
{
    if (status == m_storeStatus)
        return;
    m_storeStatus = status;
    emit storeStatusChanged();
}

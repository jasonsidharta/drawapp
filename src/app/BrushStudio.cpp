#include "BrushStudio.h"

#include "BrushLibrary.h"
#include "BrushPresetFile.h"
#include "BrushThumbnails.h"
#include "DocumentProvider.h"
#include "ProjectFile.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>

namespace {

const QString StudioThumbnailId = QStringLiteral("__studio");

bool isContent(const QUrl &url)
{
    return !url.isLocalFile() && url.scheme() == QLatin1String("content");
}

QString displayName(const QUrl &url)
{
    return url.isLocalFile() ? QFileInfo(url.toLocalFile()).fileName() : url.fileName();
}

std::vector<QPointF> pointsFrom(const QVariantList &list)
{
    std::vector<QPointF> points;
    for (const QVariant &v : list) {
        if (v.canConvert<QVariantList>() && v.toList().size() == 2) {
            const QVariantList pair = v.toList();
            points.emplace_back(pair[0].toDouble(), pair[1].toDouble());
        } else if (v.canConvert<QVariantMap>()) {
            const QVariantMap m = v.toMap();
            points.emplace_back(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble());
        } else if (v.canConvert<QPointF>()) {
            points.push_back(v.toPointF());
        }
    }
    return points;
}

QVariantList pointsTo(const std::vector<QPointF> &points)
{
    QVariantList list;
    for (const QPointF &p : points)
        list.append(QVariant(QVariantList{p.x(), p.y()}));
    return list;
}

QString engineNames(unsigned e)
{
    if (e == EngineAll)
        return QStringLiteral("every engine");
    QStringList n;
    const char *names[] = {"paint", "erase", "smudge", "wet paint", "spray", "hatching"};
    for (int i = 0; i < 6; ++i)
        if (e & (1u << i))
            n << QLatin1String(names[i]);
    return n.join(QStringLiteral(", "));
}

const char *typeName(PropertyType t)
{
    switch (t) {
    case PropertyType::Number: return "number";
    case PropertyType::Integer: return "integer";
    case PropertyType::Bool: return "bool";
    case PropertyType::Choice: return "choice";
    case PropertyType::Text: return "text";
    case PropertyType::Image: return "image";
    case PropertyType::Pressure: return "pressure";
    case PropertyType::Dynamics: return "dynamics";
    }
    return "";
}

} // namespace

BrushStudio::BrushStudio(BrushLibrary *library)
    : QObject(library)
    , m_library(library)
    , m_group(BrushProperties::groups().first())
{
}

bool BrushStudio::builtIn() const
{
    return m_presetId.startsWith(QLatin1String("builtin."));
}

QString BrushStudio::engine() const
{
    return BrushProperties::find(QStringLiteral("blend"))->get(m_edit).toString();
}

QStringList BrushStudio::groups() const
{
    return BrushProperties::groups();
}

void BrushStudio::setGroup(const QString &group)
{
    if (group == m_group || !groups().contains(group))
        return;
    m_group = group;
    emit viewChanged();
    emit propertiesChanged();
}

void BrushStudio::setSearch(const QString &text)
{
    if (text == m_search)
        return;
    m_search = text;
    emit viewChanged();
    emit propertiesChanged();
}

int BrushStudio::implementedCount() const
{
    return BrushProperties::implementedCount();
}

int BrushStudio::designCount() const
{
    return BrushProperties::designTargetCount();
}

QStringList BrushStudio::sensorIds() const
{
    QStringList ids;
    for (int i = 0; i < SensorCount; ++i)
        ids << Sensors::id(Sensor(i));
    return ids;
}

QStringList BrushStudio::sensorLabels() const
{
    QStringList labels;
    for (int i = 0; i < SensorCount; ++i)
        labels << Sensors::label(Sensor(i));
    return labels;
}

QString BrushStudio::sensorDescription(const QString &sensor) const
{
    Sensor s;
    return Sensors::fromId(sensor, &s) ? Sensors::description(s) : QString();
}

bool BrushStudio::sensorUsesRange(const QString &sensor) const
{
    Sensor s;
    return Sensors::fromId(sensor, &s) && Sensors::usesRange(s);
}

QString BrushStudio::sensorRangeUnit(const QString &sensor) const
{
    Sensor s;
    return Sensors::fromId(sensor, &s) ? Sensors::rangeUnit(s) : QString();
}

QString BrushStudio::preview() const
{
    return QStringLiteral("image://brushthumb/%1/%2").arg(StudioThumbnailId).arg(m_revision);
}

QVariantMap BrushStudio::row(const BrushProperty &p) const
{
    QVariantMap r{
        {QStringLiteral("id"), p.id},
        {QStringLiteral("name"), p.name},
        {QStringLiteral("description"), p.description},
        {QStringLiteral("group"), p.group},
        {QStringLiteral("type"), QLatin1String(typeName(p.type))},
        {QStringLiteral("min"), p.minimum},
        {QStringLiteral("max"), p.maximum},
        {QStringLiteral("scale"), p.displayScale},
        {QStringLiteral("unit"), p.unit},
        {QStringLiteral("choices"), p.choices},
        {QStringLiteral("choiceLabels"), p.choiceLabels},
        {QStringLiteral("implemented"), p.implemented},
        {QStringLiteral("advanced"), p.advanced},
        {QStringLiteral("requires"), p.requires},
        {QStringLiteral("engines"), engineNames(p.engines)},
        {QStringLiteral("applies"), BrushProperties::appliesTo(p, m_edit)},
    };
    if (!p.implemented)
        return r;
    if (p.type == PropertyType::Image) {
        r[QStringLiteral("value")] = !m_edit.tipImage.isNull();
        r[QStringLiteral("saved")] = !m_saved.tipImage.isNull();
        r[QStringLiteral("modified")] = m_edit.tipImage != m_saved.tipImage;
    } else {
        const QVariant v = p.get(m_edit), s = p.get(m_saved);
        r[QStringLiteral("value")] = v;
        r[QStringLiteral("saved")] = s;
        r[QStringLiteral("modified")] = v != s;
    }
    return r;
}

QStringList BrushStudio::propertyIds() const
{
    QStringList ids;
    const QString needle = m_search.trimmed();
    for (const BrushProperty &p : BrushProperties::all()) {
        const bool match = needle.isEmpty()
            ? p.group == m_group
            : (p.name.contains(needle, Qt::CaseInsensitive) || p.id.contains(needle, Qt::CaseInsensitive)
               || p.description.contains(needle, Qt::CaseInsensitive));
        if (match)
            ids << p.id;
    }
    return ids;
}

QVariantMap BrushStudio::propertyRow(const QString &id) const
{
    const BrushProperty *p = BrushProperties::find(id);
    return p ? row(*p) : QVariantMap();
}

QVariantList BrushStudio::properties() const
{
    QVariantList rows;
    for (const QString &id : propertyIds())
        rows.append(propertyRow(id));
    return rows;
}

bool BrushStudio::open()
{
    return openPreset(m_library->currentId());
}

bool BrushStudio::openPreset(const QString &id)
{
    if (!m_library->hasPreset(id))
        return false;
    m_presetId = id;
    m_original = m_library->workingSettings(id);
    m_saved = m_library->savedSettings(id);
    m_edit = m_original;
    m_active = true;
    setStatus(QString());
    BrushThumbnails::publish(StudioThumbnailId, m_edit);
    ++m_revision;
    emit activeChanged();
    emit changed();
    emit propertiesChanged();
    return true;
}

void BrushStudio::setEdit(const BrushSettings &settings)
{
    const BrushSettings next = settings.sanitized();
    if (next == m_edit)
        return;
    m_edit = next;
    BrushThumbnails::publish(StudioThumbnailId, m_edit);
    ++m_revision;
    emit changed();
    emit propertiesChanged();
}

void BrushStudio::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}

void BrushStudio::setValue(const QString &id, const QVariant &value)
{
    if (!m_active)
        return;
    BrushSettings next = m_edit;
    if (id == QLatin1String("blend")) {
        // The eraser slot holds erasers and the paint slot everything else.
        const QString v = value.toString();
        if (engineLocked() || v == QLatin1String("erase"))
            return;
        BrushProperties::find(id)->set(next, v);
    } else if (!BrushLibrary::applyValue(&next, id, value)) {
        return;
    }
    setEdit(next);
}

QVariant BrushStudio::value(const QString &id) const
{
    const BrushProperty *p = BrushProperties::find(id);
    return p && p->implemented ? p->get(m_edit) : QVariant();
}

void BrushStudio::resetValue(const QString &id)
{
    const BrushProperty *p = BrushProperties::find(id);
    if (!m_active || !p || !p->implemented)
        return;
    BrushSettings next = m_edit;
    if (p->type == PropertyType::Image)
        next.tipImage = m_saved.tipImage;
    else
        p->set(next, p->get(m_saved));
    setEdit(next);
}

void BrushStudio::resetAll()
{
    if (m_active)
        setEdit(m_saved);
}

ParameterDynamics BrushStudio::dynamics(const QString &id) const
{
    DynamicTarget t;
    if (!id.startsWith(QLatin1String("dynamics.")) || !DynamicTargets::fromId(id.mid(9), &t))
        return ParameterDynamics();
    return m_edit.dynamicsFor(t);
}

void BrushStudio::setDynamics(const QString &id, const ParameterDynamics &d)
{
    DynamicTarget t;
    if (!m_active || !id.startsWith(QLatin1String("dynamics.")) || !DynamicTargets::fromId(id.mid(9), &t))
        return;
    BrushSettings next = m_edit;
    next.dynamicsFor(t) = d.sanitized();
    setEdit(next);
}

void BrushStudio::setDynamicsField(const QString &id, const QString &field, const QVariant &value)
{
    ParameterDynamics d = dynamics(id);
    if (field == QLatin1String("enabled")) {
        d.enabled = value.toBool();
        if (d.enabled && d.mappings.empty())
            d.mappings.push_back({Sensor::Pressure, SensorCurve(), 0}); // something to start from
    } else if (field == QLatin1String("minimum")) {
        d.minimum = value.toDouble();
    } else if (field == QLatin1String("combine")) {
        const QStringList names{"multiply", "add", "max", "min", "difference"};
        const int i = names.indexOf(value.toString());
        if (i < 0)
            return;
        d.combine = CombineMode(i);
    } else {
        return;
    }
    setDynamics(id, d);
}

void BrushStudio::addSensor(const QString &id, const QString &sensor)
{
    ParameterDynamics d = dynamics(id);
    Sensor s;
    if (!Sensors::fromId(sensor, &s) || int(d.mappings.size()) >= ParameterDynamics::MaxMappings)
        return;
    d.mappings.push_back({s, SensorCurve(), 0});
    d.enabled = true;
    setDynamics(id, d);
}

void BrushStudio::removeSensor(const QString &id, int index)
{
    ParameterDynamics d = dynamics(id);
    if (index < 0 || index >= int(d.mappings.size()))
        return;
    d.mappings.erase(d.mappings.begin() + index);
    if (d.mappings.empty())
        d.enabled = false;
    setDynamics(id, d);
}

void BrushStudio::setSensor(const QString &id, int index, const QString &sensor)
{
    ParameterDynamics d = dynamics(id);
    Sensor s;
    if (index < 0 || index >= int(d.mappings.size()) || !Sensors::fromId(sensor, &s))
        return;
    d.mappings[size_t(index)].sensor = s;
    d.mappings[size_t(index)].range = 0; // the new sensor's default range
    setDynamics(id, d);
}

void BrushStudio::setSensorRange(const QString &id, int index, double range)
{
    ParameterDynamics d = dynamics(id);
    if (index < 0 || index >= int(d.mappings.size()))
        return;
    d.mappings[size_t(index)].range = range;
    setDynamics(id, d);
}

void BrushStudio::setSensorCurve(const QString &id, int index, const QVariantList &points)
{
    ParameterDynamics d = dynamics(id);
    if (index < 0 || index >= int(d.mappings.size()))
        return;
    d.mappings[size_t(index)].curve = SensorCurve(pointsFrom(points));
    setDynamics(id, d);
}

QVariantList BrushStudio::curvePreset(const QString &name) const
{
    SensorCurve c;
    if (name == QLatin1String("inverted"))
        c = SensorCurve::inverted();
    else if (name == QLatin1String("threshold"))
        c = SensorCurve::threshold(0.5);
    else if (name == QLatin1String("soft"))
        c = SensorCurve::exponential(0.5);
    else if (name == QLatin1String("firm"))
        c = SensorCurve::exponential(2.0);
    else if (name == QLatin1String("clamped"))
        c = SensorCurve::clamped(0.2, 0.8);
    return pointsTo(c.points());
}

QVariantList BrushStudio::sampleCurve(const QVariantList &points, int count) const
{
    const SensorCurve c(pointsFrom(points));
    std::vector<QPointF> out;
    const int n = std::clamp(count, 2, 512);
    for (int i = 0; i <= n; ++i)
        out.emplace_back(double(i) / n, c.value(double(i) / n));
    return pointsTo(out);
}

QString BrushStudio::readFile(const QUrl &url, qint64 maxBytes, QByteArray *bytes)
{
    if (isContent(url)) {
        QString error;
        *bytes = DocumentProvider::platform()->read(url.toString(), maxBytes, &error);
        if (!error.isEmpty())
            return error;
    } else {
        QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
        if (!file.open(QIODevice::ReadOnly))
            return QStringLiteral("Could not open \"%1\": %2").arg(displayName(url), file.errorString());
        *bytes = ProjectFile::readBounded(file, maxBytes);
    }
    if (bytes->size() > maxBytes)
        return QStringLiteral("\"%1\" is too large.").arg(displayName(url));
    return QString();
}

QString BrushStudio::writeFile(const QUrl &url, const QByteArray &bytes)
{
    if (isContent(url)) {
        bool touched = false;
        return DocumentProvider::platform()->write(url.toString(), bytes, &touched);
    }
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (QFileInfo(path).suffix().isEmpty())
        path += QLatin1Char('.') + QLatin1String(BrushPresetFile::Suffix);
    return ProjectFile::writeFileSafely(path, bytes);
}

QString BrushStudio::importTip(const QUrl &url)
{
    if (!m_active)
        return QString();
    QByteArray bytes;
    QString error = readFile(url, 32 * 1024 * 1024, &bytes);
    if (error.isEmpty()) {
        QBuffer buffer(&bytes);
        QImageReader reader(&buffer);
        const QSize size = reader.size();
        QImage image;
        if (size.isValid() && qint64(size.width()) * size.height() > 64ll * 1024 * 1024)
            error = QStringLiteral("The image is too large for a brush tip.");
        else if (!reader.read(&image))
            error = QStringLiteral("\"%1\" could not be read as an image (%2).").arg(displayName(url), reader.errorString());
        else {
            const QImage tip = BrushProperties::tipFromImage(image);
            if (tip.isNull()) {
                error = QStringLiteral("The image has nothing to use as a tip.");
            } else {
                BrushSettings next = m_edit;
                next.tipImage = tip;
                setEdit(next);
            }
        }
    }
    setStatus(error);
    return error;
}

void BrushStudio::clearTip()
{
    if (!m_active)
        return;
    BrushSettings next = m_edit;
    next.tipImage = QImage();
    setEdit(next);
}

void BrushStudio::close()
{
    m_active = false;
    emit activeChanged();
}

QString BrushStudio::done()
{
    if (!m_active)
        return QString();
    if (m_edit != m_saved || m_edit != m_original)
        m_library->storePreset(m_presetId, m_edit);
    close();
    return QString();
}

void BrushStudio::cancel()
{
    if (m_active)
        close();
}

QString BrushStudio::duplicate(const QString &name)
{
    if (!m_active)
        return QString();
    const QString trimmed = name.trimmed();
    const QString id = m_library->addPreset(m_edit, trimmed.isEmpty() ? m_edit.name + QStringLiteral(" copy") : trimmed);
    if (id.isEmpty()) {
        setStatus(m_library->storeStatus());
        return m_status;
    }
    m_presetId = id;
    m_original = m_saved = m_edit = m_library->savedSettings(id);
    BrushThumbnails::publish(StudioThumbnailId, m_edit);
    ++m_revision;
    setStatus(QStringLiteral("Saved as a new brush, \"%1\".").arg(m_edit.name));
    emit activeChanged();
    emit changed();
    emit propertiesChanged();
    return QString();
}

QString BrushStudio::rename(const QString &name)
{
    if (!m_active)
        return QString();
    if (builtIn())
        return QStringLiteral("Built-in brushes keep their names. Duplicate the brush to make your own copy.");
    const QString trimmed = name.trimmed().left(64);
    if (trimmed.isEmpty())
        return QStringLiteral("A brush needs a name.");
    BrushSettings next = m_edit;
    next.name = trimmed;
    setEdit(next);
    return QString();
}

QString BrushStudio::exportTo(const QUrl &url)
{
    if (!m_active)
        return QString();
    const QString error = writeFile(url, BrushPresetFile::encode(m_edit));
    setStatus(error.isEmpty() ? QStringLiteral("Exported \"%1\".").arg(displayName(url)) : error);
    return error;
}

QString BrushStudio::importFrom(const QUrl &url)
{
    QByteArray bytes;
    QString error = readFile(url, BrushPresetFile::MaxFileBytes, &bytes);
    BrushSettings settings;
    QStringList warnings;
    if (error.isEmpty() && !BrushPresetFile::decode(bytes, &settings, &error, &warnings) && error.isEmpty())
        error = QStringLiteral("The brush could not be imported.");
    if (!error.isEmpty()) {
        setStatus(error);
        return error;
    }
    const QString id = m_library->addPreset(settings, settings.name);
    if (id.isEmpty()) {
        setStatus(m_library->storeStatus());
        return m_status;
    }
    openPreset(id); // edit the imported brush
    setStatus(warnings.isEmpty() ? QStringLiteral("Imported \"%1\".").arg(settings.name)
                                 : QStringLiteral("Imported \"%1\" with notes: %2").arg(settings.name, warnings.join(QLatin1Char(' '))));
    return QString();
}

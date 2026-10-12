#pragma once

#include "BrushProperties.h"
#include "BrushSettings.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class BrushLibrary;

// The Brush Studio's model: every registry property of one preset, edited on a copy. Nothing the
// artist does here touches the preset (or the working brush) until done(); cancel() drops the copy.
// Owned by BrushLibrary (canvas.brushes.studio in QML).
//
// Rows (properties) are plain maps built from BrushProperties, so the QML only lays them out:
//   {id, name, description, group, type, min, max, scale, unit, choices, choiceLabels, value,
//    saved, modified, applies, requires, engines, implemented, advanced}
// "saved" is the preset's stored value (what "reset" returns to); "applies" says whether the
// current engine uses the property ("requires" names any further condition).
class BrushStudio : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use canvas.brushes.studio")
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString presetId READ presetId NOTIFY activeChanged)
    Q_PROPERTY(bool builtIn READ builtIn NOTIFY activeChanged)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool modified READ modified NOTIFY changed)
    Q_PROPERTY(QString engine READ engine NOTIFY changed) // registry value of "blend"
    Q_PROPERTY(bool engineLocked READ engineLocked NOTIFY activeChanged) // erasers stay erasers
    Q_PROPERTY(QStringList groups READ groups CONSTANT)
    Q_PROPERTY(QString group READ group WRITE setGroup NOTIFY viewChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY viewChanged)
    Q_PROPERTY(QVariantList properties READ properties NOTIFY propertiesChanged)
    // The ids shown for the group or search (changes only when the view does, so QML delegates
    // survive edits); each delegate reads its row with propertyRow(id) and re-reads on revision.
    Q_PROPERTY(QStringList propertyIds READ propertyIds NOTIFY viewChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString preview READ preview NOTIFY changed)
    Q_PROPERTY(int implementedCount READ implementedCount CONSTANT)
    Q_PROPERTY(int designCount READ designCount CONSTANT)
    Q_PROPERTY(QStringList sensorIds READ sensorIds CONSTANT)
    Q_PROPERTY(QStringList sensorLabels READ sensorLabels CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit BrushStudio(BrushLibrary *library);

    bool active() const { return m_active; }
    QString presetId() const { return m_presetId; }
    bool builtIn() const;
    QString name() const { return m_edit.name; }
    bool modified() const { return m_edit != m_original; }
    QString engine() const;
    bool engineLocked() const { return m_original.blend == BrushBlend::Erase; }
    QStringList groups() const;
    QString group() const { return m_group; }
    void setGroup(const QString &group);
    QString search() const { return m_search; }
    void setSearch(const QString &text);
    QVariantList properties() const;
    QStringList propertyIds() const;
    int revision() const { return m_revision; }
    Q_INVOKABLE QVariantMap propertyRow(const QString &id) const;
    QString preview() const;
    int implementedCount() const;
    int designCount() const;
    QStringList sensorIds() const;
    QStringList sensorLabels() const;
    QString status() const { return m_status; }

    // The settings being edited (the preview pad paints with these while the studio is open).
    const BrushSettings &settings() const { return m_edit; }

    // Starts editing a copy of the current preset's working settings (or of the preset id).
    Q_INVOKABLE bool open();
    Q_INVOKABLE bool openPreset(const QString &id);
    // One property by registry id; the same rules as BrushLibrary::setValue (wrong kinds of value
    // are ignored, values are clamped). "blend" changes the engine (not for erasers).
    Q_INVOKABLE void setValue(const QString &id, const QVariant &value);
    Q_INVOKABLE QVariant value(const QString &id) const;
    // Back to the preset's saved value / the whole preset as saved.
    Q_INVOKABLE void resetValue(const QString &id);
    Q_INVOKABLE void resetAll();

    // Dynamics of a property ("dynamics.size"): fields enabled, minimum, combine; sensors by index.
    Q_INVOKABLE void setDynamicsField(const QString &id, const QString &field, const QVariant &value);
    Q_INVOKABLE void addSensor(const QString &id, const QString &sensor);
    Q_INVOKABLE void removeSensor(const QString &id, int index);
    Q_INVOKABLE void setSensor(const QString &id, int index, const QString &sensor);
    Q_INVOKABLE void setSensorRange(const QString &id, int index, double range);
    // points: [[x, y], ...] in 0..1.
    Q_INVOKABLE void setSensorCurve(const QString &id, int index, const QVariantList &points);
    // Preset curves: "linear", "inverted", "threshold", "soft", "firm", "clamped".
    Q_INVOKABLE QVariantList curvePreset(const QString &name) const;
    // count + 1 evenly spaced (x, y) points of the curve, for drawing it exactly as rendered.
    Q_INVOKABLE QVariantList sampleCurve(const QVariantList &points, int count) const;
    Q_INVOKABLE QString sensorDescription(const QString &sensor) const;
    Q_INVOKABLE bool sensorUsesRange(const QString &sensor) const;
    Q_INVOKABLE QString sensorRangeUnit(const QString &sensor) const;

    // An image file as the tip (alpha, or darkness for opaque images); empty or an error message.
    Q_INVOKABLE QString importTip(const QUrl &url);
    Q_INVOKABLE void clearTip();

    // Saves the edited settings into the preset (and its working brush) and closes; returns an
    // error message or "".
    Q_INVOKABLE QString done();
    // Closes without changing anything.
    Q_INVOKABLE void cancel();
    // A new custom preset from the edited settings; the studio goes on editing the new preset and
    // the original stays as it was. Returns an error message or "".
    Q_INVOKABLE QString duplicate(const QString &name);
    // Renames the edited preset (custom presets only; built-ins keep their names).
    Q_INVOKABLE QString rename(const QString &name);
    // The edited settings as a .drawbrush file / a .drawbrush file as a new custom preset (which
    // the studio then edits). Error message or "".
    Q_INVOKABLE QString exportTo(const QUrl &url);
    Q_INVOKABLE QString importFrom(const QUrl &url);

    // For tests: read and write without file dialogs.
    static QString readFile(const QUrl &url, qint64 maxBytes, QByteArray *bytes);
    static QString writeFile(const QUrl &url, const QByteArray &bytes);

signals:
    void activeChanged();
    void changed();
    void viewChanged();
    void propertiesChanged();
    void statusChanged();

private:
    void setEdit(const BrushSettings &settings);
    void setStatus(const QString &status);
    void close();
    ParameterDynamics dynamics(const QString &id) const;
    void setDynamics(const QString &id, const ParameterDynamics &d);
    QVariantMap row(const BrushProperty &p) const;

    BrushLibrary *m_library;
    bool m_active = false;
    QString m_presetId;
    BrushSettings m_original; // the preset's working settings when the studio opened
    BrushSettings m_saved;    // the preset as saved (reset target)
    BrushSettings m_edit;
    QString m_group;
    QString m_search;
    QString m_status;
    int m_revision = 0;
};

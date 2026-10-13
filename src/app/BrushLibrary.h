#pragma once

#include "BrushPresetStore.h"
#include "BrushStudio.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <map>

// The presets the artist chooses from and the two working brushes (paint and eraser) that
// strokes use. Owned by CanvasItem (canvas.brushes in QML).
//
// Editing a setting changes the working copy of the selected preset for this session only; the
// preset itself changes only on saveCurrent(). The temporary size is never what makes a preset
// "modified", and it is never saved without an explicit save. Built-in presets can be saved over
// (stored as changes) and reset to their original definition; custom presets can also be renamed
// and deleted. Selections, favourites and saved presets persist in the store file.
class BrushLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use canvas.brushes")
    // Every preset, built-in first: {id, name, builtIn, favorite, modified, savedChanges, blend,
    // thumbnail, current, inUse}.
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    // The preset categories in display order (BrushPresets::categories).
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    // The current working settings as BrushSettings::toJson() (nested maps for pressure).
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(QString currentThumbnail READ currentThumbnail NOTIFY currentChanged)
    Q_PROPERTY(bool currentBuiltIn READ currentBuiltIn NOTIFY currentChanged)
    Q_PROPERTY(bool currentModified READ currentModified NOTIFY currentChanged)
    Q_PROPERTY(bool currentSizeChanged READ currentSizeChanged NOTIFY currentChanged)
    Q_PROPERTY(bool currentHasSavedChanges READ currentHasSavedChanges NOTIFY currentChanged)
    Q_PROPERTY(bool currentFavorite READ currentFavorite NOTIFY presetsChanged)
    // Problems reading or writing the store file, for the user; empty when all is well.
    Q_PROPERTY(QString storeStatus READ storeStatus NOTIFY storeStatusChanged)
    // The Brush Studio, which edits one preset on a copy.
    Q_PROPERTY(BrushStudio *studio READ studio CONSTANT)

public:
    explicit BrushLibrary(QObject *parent = nullptr);
    ~BrushLibrary() override;

    // Loads the library from path and saves changes there from now on. Without a path nothing is
    // persisted (tests, previews).
    void setStorePath(const QString &path);
    QString storePath() const { return m_storePath; }

    // Which working brush the controls edit: the eraser's (true) or the paint brush's.
    void setEraserSlot(bool eraser);
    bool eraserSlot() const { return m_eraserSlot; }
    // The working settings a new stroke uses.
    BrushSettings settings(bool eraser) const;
    QString selectedId(bool eraser) const { return eraser ? m_eraserId : m_paintId; }

    QVariantList presets() const;
    QStringList categories() const { return BrushPresets::categories(); }
    QString currentId() const { return selectedId(m_eraserSlot); }
    QString currentName() const;
    QVariantMap current() const;
    QString currentThumbnail() const { return thumbnailUrl(currentId()); }
    bool currentBuiltIn() const;
    bool currentModified() const;
    bool currentSizeChanged() const;
    bool currentHasSavedChanges() const;
    bool currentFavorite() const { return m_data.favorites.contains(currentId()); }
    QString storeStatus() const { return m_storeStatus; }
    BrushStudio *studio() { return m_studio; }

    // Selects a preset for its kind: erasers become the eraser brush, all others the paint brush.
    Q_INVOKABLE bool select(const QString &id);
    // Changes one setting of the current working brush by its BrushSettings JSON key; nested
    // pressure keys as "pressureSize.minimum". Values are validated and clamped. The blend mode
    // and name cannot be changed this way.
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void toggleFavorite(const QString &id);
    // Whether a setting (BrushSettings JSON key; "pressureSize.minimum" is pressureSize) does
    // anything with the current working brush's engine (the registry, BrushProperties::appliesTo),
    // so the panel shows only controls that work. `dependency` is ignored: QML passes the current
    // settings so the binding re-evaluates when they change.
    Q_INVOKABLE bool applies(const QString &key, const QVariant &dependency = {}) const;

    Q_INVOKABLE void saveCurrent();
    // A new custom preset from the current working settings; selected afterwards. Returns its id.
    Q_INVOKABLE QString saveAsNew(const QString &name);
    // Back to the preset as saved; a built-in also loses its saved changes.
    Q_INVOKABLE void resetCurrent();
    Q_INVOKABLE void renameCurrent(const QString &name);
    Q_INVOKABLE bool deleteCurrent();
    // Writes pending changes now (also done shortly after each change and on destruction).
    Q_INVOKABLE void flush();

    // Settings without session edits.
    BrushSettings savedSettings(const QString &id) const;
    BrushSettings workingSettings(const QString &id) const;
    bool hasPreset(const QString &id) const;
    // A new custom preset with these settings, named name, selected for its kind. Returns its id,
    // or "" when the library is full (storeStatus says so).
    QString addPreset(const BrushSettings &settings, const QString &name);
    // Replaces a preset's settings (saved and working), as the Brush Studio's Done does.
    void storePreset(const QString &id, const BrushSettings &settings);
    // One setting by registry id (or "pressureX.field") with the type rules of setValue; false when
    // the key or the kind of value is not accepted. Not sanitised.
    static bool applyValue(BrushSettings *settings, const QString &key, const QVariant &value);

signals:
    void presetsChanged();
    void currentChanged();
    void storeStatusChanged();
    // A preset of the other kind was selected: the canvas switches tool (true = eraser).
    void toolRequested(bool eraser);

private:
    void setWorking(const QString &id, const BrushSettings &settings);
    void scheduleSave();
    void setStoreStatus(const QString &status);
    QString thumbnailUrl(const QString &id) const;
    void publishThumbnail(const QString &id);
    QString newCustomId() const;
    BrushPreset *customPreset(const QString &id);
    const BrushPreset *customPreset(const QString &id) const;

    BrushPresetStore::Data m_data;
    std::map<QString, BrushSettings> m_edits; // session edits, per preset
    std::map<QString, int> m_thumbRevision;
    QString m_paintId;
    QString m_eraserId;
    bool m_eraserSlot = false;
    QString m_storePath;
    bool m_storeWritable = false; // false without a path or after refusing an unusable file
    QString m_storeStatus;
    QTimer m_saveTimer;
    BrushStudio *m_studio; // child
};

#pragma once

#include "BrushPresets.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <map>
#include <vector>

// The artist's brush library on disk: custom presets, saved changes to built-in presets,
// favourites and the selected paint and eraser presets. One small JSON file, written atomically
// (ProjectFile::writeFileSafely), with a format name and version:
//
//   {"format": "drawapp-brushes", "version": 1,
//    "custom":    [{"id": "custom.3", "settings": {...BrushSettings::toJson()...}}, ...],
//    "overrides": {"builtin.clean-ink": {...}},
//    "favorites": ["builtin.clean-ink", "custom.3"],
//    "selected":  {"paint": "builtin.solid-round", "eraser": "builtin.hard-eraser"}}
//
// Reading validates everything: a file that is not a brush library, or from a newer version, is
// refused as a whole (and must then not be overwritten); a single invalid preset or reference is
// skipped with a warning, so one bad entry does not cost the rest.
namespace BrushPresetStore {

constexpr int FormatVersion = 1;
constexpr int MaxCustomPresets = 500;
constexpr qint64 MaxFileBytes = 4 * 1024 * 1024;

struct Data
{
    std::vector<BrushPreset> custom;
    std::map<QString, BrushSettings> overrides; // built-in id -> saved settings
    QStringList favorites;
    QString paintId;
    QString eraserId;
};

QByteArray encode(const Data &data);
// Returns false with *error when the bytes are not a usable brush library at all.
bool decode(const QByteArray &bytes, Data *out, QString *error, QStringList *warnings);

// An absent file is an empty library (true, no warnings). Returns false with *error if the file
// exists but cannot be used; the caller must then leave it alone.
bool load(const QString &path, Data *out, QString *error, QStringList *warnings);
// Empty on success, otherwise a user-facing message.
QString save(const QString &path, const Data &data);

} // namespace BrushPresetStore

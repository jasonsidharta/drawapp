#pragma once

#include "BrushSettings.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// One brush preset as a file to share (".drawbrush"), documented in docs/brush-preset-format.md:
//
//   {"format": "drawapp-brush", "version": 2,
//    "metadata": {"app": "DrawApp", "settingsVersion": 2},
//    "settings": {...BrushSettings::toJson()...}}
//
// UTF-8 JSON. Imported tips are embedded in the settings, so a file never refers to an asset
// outside itself; an unknown tip or texture id (from a newer version) is kept and drawn as the
// round tip / no texture, with a warning. Keys this version does not know are kept.
//
// This is DrawApp's own format. Krita (.kpp) and Procreate (.brush, .brushset) presets cannot be
// imported (see docs/brush-preset-format.md, "Other formats").
namespace BrushPresetFile {

constexpr int FormatVersion = 2;
constexpr qint64 MaxFileBytes = 2 * 1024 * 1024;
constexpr const char *Suffix = "drawbrush";

QByteArray encode(const BrushSettings &settings);
// false with *error when the bytes are not a usable preset; recoverable problems go to *warnings.
bool decode(const QByteArray &bytes, BrushSettings *out, QString *error, QStringList *warnings);

} // namespace BrushPresetFile

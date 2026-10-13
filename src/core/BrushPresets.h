#pragma once

#include "BrushSettings.h"

#include <QString>
#include <QStringList>
#include <vector>

// A named brush the artist picks from. Built-in presets are defined in code (BrushPresets.cpp);
// custom presets are stored by BrushPresetStore. Ids are stable: built-ins use "builtin.*",
// custom presets "custom.*".
struct BrushPreset
{
    QString id;
    BrushSettings settings; // settings.name is the display name
    bool builtIn = false;
};

namespace BrushPresets {

// The original presets, in display order. All of them use the one StrokeEngine renderer.
const std::vector<BrushPreset> &builtIns();
// nullptr for an unknown id.
const BrushPreset *builtIn(const QString &id);

// The group a preset is listed under, from what its settings do (so custom presets group too):
// Basic, Erasers, Ink, Dry Media, Bristles, Blending, Watercolour, Nature & Patterns, Effects.
QString category(const BrushSettings &settings);
// Every category, in display order.
QStringList categories();

// What a new installation starts with.
QString defaultPaintId();  // Solid Round: the milestone 5 brush (12 px, hard, uniform)
QString defaultEraserId(); // Hard Eraser

} // namespace BrushPresets

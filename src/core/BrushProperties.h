#pragma once

#include "BrushSettings.h"

#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>
#include <vector>

// The brush property registry (research/milestone9-architecture-decisions.md, D4): every brush
// property declared once, with what the settings specification, the Brush Studio, serialisation,
// validation and comparison need. docs/brush-settings-specification.md is generated from it:
// tst_brushengine::specificationIsCurrent writes it with DRAWAPP_WRITE_SPEC=1 and otherwise checks
// that the committed copy is current.
//
// The twelve design groups hold the 120-property design target. A property is "implemented"
// only when it changes rendering: tst_brushengine::everyImplementedPropertyChangesRendering
// renders a standard stroke with the default and a changed value for each one and requires
// different pixels. Planned properties are listed with implemented = false, no accessors and no controls.
//
// Values travel as QVariant in their JSON form: numbers as double, integers as int, booleans,
// option names as strings, an image as a base64 PNG string, pressure responses and dynamics as
// maps (QVariantMap of the JSON object).

enum class PropertyType { Number, Integer, Bool, Choice, Text, Image, Pressure, Dynamics };

// Which engines a property affects: blend × stamp, as bit flags.
enum EngineFlag : unsigned {
    EnginePaint = 1,    // Paint blend, tip stamp
    EngineErase = 2,    // Erase blend, tip stamp
    EngineSmudge = 4,
    EngineWet = 8,
    EngineSpray = 16,   // Paint or Erase with the spray stamp
    EngineHatching = 32, // Paint or Erase with the hatching stamp
    EngineDry = 64,      // Paint or Erase with the dry-media stamp (milestone 11)
    EngineBristle = 128, // Paint or Erase with the bristle stamp (milestone 11)
    EngineCoverage = EnginePaint | EngineErase | EngineSpray | EngineHatching | EngineDry | EngineBristle,
    EngineCarried = EngineSmudge | EngineWet,
    EngineAll = 255,
};

struct BrushProperty
{
    QString id;          // also the JSON key; "dynamics.size" is "size" inside "dynamics"
    QString group;
    QString name;
    QString description;
    PropertyType type = PropertyType::Number;
    double minimum = 0;  // Number, Integer
    double maximum = 1;
    double displayScale = 1; // shown value = value × displayScale (100 for percentages)
    QString unit;            // of the shown value: "px", "%", "°", ...
    QStringList choices;      // Choice: serialised names
    QStringList choiceLabels; // Choice: names shown to the artist
    unsigned engines = EngineAll;
    QString sensors;   // sensors it accepts ("all sensors", "pressure", "tilt", "—")
    QString rendering; // what it does to the pixels
    QString cost;      // computational implications
    QString tests;     // tests that check it
    QString requires;  // a condition for it to have an effect, for the artist ("" if none)
    bool implemented = false;
    bool advanced = true; // false: also in the quick brush panel
    // The milestone that added it. Properties from milestone 11 on are written to preset files only
    // when they differ from their default, so presets that do not use them stay byte-for-byte what
    // milestones 9 and 10 wrote (and older readers see no new keys).
    int since = 9;
    std::function<QVariant(const BrushSettings &)> get;
    std::function<void(BrushSettings &, const QVariant &)> set; // sanitising is up to the caller

    QVariant defaultValue() const;
};

namespace BrushProperties {

// The groups in order: the twelve milestone 9 design groups (the 120-property design target), the
// milestone 11 engine groups (properties of the new engines, counted separately so the original
// target keeps its meaning), then "Engine" (the blend and stamp selectors, counted in neither).
QStringList groups();
QStringList designGroups();
QStringList engineGroups();
const std::vector<BrushProperty> &all();
const BrushProperty *find(const QString &id);
int implementedCount();     // implemented, in every group but "Engine"
int designTargetCount();    // properties in the twelve design groups (120)
int engineGroupCount();     // properties in the milestone 11 engine groups

// The engine flag of these settings (one bit).
unsigned engineOf(const BrushSettings &settings);
bool appliesTo(const BrushProperty &property, const BrushSettings &settings);

// The settings specification (Markdown), generated from the registry.
QString specificationMarkdown();

// Image values: base64 PNG of a Grayscale8 image; a null image is "".
QString encodeImage(const QImage &image);
QImage decodeImage(const QString &text, bool *ok);
// A user image as a tip: square, Grayscale8, at most MaxTipImageSize; alpha when the image has
// transparency, otherwise darkness (black paints). Null when the image is unusable.
QImage tipFromImage(const QImage &image);

} // namespace BrushProperties

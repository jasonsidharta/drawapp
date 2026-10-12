#pragma once

#include <QJsonObject>
#include <QString>

// What a brush does to the layer.
enum class BrushBlend { Paint, Erase, Smudge };

// How the dabs of one stroke combine (see StrokeEngine):
//   Uniform: a pixel's stroke coverage is the largest any dab gives it, so overlaps and
//            self-crossings never darken; the stroke reaches its opacity in one pass.
//   BuildUp: each dab adds a share of the remaining distance to the opacity ceiling, so slow or
//            repeated passes within the stroke darken gradually, never past the ceiling.
enum class Accumulation { Uniform, BuildUp };

// How the texture modulates coverage: Multiply scales it; Subtract removes coverage in the
// texture's valleys first, so light marks break up into grain while heavy ones fill it (pencil
// tooth).
enum class TextureMode { Multiply, Subtract };

// A pressure response: value = minimum + (1 − minimum) × pressure^gamma. Disabled = 1.
// Input without pressure (mouse, touch) arrives as pressure 1, so it always gives 1.
struct PressureMapping
{
    bool enabled = false;
    double minimum = 0.0;
    double gamma = 1.0; // < 1 soft, > 1 firm (0.33 .. 3)

    double apply(double pressure) const;
    bool operator==(const PressureMapping &other) const;
    bool operator!=(const PressureMapping &other) const { return !(*this == other); }
};

// Brush settings and preset data: plain values, no colour (the colour is chosen separately) and no
// stroke state. Units: sizes in document pixels, spacing as a fraction of the dab diameter,
// percentages as 0..1 fractions, angles in degrees, rates in dabs per second.
struct BrushSettings
{
    static constexpr int FormatVersion = 1;
    static constexpr double MinSize = 1.0;
    static constexpr double MaxSize = 512.0;

    QString name;
    BrushBlend blend = BrushBlend::Paint;

    // Tip: computed round (hardness, roundness) or one of our generated grayscale tips.
    QString tip;              // empty = round; otherwise a BrushTips id
    double size = 12.0;
    double hardness = 1.0;    // round tips: 1 = crisp anti-aliased edge, 0 = smooth falloff to the rim
    double roundness = 1.0;   // 1 = circle
    double angle = 0.0;
    bool followDirection = false; // the tip turns with the stroke (or with pen tilt, when reported)
    double angleJitter = 0.0;     // ± degrees, deterministic per stroke
    double spacing = 0.15;

    // Deposition. Opacity is the ceiling the stroke can reach; flow is how much one pass deposits
    // (BuildUp only; Uniform deposits the ceiling at once). They never multiply twice: see
    // StrokeEngine.
    double opacity = 1.0;
    double flow = 1.0;
    Accumulation accumulation = Accumulation::Uniform;

    PressureMapping pressureSize{true, 0.1, 1.0};
    PressureMapping pressureOpacity;
    PressureMapping pressureFlow;

    // Pen tilt, used only when the device reports it: at full tilt (60° or more) the size is
    // multiplied by tiltSize and the ceiling by tiltOpacity. Without tilt data both stay 1.
    double tiltSize = 1.0;
    double tiltOpacity = 1.0;

    // Texture in document space (coherent along and across strokes), applied to the stroke.
    QString texture;           // empty = none; otherwise a BrushTips texture id
    double textureScale = 1.0;
    double textureStrength = 0.0;
    TextureMode textureMode = TextureMode::Multiply;

    // Airbrush: while the pen is down, dabs also come with elapsed time (not with input events),
    // so holding still keeps building up (BuildUp accumulation).
    bool airbrush = false;
    double airbrushRate = 30.0;

    // Smudge: how strongly the carried colour is laid down, and how fast it takes on new colour.
    double smudgeStrength = 0.6;
    double smudgePickup = 0.25;

    int smoothing = 0; // 0..100, see StrokeSmoother

    bool operator==(const BrushSettings &other) const;
    bool operator!=(const BrushSettings &other) const { return !(*this == other); }

    // Clamps every value into its valid range (and NaN to the default).
    BrushSettings sanitized() const;

    QJsonObject toJson() const;
    // Reads settings written by toJson (unknown keys are ignored, missing keys keep defaults,
    // out-of-range values are clamped). Returns false with *error for a value of the wrong type
    // or an unknown enum name.
    static bool fromJson(const QJsonObject &json, BrushSettings *out, QString *error);
};

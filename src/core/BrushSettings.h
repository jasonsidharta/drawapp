#pragma once

#include "BrushDynamics.h"

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// What a brush does to the layer.
//   Paint, Erase: stroke coverage composited with the colour (or removing alpha)
//   Smudge:       a carried patch of canvas paint is dragged along
//   WetPaint:     a carried patch that starts charged with the brush colour, deposits it and
//                 picks up canvas paint as it goes (a bounded prototype, see StrokeEngine)
enum class BrushBlend { Paint, Erase, Smudge, WetPaint };

// What one dab is (Paint and Erase only; the other blends always use the tip).
//   Tip:      the tip image or the computed round tip
//   Spray:    many small round particles scattered over the dab's disc
//   Hatching: parallel lines fixed in document space, inside the tip
//   Dry:      dry media (milestone 11): the tip deposits only where the pressure-dependent
//             penetration reaches into the paper's tooth (graphite, charcoal, chalk, pastel)
enum class BrushStamp { Tip, Spray, Hatching, Dry };

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

// How painted colour combines with the layer (Paint only).
enum class PaintMode { Normal, Multiply, Screen, Behind, PreserveAlpha };

// Smudge: drag the carried patch pixel by pixel (Smear), or carry one averaged colour (Dulling).
enum class SmudgeMode { Smear, Dulling };

// A pressure response: value = minimum + (1 − minimum) × pressure^gamma. Disabled = 1.
// Input without pressure (mouse, touch) gives 1.
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
//
// Every field except name and extra is declared in the property registry (BrushProperties), which
// drives serialisation, validation, comparison, the Brush Studio and the settings specification.
struct BrushSettings
{
    // 1: milestone 6. 2: milestone 9 (engines, dynamics and the properties below); a version 1
    // preset reads as version 2 with the new properties at their defaults.
    static constexpr int FormatVersion = 2;
    static constexpr double MinSize = 1.0;
    static constexpr double MaxSize = 512.0;
    static constexpr int MaxTipImageSize = 256;

    QString name;
    BrushBlend blend = BrushBlend::Paint;
    BrushStamp stamp = BrushStamp::Tip;

    // --- Brush tip ---
    QString tip;              // empty = round; otherwise a BrushTips id (unknown ids draw round)
    QImage tipImage;          // an imported tip (Grayscale8, square); overrides tip when set
    double size = 12.0;
    double hardness = 1.0;    // round tips: 1 = crisp anti-aliased edge, 0 = smooth falloff to the rim
    double roundness = 1.0;   // 1 = circle
    double angle = 0.0;
    bool flipX = false;
    bool flipY = false;
    double minimumSize = 1.0; // smallest dab diameter after every size change, px
    double tipDensity = 1.0;  // share of tip pixels each dab paints (random per dab)

    // --- Stroke path ---
    double spacing = 0.15;
    double spacingJitter = 0.0; // ± share of the spacing, random per dab
    double scatter = 0.0;       // offset across the stroke, up to this many diameters
    bool scatterBothAxes = false;
    int count = 1;              // dabs per spacing step
    double countJitter = 0.0;   // share of count that may be dropped per step
    // Airbrush: while the pen is down, dabs also come with elapsed time (not with input events),
    // so holding still keeps building up (BuildUp accumulation).
    bool airbrush = false;
    double airbrushRate = 30.0;

    // --- Pressure and tilt ---
    PressureMapping pressureSize{true, 0.1, 1.0};
    PressureMapping pressureOpacity;
    PressureMapping pressureFlow;
    // Pen tilt, used only when the device reports it: at full tilt the size is multiplied by
    // tiltSize and the ceiling by tiltOpacity. Without tilt data both stay 1.
    double tiltSize = 1.0;
    double tiltOpacity = 1.0;
    double fullTiltAngle = 60.0;        // tilt from vertical that counts as full tilt, degrees
    double tiltAzimuthThreshold = 15.0; // below this tilt the azimuth does not turn the tip

    // --- Opacity and flow ---
    // Opacity is the ceiling the stroke can reach; flow is how much one pass deposits
    // (BuildUp only; Uniform deposits the ceiling at once). They never multiply twice: see
    // StrokeEngine.
    double opacity = 1.0;
    double flow = 1.0;
    Accumulation accumulation = Accumulation::Uniform;
    double opacityJitter = 0.0;
    double flowJitter = 0.0;

    // --- Texture and grain --- (document space unless textureMoving)
    QString texture;           // empty = none; otherwise a BrushTips texture id
    double textureScale = 1.0;
    double textureStrength = 0.0;
    TextureMode textureMode = TextureMode::Multiply;
    double textureRotation = 0.0;   // degrees
    double textureBrightness = 0.0; // −1..1
    double textureContrast = 1.0;   // 0..3
    bool textureInvert = false;
    bool textureEachDab = false;    // texture each dab (overlaps fill the grain) instead of the stroke
    bool textureMoving = false;     // the grain moves and turns with each dab (each-dab only)
    bool textureRandomOffset = false; // a new grain position for every stroke

    // --- Shape dynamics ---
    bool followDirection = false; // the tip turns with the stroke (or with pen tilt, when reported)
    double angleJitter = 0.0;     // ± degrees, deterministic per stroke
    double sizeJitter = 0.0;      // share of the size that may be lost, per dab
    double strokeSizeJitter = 0.0; // the same, once per stroke
    double roundnessJitter = 0.0;
    bool randomFlipX = false;
    bool randomFlipY = false;

    // --- Colour dynamics --- (degrees for hue, 0..1 for saturation and lightness)
    double hueJitter = 0.0;
    double saturationJitter = 0.0;
    double lightnessJitter = 0.0;
    double strokeHueJitter = 0.0;
    double strokeSaturationJitter = 0.0;
    double strokeLightnessJitter = 0.0;

    // --- Blending and smudge ---
    PaintMode paintMode = PaintMode::Normal;
    // Smudge: how strongly the carried colour is laid down, and how fast it takes on new colour.
    double smudgeStrength = 0.6;
    double smudgePickup = 0.25;
    SmudgeMode smudgeMode = SmudgeMode::Smear;
    double smudgeColorRate = 0.0; // brush colour mixed into the carried paint per pass
    bool smudgePreserveAlpha = false;

    // --- Wet paint ---
    double wetLoad = 1.0;         // how much brush colour the patch holds at the start
    double wetLoadLength = 600.0; // px of stroke until the load runs out; 0 = never
    double wetAttack = 0.6;       // how much of the carried paint each pass deposits
    double wetPull = 0.3;         // how much canvas paint each pass picks up
    double wetDilution = 0.0;     // thins the brush paint (lower alpha), 0..1
    double wetnessJitter = 0.0;   // random dilution per dab

    // --- Stabilisation ---
    int smoothing = 0; // 0..100, see StrokeSmoother
    double pressureSmoothing = 0.0; // 0..1
    double jitterFilter = 0.0;      // px: input moves shorter than this are held back
    bool smoothingFinish = true;    // pen up draws the smoothing string's tail to the pen

    // --- Ink and stroke geometry --- (milestone 11; research/m11-brush-architecture.md, A4)
    bool curveInterpolation = false; // centripetal Catmull–Rom between input samples (one sample of latency)
    double cornerAngle = 70.0;       // degrees: sharper turns stay corners
    bool taperLive = false;          // end taper without holding the line back (the tail is redrawn at pen-up)
    double smoothingSpeedAdapt = 0.0; // 0..1: the smoothing string shortens at speed

    // --- Taper --- (lengths in px; sizes as a share of the size)
    double taperStart = 0.0;
    double taperEnd = 0.0;
    double taperStartSize = 0.1;
    double taperEndSize = 0.1;
    double taperOpacity = 0.0; // how much the taper also fades the opacity
    double taperShape = 1.0;   // exponent: < 1 blunt, > 1 sharp

    // --- Special effects ---
    double sprayDensity = 30.0;    // particles per dab
    double sprayParticleSize = 2.0; // px
    double sprayParticleJitter = 0.0;
    double hatchAngle = 45.0;      // degrees
    double hatchSpacing = 6.0;     // px between line centres
    double hatchThickness = 1.5;   // px

    // --- Dry media --- (stamp Dry; research/m11-brush-architecture.md, A3)
    // The paper is the texture (document space; "paper" when none is chosen) and its strength is
    // the tooth: 0 = smooth paper. A dab pixel deposits tip × smoothstep(depth − (1 − height)),
    // so light pressure catches the peaks and heavy pressure fills the valleys.
    double dryDepth = 0.8;                        // penetration at full pressure, 0..1
    PressureMapping pressureDepth{true, 0.15, 1.0}; // pressure → penetration
    double drySoftness = 0.12;                    // width of the touched/untouched transition
    double dryStreaks = 0.0;                      // grain running along the stroke (charcoal, pastel)
    double dryStreakLength = 6.0;                 // how elongated the streaks are (1 = round)
    double dryStreakScale = 1.0;                  // size of the streak grain
    double dryEdgeBreakup = 0.0;                  // the mark's rim crumbles
    double dryGrit = 0.0;                         // share of pixels the pigment skips (per stroke)
    double dryTiltDepth = 1.0;                    // penetration × this at full tilt (side shading)

    // --- Dynamics: parameters following sensors (BrushDynamics.h) ---
    DynamicsSet dynamics;

    // Keys this version does not know, kept so a newer preset survives a round trip.
    QJsonObject extra;

    ParameterDynamics &dynamicsFor(DynamicTarget t) { return dynamics[size_t(t)]; }
    const ParameterDynamics &dynamicsFor(DynamicTarget t) const { return dynamics[size_t(t)]; }
    bool hasColorDynamics() const;

    bool operator==(const BrushSettings &other) const;
    bool operator!=(const BrushSettings &other) const { return !(*this == other); }

    // Clamps every value into its valid range (and NaN to the default).
    BrushSettings sanitized() const;

    QJsonObject toJson() const;
    // Reads settings written by toJson (missing keys keep defaults, out-of-range values are
    // clamped, unknown keys are kept in extra). Returns false with *error for a value of the
    // wrong type or an unknown option name. Recoverable problems (an unreadable tip image, an
    // unknown tip or texture id) only add to *warnings.
    static bool fromJson(const QJsonObject &json, BrushSettings *out, QString *error, QStringList *warnings = nullptr);
};

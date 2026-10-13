#include "BrushProperties.h"

#include "BrushTips.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {

using Getter = std::function<QVariant(const BrushSettings &)>;
using Setter = std::function<void(BrushSettings &, const QVariant &)>;
struct Access
{
    Getter get;
    Setter set;
};

Access number(double BrushSettings::*m)
{
    return {[m](const BrushSettings &s) { return QVariant(s.*m); },
            [m](BrushSettings &s, const QVariant &v) { s.*m = v.toDouble(); }};
}

Access integer(int BrushSettings::*m)
{
    return {[m](const BrushSettings &s) { return QVariant(s.*m); },
            [m](BrushSettings &s, const QVariant &v) {
                const double d = v.toDouble();
                s.*m = std::isfinite(d) ? int(std::lround(std::clamp(d, -1e6, 1e6))) : 0;
            }};
}

Access flag(bool BrushSettings::*m)
{
    return {[m](const BrushSettings &s) { return QVariant(s.*m); },
            [m](BrushSettings &s, const QVariant &v) { s.*m = v.toBool(); }};
}

Access text(QString BrushSettings::*m)
{
    return {[m](const BrushSettings &s) { return QVariant(s.*m); },
            [m](BrushSettings &s, const QVariant &v) { s.*m = v.toString(); }};
}

template <typename E>
Access choice(E BrushSettings::*m, const QStringList &names)
{
    return {[m, names](const BrushSettings &s) { return QVariant(names.value(int(s.*m))); },
            [m, names](BrushSettings &s, const QVariant &v) {
                const int i = names.indexOf(v.toString());
                if (i >= 0)
                    s.*m = E(i);
            }};
}

Access pressure(PressureMapping BrushSettings::*m)
{
    return {[m](const BrushSettings &s) {
                const PressureMapping &p = s.*m;
                return QVariant(QVariantMap{{QStringLiteral("enabled"), p.enabled},
                                            {QStringLiteral("minimum"), p.minimum},
                                            {QStringLiteral("gamma"), p.gamma}});
            },
            [m](BrushSettings &s, const QVariant &v) {
                const QVariantMap map = v.toMap();
                PressureMapping &p = s.*m;
                if (map.contains(QStringLiteral("enabled")))
                    p.enabled = map.value(QStringLiteral("enabled")).toBool();
                if (map.contains(QStringLiteral("minimum")))
                    p.minimum = map.value(QStringLiteral("minimum")).toDouble();
                if (map.contains(QStringLiteral("gamma")))
                    p.gamma = map.value(QStringLiteral("gamma")).toDouble();
            }};
}

Access dynamicsOf(DynamicTarget t)
{
    return {[t](const BrushSettings &s) { return QVariant(s.dynamicsFor(t).toJson().toVariantMap()); },
            [t](BrushSettings &s, const QVariant &v) {
                ParameterDynamics d;
                if (ParameterDynamics::fromJson(QJsonObject::fromVariantMap(v.toMap()), &d, nullptr))
                    s.dynamicsFor(t) = d;
            }};
}

Access tipImageAccess()
{
    return {[](const BrushSettings &s) { return QVariant(BrushProperties::encodeImage(s.tipImage)); },
            [](BrushSettings &s, const QVariant &v) {
                if (v.typeId() == QMetaType::QImage) {
                    s.tipImage = BrushProperties::tipFromImage(v.value<QImage>());
                    return;
                }
                // A stored tip (base64 PNG) is already a tip: taken as it is, not converted again.
                bool ok = false;
                const QImage image = BrushProperties::decodeImage(v.toString(), &ok);
                if (ok)
                    s.tipImage = image.isNull() ? QImage() : image.convertToFormat(QImage::Format_Grayscale8);
            }};
}

// Fluent construction of one entry.
struct Entry
{
    BrushProperty p;

    Entry(const QString &group, const char *id, const char *name, PropertyType type)
    {
        p.group = group;
        p.id = QLatin1String(id);
        p.name = QString::fromUtf8(name);
        p.type = type;
        if (type == PropertyType::Bool) {
            p.minimum = 0;
            p.maximum = 1;
        }
    }
    Entry &range(double lo, double hi, double scale = 1, const char *unit = "")
    {
        p.minimum = lo;
        p.maximum = hi;
        p.displayScale = scale;
        p.unit = QString::fromUtf8(unit);
        return *this;
    }
    Entry &percent(double lo = 0, double hi = 1) { return range(lo, hi, 100, "%"); }
    Entry &options(const QStringList &names, const QStringList &labels)
    {
        p.choices = names;
        p.choiceLabels = labels;
        return *this;
    }
    Entry &engines(unsigned e)
    {
        p.engines = e;
        return *this;
    }
    Entry &sensors(const char *s)
    {
        p.sensors = QString::fromUtf8(s);
        return *this;
    }
    Entry &desc(const char *s)
    {
        p.description = QString::fromUtf8(s);
        return *this;
    }
    Entry &render(const char *s)
    {
        p.rendering = QString::fromUtf8(s);
        return *this;
    }
    Entry &cost(const char *s)
    {
        p.cost = QString::fromUtf8(s);
        return *this;
    }
    Entry &tests(const char *s)
    {
        p.tests = QString::fromUtf8(s);
        return *this;
    }
    Entry &needs(const char *s)
    {
        p.requires = QString::fromUtf8(s);
        return *this;
    }
    Entry &basic()
    {
        p.advanced = false;
        return *this;
    }
    Entry &since(int milestone)
    {
        p.since = milestone;
        return *this;
    }
    Entry &access(const Access &a)
    {
        p.get = a.get;
        p.set = a.set;
        p.implemented = true;
        return *this;
    }
};

const char *const EffectTest = "tst_brushengine::everyImplementedPropertyChangesRendering";

std::vector<BrushProperty> build()
{
    std::vector<Entry> e;
    QString g;
    auto add = [&](const char *id, const char *name, PropertyType type) -> Entry & {
        e.emplace_back(g, id, name, type);
        Entry &x = e.back();
        x.p.sensors = QStringLiteral("—");
        if (type == PropertyType::Dynamics)
            x.p.sensors = QStringLiteral("all sensors");
        return x;
    };
    auto dyn = [&](DynamicTarget t, const char *name, unsigned engines, const char *render, const char *needs = "") {
        add(qPrintable(QStringLiteral("dynamics.") + DynamicTargets::id(t)), name, PropertyType::Dynamics)
            .engines(engines)
            .desc("Makes the parameter follow sensors (pressure, tilt, direction, speed, distance, time, random …) through "
                  "response curves; several sensors combine by the chosen rule. Off by default.")
            .render(render)
            .cost("Per dab: one table look-up per sensor (256-entry curve tables built once per stroke).")
            .tests("tst_brushengine::dynamics*, tst_brushengine::everyImplementedPropertyChangesRendering")
            .needs(needs)
            .access(dynamicsOf(t));
    };

    QStringList tipNames{QString()}, tipLabels{QStringLiteral("Round")};
    for (const QString &id : BrushTips::tipIds()) {
        tipNames << id;
        tipLabels << BrushTips::label(id);
    }
    QStringList textureNames{QString()}, textureLabels{QStringLiteral("None")};
    for (const QString &id : BrushTips::textureIds()) {
        textureNames << id;
        textureLabels << BrushTips::label(id);
    }
    const unsigned TipEngines = EngineAll & ~EngineSpray;

    // ----------------------------------------------------------------------------------- Engine
    g = QStringLiteral("Engine");
    add("blend", "Engine", PropertyType::Choice)
        .options({"paint", "erase", "smudge", "wet", "watercolor"}, {"Paint", "Erase", "Smudge", "Wet paint", "Watercolour"})
        .desc("What the brush does to the layer: paint colour, erase, smudge existing paint, or wet paint (carries and "
              "mixes paint).")
        .render("Selects the renderer: coverage composite (paint, erase) or carried patch (smudge, wet paint).")
        .cost("Smudge and wet paint touch pixels per dab; paint and erase recomposite each pixel once per input event.")
        .tests("tst_brush, tst_brushengine (engine suites), tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(choice(&BrushSettings::blend, {"paint", "erase", "smudge", "wet", "watercolor"}));
    add("stamp", "Dab type", PropertyType::Choice)
        .options({"tip", "spray", "hatching", "dry", "bristle", "strands", "grass", "foliage", "splatter", "pattern"},
                 {"Tip", "Spray", "Hatching", "Dry media", "Bristles", "Strands (hair, fur)", "Grass", "Foliage",
                  "Splatter", "Pattern"})
        .engines(EngineCoverage)
        .desc("What one dab of a paint or erase brush is: the tip, a spray of particles, hatching lines, dry media "
              "(graphite, charcoal, chalk, pastel) that catches in the paper's tooth, bristles, or procedural geometry: "
              "hair and fur strands, grass blades, leaves, splatter drops or a repeated pattern.")
        .render("Spray scatters round particles over the dab's disc; hatching draws parallel document-space lines inside "
                "the tip; dry media deposits the tip only where the pressure-dependent penetration reaches into the "
                "paper (the texture, in document space).")
        .cost("Spray: per particle O(particle area). Hatching: as the tip plus one distance per pixel. Dry media: as "
              "the tip plus one or two texture samples per pixel.")
        .tests("tst_brushengine::sprayAndHatching, tst_brushmedia (dry media)")
        .needs("Paint or erase engine")
        .access(choice(&BrushSettings::stamp,
                       {"tip", "spray", "hatching", "dry", "bristle", "strands", "grass", "foliage", "splatter", "pattern"}));

    // -------------------------------------------------------------------------------- Brush tip
    g = QStringLiteral("Brush Tip");
    add("tip", "Tip shape", PropertyType::Choice)
        .options(tipNames, tipLabels)
        .engines(TipEngines & ~EngineBristle & ~EngineProcedural)
        .basic()
        .desc("The computed round tip or a generated tip image. An unknown id (from a newer version) draws round and is "
              "kept.")
        .render("Round: analytic disc with hardness falloff. Image tips: bilinear sample of the nearest larger mip level.")
        .cost("Per pixel of each dab: one analytic evaluation or one bilinear sample.")
        .tests("tst_brush::generatedTipsAndTextures, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(text(&BrushSettings::tip));
    add("tipImage", "Imported tip", PropertyType::Image)
        .engines(TipEngines & ~EngineBristle & ~EngineProcedural)
        .desc("A tip made from an imported image (alpha, or darkness for opaque images), stored inside the preset so it "
              "can never go missing. Overrides the tip shape.")
        .render("Like the generated image tips; mip levels and the flow pass length are built once per stroke.")
        .cost("Per stroke: mip chain of at most 256 × 256 (well under 1 ms). Per pixel: one bilinear sample.")
        .tests("tst_brushengine::importedTipIsUsedAndSaved, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(tipImageAccess());
    add("size", "Size", PropertyType::Number)
        .range(BrushSettings::MinSize, BrushSettings::MaxSize, 1, "px")
        .basic()
        .desc("Dab diameter at full pressure, in document pixels.")
        .render("Every size change (pressure, tilt, dynamics, jitter, taper) multiplies this diameter.")
        .cost("Dab cost grows with the square of the size.")
        .tests("tst_brush::pressureRampTapersSize, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::size));
    add("hardness", "Hardness", PropertyType::Number)
        .percent()
        .engines(TipEngines & ~EngineBristle & ~EngineProcedural)
        .basic()
        .desc("Round tip only: 100 % is a crisp anti-aliased edge; lower values fade smoothly from the core to the rim.")
        .render("Falloff (1 − t²)² from the hard core to the rim; the flow pass length follows so flow stays calibrated.")
        .cost("None beyond the tip evaluation.")
        .needs("Round tip")
        .tests("tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::hardness));
    add("roundness", "Roundness", PropertyType::Number)
        .percent(0.05, 1)
        .engines(TipEngines & ~EngineProcedural)
        .desc("Squashes the tip across its angle; 100 % keeps it round.")
        .render("The tip is sampled at (u, w / roundness) in its own rotated frame.")
        .cost("None.")
        .tests(EffectTest)
        .access(number(&BrushSettings::roundness));
    add("angle", "Angle", PropertyType::Number)
        .range(-360, 360, 1, "°")
        .engines(TipEngines)
        .desc("Rotation of the tip. Directions and pen azimuth add to it when the tip follows them.")
        .render("Rotates the tip's sampling frame.")
        .cost("None.")
        .tests(EffectTest)
        .access(number(&BrushSettings::angle));
    add("flipX", "Flip horizontally", PropertyType::Bool)
        .engines(TipEngines & ~EngineBristle & ~EngineProcedural)
        .desc("Mirrors the tip along its travel axis.")
        .render("Negates the tip's u coordinate. No visible effect on a symmetric (round) tip.")
        .cost("None.")
        .needs("An asymmetric tip image")
        .tests(EffectTest)
        .access(flag(&BrushSettings::flipX));
    add("flipY", "Flip vertically", PropertyType::Bool)
        .engines(TipEngines & ~EngineBristle & ~EngineProcedural)
        .desc("Mirrors the tip across its travel axis.")
        .render("Negates the tip's w coordinate. No visible effect on a symmetric (round) tip.")
        .cost("None.")
        .needs("An asymmetric tip image")
        .tests(EffectTest)
        .access(flag(&BrushSettings::flipY));
    add("minimumSize", "Minimum size", PropertyType::Number)
        .range(1, BrushSettings::MaxSize, 1, "px")
        .desc("No dab gets smaller than this, whatever pressure, tilt, taper, jitter or dynamics do (never more than "
              "the size).")
        .render("Clamps the final dab diameter from below.")
        .cost("None.")
        .tests(EffectTest)
        .access(number(&BrushSettings::minimumSize));
    add("tipDensity", "Density", PropertyType::Number)
        .percent(0.01, 1)
        .engines(TipEngines & ~EngineBristle & ~EngineProcedural & ~EngineCarried & ~EngineWatercolor)
        .desc("Share of the tip's pixels each dab paints; the rest are left out at random, differently for every dab.")
        .render("Per pixel and dab, a hash of position and dab index decides whether the pixel is painted.")
        .cost("Per pixel: one hash when below 100 %.")
        .tests(EffectTest)
        .access(number(&BrushSettings::tipDensity));
    add("tipSpikes", "Spikes", PropertyType::Integer)
        .range(2, 32)
        .desc("Planned: star-shaped round tips with this many points (Krita auto brush). Not implemented.");
    add("tipEdgeNoise", "Edge noise", PropertyType::Number)
        .percent()
        .desc("Planned: random roughness of the round tip's edge (Krita auto brush 'randomness'). Not implemented.");

    // ------------------------------------------------------------------------------ Stroke path
    g = QStringLiteral("Stroke Path");
    add("spacing", "Spacing", PropertyType::Number)
        .percent(0.02, 2)
        .basic()
        .desc("Distance between dab centres as a share of the dab diameter (at least 0.5 px).")
        .render("Dabs are placed by distance along the input path; flow is normalised to the spacing so it does not "
                "change the stroke's darkness.")
        .cost("Dabs per pixel of stroke = 1 / (spacing × diameter).")
        .tests("tst_core::dabsAreEvenlySpaced, tst_brush::noGapsOnFastStrokes, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::spacing));
    add("spacingJitter", "Spacing jitter", PropertyType::Number)
        .percent(0, 0.9)
        .desc("Varies each gap between dabs by up to this share of the spacing, at random.")
        .render("Multiplies each step by 1 ± jitter (deterministic per stroke seed).")
        .cost("None.")
        .tests(EffectTest)
        .access(number(&BrushSettings::spacingJitter));
    add("scatter", "Scatter", PropertyType::Number)
        .range(0, 4, 100, "%")
        .engines(EngineCoverage | EngineWet)
        .desc("Moves each dab sideways off the path by up to this many dab diameters, at random.")
        .render("Offsets the dab centre perpendicular to the direction of travel (and along it with both axes).")
        .cost("Dirty area grows with the scatter.")
        .tests(EffectTest)
        .access(number(&BrushSettings::scatter));
    add("scatterBothAxes", "Scatter both axes", PropertyType::Bool)
        .engines(EngineCoverage | EngineWet)
        .desc("Scatter also moves dabs along the path, not only across it.")
        .render("Adds an independent random offset along the direction of travel.")
        .cost("None.")
        .needs("Scatter above 0")
        .tests(EffectTest)
        .access(flag(&BrushSettings::scatterBothAxes));
    add("count", "Count", PropertyType::Integer)
        .range(1, 16, 1, "dabs")
        .engines(EngineCoverage | EngineWet)
        .desc("Dabs placed at every spacing step. Useful with scatter or jitter.")
        .render("Each step renders count dabs, each with its own random scatter and jitter. Not normalised: more dabs "
                "build up faster.")
        .cost("Multiplies the dab work.")
        .tests(EffectTest)
        .access(integer(&BrushSettings::count));
    add("countJitter", "Count jitter", PropertyType::Number)
        .percent()
        .engines(EngineCoverage | EngineWet)
        .desc("Randomly drops up to this share of the count at each step.")
        .render("Each step renders between count × (1 − jitter) and count dabs (at least one).")
        .cost("None.")
        .needs("Count above 1")
        .tests(EffectTest)
        .access(number(&BrushSettings::countJitter));
    add("airbrush", "Airbrush", PropertyType::Bool)
        .engines(EngineCoverage)
        .desc("Keeps adding dabs while the pen is held still (build-up).")
        .render("Dabs also come from elapsed time at the airbrush rate, at the last position.")
        .cost("Up to rate dabs per second while held.")
        .tests("tst_brush::airbrush*, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(flag(&BrushSettings::airbrush));
    add("airbrushRate", "Airbrush rate", PropertyType::Number)
        .range(1, 200, 1, "dabs/s")
        .engines(EngineCoverage)
        .desc("Dabs per second while the airbrush is held still.")
        .render("Interval between time dabs = 1000 / rate ms.")
        .cost("Linear in the rate.")
        .needs("Airbrush on")
        .tests(EffectTest)
        .access(number(&BrushSettings::airbrushRate));
    add("pixelSpacing", "Spacing in pixels", PropertyType::Number)
        .range(0.5, 200, 1, "px")
        .desc("Planned: a fixed distance between dabs, independent of the size. Not implemented.");
    add("pathWobble", "Path wobble", PropertyType::Number)
        .percent()
        .desc("Planned: smooth (low-frequency) sideways wander of the path, unlike per-dab scatter. Not implemented.");

    // ------------------------------------------------------------------------- Pressure and tilt
    g = QStringLiteral("Pressure and Tilt");
    const char *pressureRender = "value = minimum + (1 − minimum) × pressure^firmness; mouse and touch give 1 (no data "
                                 "is invented). Multiplies with the advanced dynamics of the same parameter.";
    add("pressureSize", "Pressure → size", PropertyType::Pressure)
        .basic()
        .sensors("pressure")
        .desc("Pen pressure changes the size: on/off, the size at the lightest pressure, and firmness.")
        .render(pressureRender)
        .cost("Per dab: one pow().")
        .tests("tst_brush::pressure*, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(pressure(&BrushSettings::pressureSize));
    add("pressureOpacity", "Pressure → opacity", PropertyType::Pressure)
        .basic()
        .sensors("pressure")
        .desc("Pen pressure changes the opacity ceiling (for smudge: the strength).")
        .render(pressureRender)
        .cost("Per dab: one pow().")
        .tests("tst_brush::pressure*, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(pressure(&BrushSettings::pressureOpacity));
    add("pressureFlow", "Pressure → flow", PropertyType::Pressure)
        .basic()
        .sensors("pressure")
        .engines(EngineCoverage)
        .desc("Pen pressure changes the flow (build-up brushes).")
        .render(pressureRender)
        .cost("Per dab: one pow().")
        .needs("Build-up or airbrush")
        .tests("tst_brush::pressure*, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(pressure(&BrushSettings::pressureFlow));
    add("tiltSize", "Size at full tilt", PropertyType::Number)
        .range(0.25, 4, 100, "%")
        .sensors("tilt amount")
        .desc("Size multiplier when the pen is tilted fully (pens that report tilt only).")
        .render("Factor 1 + (value − 1) × tilt amount.")
        .cost("None.")
        .needs("A pen that reports tilt")
        .tests("tst_brush::tiltScalesOnlyWhenReported, tst_brushengine::everyImplementedPropertyChangesRendering (simulated tilt)")
        .access(number(&BrushSettings::tiltSize));
    add("tiltOpacity", "Opacity at full tilt", PropertyType::Number)
        .percent()
        .sensors("tilt amount")
        .desc("Opacity multiplier when the pen is tilted fully (pens that report tilt only).")
        .render("Factor 1 + (value − 1) × tilt amount, on the ceiling (smudge: strength).")
        .cost("None.")
        .needs("A pen that reports tilt")
        .tests("tst_brush::tiltScalesOnlyWhenReported, tst_brushengine::everyImplementedPropertyChangesRendering (simulated tilt)")
        .access(number(&BrushSettings::tiltOpacity));
    add("fullTiltAngle", "Full tilt angle", PropertyType::Number)
        .range(20, 90, 1, "°")
        .sensors("tilt")
        .desc("The tilt from vertical that counts as full tilt, for the tilt factors and the tilt sensors.")
        .render("tilt amount = min(1, tilt / angle).")
        .cost("None.")
        .needs("A pen that reports tilt")
        .tests("tst_brushengine::everyImplementedPropertyChangesRendering (simulated tilt)")
        .access(number(&BrushSettings::fullTiltAngle));
    add("tiltAzimuthThreshold", "Tilt needed to steer", PropertyType::Number)
        .range(0, 60, 1, "°")
        .engines(EngineAll & ~EngineSpray) // spray particles are round: no direction to steer
        .sensors("tilt")
        .desc("A tip that follows direction uses the pen's azimuth only above this tilt; nearer upright it follows the "
              "stroke.")
        .render("Chooses between pen azimuth and travel direction per dab.")
        .cost("None.")
        .needs("Follow direction, a pen that reports tilt")
        .tests("tst_brushengine::everyImplementedPropertyChangesRendering (simulated tilt)")
        .access(number(&BrushSettings::tiltAzimuthThreshold));
    dyn(DynamicTarget::Size, "Size dynamics", EngineAll, "Diameter × value (before the minimum size).");
    dyn(DynamicTarget::Opacity, "Opacity dynamics", EngineAll,
        "Ceiling × value (smudge: strength; wet paint: deposit).");
    dyn(DynamicTarget::Flow, "Flow dynamics", EngineCoverage, "Per-pass flow × value.", "Build-up or airbrush");
    add("barrelRotation", "Barrel rotation", PropertyType::Dynamics)
        .sensors("pen barrel rotation")
        .desc("Planned: pens that report barrel rotation (QTabletEvent::rotation) as a sensor. Not implemented; no test "
              "hardware.");
    add("pressureCalibration", "Pressure calibration", PropertyType::Pressure)
        .sensors("pressure")
        .desc("Planned: a per-device pressure curve applied before every brush. Not implemented.");

    // --------------------------------------------------------------------------- Opacity and flow
    g = QStringLiteral("Opacity and Flow");
    add("opacity", "Opacity", PropertyType::Number)
        .percent(0.01, 1)
        .engines(EngineAll & ~EngineCarried) // smudge and wet paint: strength and wetness instead
        .basic()
        .desc("The most paint a stroke can lay down. Overlaps within one stroke never exceed it.")
        .render("Ceiling C of the coverage model, rounded to a whole 8-bit level.")
        .cost("None.")
        .tests("tst_brush::uniformNeverDarkensAndOpacityAppliesOnce, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::opacity));
    add("flow", "Flow", PropertyType::Number)
        .percent(0.01, 1)
        .basic()
        .engines(EngineCoverage | EngineWatercolor) // watercolour: the share each dab exchanges
        .desc("How much of the way to the opacity one pass deposits (build-up and airbrush).")
        .render("Per dab f = 1 − (1 − flow)^(step / pass length).")
        .cost("Per dab: one pow().")
        .needs("Build-up or airbrush")
        .tests("tst_brush::buildUpOnePassDepositsFlow, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::flow));
    add("accumulation", "Accumulation", PropertyType::Choice)
        .options({"uniform", "buildUp"}, {"Uniform", "Build up"})
        .engines(EngineCoverage & ~EngineBristle)
        .basic()
        .desc("Uniform: overlaps within a stroke never darken. Build up: slow or repeated passes darken towards the "
              "opacity.")
        .render("Uniform S = max(S, v·C); build-up S += v·f·(C − S) with stochastic rounding.")
        .cost("Same.")
        .tests("tst_brush::uniformNeverDarkensAndOpacityAppliesOnce, buildUpApproachesButNeverPassesCeiling, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(choice(&BrushSettings::accumulation, {"uniform", "buildUp"}));
    add("opacityJitter", "Opacity jitter", PropertyType::Number)
        .percent()
        .desc("Lowers each dab's opacity by up to this share, at random.")
        .render("Ceiling × (1 − jitter × random).")
        .cost("None.")
        .tests(EffectTest)
        .access(number(&BrushSettings::opacityJitter));
    add("flowJitter", "Flow jitter", PropertyType::Number)
        .percent()
        .engines(EngineCoverage)
        .desc("Lowers each dab's flow by up to this share, at random.")
        .render("Pass flow × (1 − jitter × random).")
        .cost("None.")
        .needs("Build-up or airbrush")
        .tests(EffectTest)
        .access(number(&BrushSettings::flowJitter));
    add("wetEdges", "Wet edges", PropertyType::Number)
        .percent()
        .desc("Planned: darker stroke rims, like watercolour (Krita 'wet edges'). Not implemented.");
    add("intenseGlaze", "Intense glaze", PropertyType::Bool)
        .desc("Planned: overlaps within a stroke multiply instead of capping at the opacity. Not implemented.");
    add("luminanceBlending", "Luminance blending", PropertyType::Bool)
        .desc("Planned: blend the stroke's lightness only. Not implemented.");

    // ---------------------------------------------------------------------------- Texture and grain
    g = QStringLiteral("Texture and Grain");
    const unsigned TexEngines = EngineAll;
    add("texture", "Texture", PropertyType::Choice)
        .options(textureNames, textureLabels)
        .engines(TexEngines)
        .basic()
        .desc("A generated paper or canvas grain that breaks up the paint.")
        .render("Per pixel the coverage is reduced in the texture's valleys (document space unless moving).")
        .cost("Per pixel: one texture sample (nearest at scale 1 without rotation, bilinear otherwise).")
        .tests("tst_brush::textureIsCoherentInDocumentSpace, subtractTextureFillsWithPressure, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(text(&BrushSettings::texture));
    add("textureScale", "Texture scale", PropertyType::Number)
        .range(0.1, 8, 100, "%")
        .engines(TexEngines & ~EngineWatercolor)
        .basic()
        .desc("Size of the grain.")
        .render("Texture coordinates divided by the scale (bilinear).")
        .cost("Bilinear sampling when not 100 %.")
        .needs("A texture")
        .tests(EffectTest)
        .access(number(&BrushSettings::textureScale));
    add("textureStrength", "Texture strength", PropertyType::Number)
        .percent()
        .engines(TexEngines)
        .basic()
        .desc("How much the grain removes from the paint.")
        .render("valley = strength × (1 − texture).")
        .cost("None.")
        .needs("A texture")
        .tests(EffectTest)
        .access(number(&BrushSettings::textureStrength));
    add("textureMode", "Texture mode", PropertyType::Choice)
        .options({"multiply", "subtract"}, {"Multiply", "Tooth (pressure fills it)"})
        .engines(TexEngines & ~EngineDry & ~EngineWatercolor & ~EngineCarried & ~EngineBristle)
        .basic()
        .desc("Multiply scales the paint by the grain; tooth removes paint in the valleys first, so light marks break "
              "up and heavy ones fill in.")
        .render("Multiply: s × (1 − valley). Subtract: opacity × max(0, s/opacity − valley).")
        .cost("None.")
        .needs("A texture")
        .tests("tst_brush::textureIsCoherentInDocumentSpace, subtractTextureFillsWithPressure, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(choice(&BrushSettings::textureMode, {"multiply", "subtract"}));
    add("textureRotation", "Texture rotation", PropertyType::Number)
        .range(-180, 180, 1, "°")
        .engines(TexEngines & ~EngineWatercolor)
        .desc("Turns the grain.")
        .render("Rotates the texture coordinates (bilinear sampling).")
        .cost("Bilinear sampling.")
        .needs("A texture")
        .tests(EffectTest)
        .access(number(&BrushSettings::textureRotation));
    add("textureBrightness", "Texture brightness", PropertyType::Number)
        .range(-1, 1, 100, "%")
        .engines(TexEngines & ~EngineWatercolor)
        .desc("Lightens (fewer valleys) or darkens (more valleys) the grain.")
        .render("t + brightness, after contrast, clamped to 0..1.")
        .cost("None.")
        .needs("A texture")
        .tests(EffectTest)
        .access(number(&BrushSettings::textureBrightness));
    add("textureContrast", "Texture contrast", PropertyType::Number)
        .range(0, 3, 100, "%")
        .engines(TexEngines & ~EngineWatercolor)
        .desc("Flattens (below 100 %) or sharpens (above) the grain.")
        .render("(t − 0.5) × contrast + 0.5.")
        .cost("None.")
        .needs("A texture")
        .tests(EffectTest)
        .access(number(&BrushSettings::textureContrast));
    add("textureInvert", "Invert texture", PropertyType::Bool)
        .engines(TexEngines & ~EngineWatercolor)
        .desc("Swaps the grain's peaks and valleys.")
        .render("t → 1 − t before contrast and brightness.")
        .cost("None.")
        .needs("A texture")
        .tests(EffectTest)
        .access(flag(&BrushSettings::textureInvert));
    add("textureEachDab", "Texture each dab", PropertyType::Bool)
        .engines(EngineCoverage & ~EngineDry & ~EngineBristle)
        .desc("Applies the grain to every dab instead of the finished stroke, so overlapping dabs fill the grain in "
              "(build-up). Texture strength dynamics always work this way.")
        .render("The grain multiplies each dab's tip value instead of the stroke coverage at composite time.")
        .cost("Per pixel of each dab: one texture sample (instead of one per pixel per event).")
        .needs("A texture")
        .tests(EffectTest)
        .access(flag(&BrushSettings::textureEachDab));
    add("textureMoving", "Moving grain", PropertyType::Bool)
        .engines(EngineCoverage & ~EngineDry & ~EngineBristle & ~EngineSpray)
        .desc("The grain moves and turns with each dab (stamped), instead of staying fixed on the paper.")
        .render("Texture coordinates relative to the dab centre, rotated with the dab.")
        .cost("Bilinear sampling.")
        .needs("A texture, texture each dab")
        .tests(EffectTest)
        .access(flag(&BrushSettings::textureMoving));
    add("textureRandomOffset", "Random grain offset", PropertyType::Bool)
        .engines(TexEngines & ~EngineWatercolor)
        .desc("Shifts the grain to a new random position for every stroke, so repeated strokes do not line up.")
        .render("Adds a per-stroke offset (from the stroke seed) to the texture coordinates.")
        .cost("None.")
        .needs("A texture")
        .tests(EffectTest)
        .access(flag(&BrushSettings::textureRandomOffset));
    dyn(DynamicTarget::TextureStrength, "Texture strength dynamics", EngineCoverage & ~EngineDry,
        "Each dab's texture strength × value (switches the texture to each-dab).", "A texture");

    // ---------------------------------------------------------------------------- Shape dynamics
    g = QStringLiteral("Shape Dynamics");
    add("followDirection", "Follow direction", PropertyType::Bool)
        .engines(TipEngines)
        .basic()
        .sensors("direction, tilt direction")
        .desc("The tip turns with the stroke, or with the pen's azimuth when it is tilted enough.")
        .render("Adds the smoothed travel direction (or the azimuth) to the angle.")
        .cost("None.")
        .tests("tst_brush::tipFollowsDirectionOrTilt, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(flag(&BrushSettings::followDirection));
    add("angleJitter", "Angle jitter", PropertyType::Number)
        .range(0, 180, 1, "°")
        .engines(TipEngines)
        .desc("Turns each dab by up to ± this angle, at random.")
        .render("Adds a random angle per dab.")
        .cost("None.")
        .tests(EffectTest)
        .access(number(&BrushSettings::angleJitter));
    add("sizeJitter", "Size jitter", PropertyType::Number)
        .percent()
        .desc("Shrinks each dab by up to this share, at random.")
        .render("Diameter × (1 − jitter × random).")
        .cost("None.")
        .tests(EffectTest)
        .access(number(&BrushSettings::sizeJitter));
    add("strokeSizeJitter", "Size jitter per stroke", PropertyType::Number)
        .percent()
        .desc("Shrinks the whole stroke by up to this share, at random.")
        .render("Diameter × (1 − jitter × random per stroke).")
        .cost("None.")
        .tests(EffectTest)
        .access(number(&BrushSettings::strokeSizeJitter));
    add("roundnessJitter", "Roundness jitter", PropertyType::Number)
        .percent()
        .engines(TipEngines & ~EngineProcedural)
        .desc("Squashes each dab by up to this share, at random.")
        .render("Roundness × (1 − jitter × random), at least 5 %.")
        .cost("None.")
        .tests(EffectTest)
        .access(number(&BrushSettings::roundnessJitter));
    add("randomFlipX", "Random flip horizontally", PropertyType::Bool)
        .engines(TipEngines & ~EngineBristle & ~EngineProcedural)
        .desc("Mirrors about half the dabs along the travel axis, at random.")
        .render("Per dab, toggles flip horizontally.")
        .cost("None.")
        .needs("An asymmetric tip image")
        .tests(EffectTest)
        .access(flag(&BrushSettings::randomFlipX));
    add("randomFlipY", "Random flip vertically", PropertyType::Bool)
        .engines(TipEngines & ~EngineBristle & ~EngineProcedural)
        .desc("Mirrors about half the dabs across the travel axis, at random.")
        .render("Per dab, toggles flip vertically.")
        .cost("None.")
        .needs("An asymmetric tip image")
        .tests(EffectTest)
        .access(flag(&BrushSettings::randomFlipY));
    dyn(DynamicTarget::Rotation, "Rotation dynamics", TipEngines, "Adds value × 360° to the angle.");
    dyn(DynamicTarget::Roundness, "Roundness dynamics", TipEngines & ~EngineProcedural, "Roundness × value (at least 5 %).");
    dyn(DynamicTarget::Scatter, "Scatter dynamics", EngineCoverage | EngineWet, "Scatter × value.", "Scatter above 0");
    dyn(DynamicTarget::Spacing, "Spacing dynamics", EngineAll,
        "The step after each dab × value (at least 0.5 px); flow stays normalised to the actual step.");
    add("tipAnimation", "Tip sequence", PropertyType::Text)
        .desc("Planned: cycle through several tip images along the stroke (Krita animated tips). Not implemented.");

    // ---------------------------------------------------------------------------- Colour dynamics
    g = QStringLiteral("Color Dynamics");
    const unsigned ColorEngines =
        EnginePaint | EngineSpray | EngineHatching | EngineDry | EngineWet | EngineWatercolor | EngineProcedural;
    auto colorJitter = [&](const char *id, const char *name, double max, double scale, const char *unit,
                           double BrushSettings::*m, const char *what, bool perStroke) {
        add(id, name, PropertyType::Number)
            .range(0, max, scale, unit)
            .engines(ColorEngines)
            .desc(what)
            .render(perStroke ? "Changes the stroke colour once, from the stroke seed (no per-pixel colour needed)."
                              : "Each dab gets its own colour; the stroke keeps a colour per pixel, mixed by coverage.")
            .cost(perStroke ? "None." : "Per touched tile: 16 KiB of colour; per pixel of each dab: a weighted mix.")
            .tests("tst_brushengine::colorDynamicsVaryPerDab, tst_brushengine::everyImplementedPropertyChangesRendering")
            .access(number(m));
    };
    colorJitter("hueJitter", "Hue jitter", 180, 1, "°", &BrushSettings::hueJitter,
                "Turns each dab's hue by up to ± this angle, at random.", false);
    colorJitter("saturationJitter", "Saturation jitter", 1, 100, "%", &BrushSettings::saturationJitter,
                "Changes each dab's saturation by up to ± this amount, at random.", false);
    colorJitter("lightnessJitter", "Lightness jitter", 1, 100, "%", &BrushSettings::lightnessJitter,
                "Changes each dab's lightness by up to ± this amount, at random.", false);
    colorJitter("strokeHueJitter", "Hue jitter per stroke", 180, 1, "°", &BrushSettings::strokeHueJitter,
                "Turns the whole stroke's hue by up to ± this angle, at random.", true);
    colorJitter("strokeSaturationJitter", "Saturation jitter per stroke", 1, 100, "%",
                &BrushSettings::strokeSaturationJitter, "Changes the stroke's saturation by up to ± this amount.", true);
    colorJitter("strokeLightnessJitter", "Lightness jitter per stroke", 1, 100, "%",
                &BrushSettings::strokeLightnessJitter, "Changes the stroke's lightness by up to ± this amount.", true);
    dyn(DynamicTarget::Hue, "Hue dynamics", ColorEngines,
        "Turns the hue by value × 360° (a full reading is a full turn: the chosen hue).");
    dyn(DynamicTarget::Saturation, "Saturation dynamics", ColorEngines, "Saturation × value.");
    dyn(DynamicTarget::Lightness, "Lightness dynamics", ColorEngines, "Lightness × value.");
    add("secondaryColor", "Secondary colour mix", PropertyType::Dynamics)
        .desc("Planned: mix towards a second colour by a sensor. DrawApp has no secondary colour yet. Not implemented.");

    // ------------------------------------------------------------------------- Blending and smudge
    g = QStringLiteral("Blending and Smudge");
    add("paintMode", "Blend mode", PropertyType::Choice)
        .options({"normal", "multiply", "screen", "behind", "preserveAlpha"},
                 {"Normal", "Multiply", "Screen", "Behind", "Inside (preserve alpha)"})
        .engines(EnginePaint | EngineSpray | EngineHatching | EngineDry | EngineProcedural)
        .desc("How the stroke's colour combines with the layer: over it, multiplied (darkens), screened (lightens), "
              "behind existing paint, or only on existing paint without changing its alpha.")
        .render("W3C compositing formulas on premultiplied pixels with the stroke coverage as source alpha.")
        .cost("Per composited pixel: a few multiplies.")
        .tests("tst_brushengine::paintModes, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(choice(&BrushSettings::paintMode, {"normal", "multiply", "screen", "behind", "preserveAlpha"}));
    add("smudgeStrength", "Smudge strength", PropertyType::Number)
        .percent()
        .engines(EngineSmudge)
        .basic()
        .desc("How strongly the carried paint is laid down, per pass.")
        .render("result = pixel + (carried − pixel) × strength × tip, normalised to the spacing.")
        .cost("Per pixel of each dab: one mix.")
        .tests("tst_brush::smudgeCarriesColour, smudgeTransparencyRules, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::smudgeStrength));
    add("smudgePickup", "Pickup", PropertyType::Number)
        .percent()
        .engines(EngineSmudge)
        .basic()
        .desc("How quickly the carried paint takes on the colour it passes (short smears at high values).")
        .render("carried += (pixel − carried) × pickup × tip, normalised to the spacing.")
        .cost("Per pixel of each dab: one mix.")
        .tests("tst_brush::smudgeCarriesColour, smudgeTransparencyRules, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::smudgePickup));
    add("smudgeMode", "Smudge mode", PropertyType::Choice)
        .options({"smear", "dulling", "mix"}, {"Smear", "Dulling", "Mix (one carried colour)"})
        .engines(EngineSmudge)
        .desc("Smear drags the paint pixel by pixel; dulling carries one averaged colour (softer, blends to flat "
              "colour); mix (milestone 11) carries one colour that blends what it passes with what it carries and the "
              "brush colour, for as long as the smudge length says.")
        .render("Dulling replaces the carried patch by its coverage-weighted average after each pickup. Mix keeps one "
                "premultiplied colour, mixed with the centre-weighted average under each dab.")
        .cost("Dulling: one extra pass over the patch per dab. Mix: one sampling pass and one deposit pass per dab.")
        .tests("tst_brushengine::smudgeModes, tst_brushmedia::mix*, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(choice(&BrushSettings::smudgeMode, {"smear", "dulling", "mix"}));
    add("smudgeColorRate", "Colour rate", PropertyType::Number)
        .percent()
        .engines(EngineSmudge)
        .desc("Mixes the brush colour into the carried paint on every pass (paints while smudging).")
        .render("carried += (brush colour − carried) × rate × tip, normalised to the spacing.")
        .cost("Per pixel of each dab: one mix.")
        .tests(EffectTest)
        .access(number(&BrushSettings::smudgeColorRate));
    add("smudgePreserveAlpha", "Smudge colour only", PropertyType::Bool)
        .engines(EngineSmudge | EngineWet)
        .desc("Moves colour but keeps each pixel's alpha, so smudging never spreads paint into transparency.")
        .render("The result keeps the pixel's alpha; colour is mixed in unpremultiplied form.")
        .cost("A divide per pixel.")
        .tests(EffectTest)
        .access(flag(&BrushSettings::smudgePreserveAlpha));
    dyn(DynamicTarget::SmudgeStrength, "Smudge strength dynamics", EngineSmudge | EngineWet,
        "Strength (wet paint: attack) × value.");
    add("sampleMerged", "Sample all layers", PropertyType::Bool)
        .desc("Planned: smudge picks up colour from all visible layers. Not implemented.");
    add("smudgeRadius", "Smudge radius", PropertyType::Number)
        .percent(0.25, 1)
        .engines(EngineSmudge)
        .since(11)
        .desc("The part of the dab colour is picked up from (milestone 11, for smudge mode Mix): smaller values sample "
              "the middle only, so the dab's edges do not pull in neighbouring colours. Dulling and smear sample the "
              "whole dab as before.")
        .render("Mix: the tip evaluated at the distance divided by this share weights the sample.")
        .cost("None.")
        .needs("Smudge mode Mix")
        .tests("tst_brushmedia::mix*, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::smudgeRadius));
    add("moreBlendModes", "Overlay and colour modes", PropertyType::Choice)
        .desc("Planned: overlay, soft light, hue, saturation, colour and luminosity blend modes. Not implemented.");
    add("pigmentMixing", "Pigment mixing", PropertyType::Bool)
        .engines(EngineSmudge | EngineWet | EngineBristle)
        .since(11)
        .desc("Mixes colours like paint instead of light (milestone 11): blue and yellow give green rather than grey. "
              "Per-channel Kubelka–Munk in linear light; pure screen primaries mix towards black (three channels cannot "
              "hold pigment spectra).")
        .render("Every colour mix of the carried paint (pickup, colour rate, lay-down; wet paint refill and pickup) goes "
                "through K/S = (1 − R)² / 2R per channel, weighted by alpha.")
        .cost("Per pixel of each dab: three table look-ups and three square roots per mix.")
        .tests("tst_brushmedia::mix*, tst_brushmedia::pigment*, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(flag(&BrushSettings::pigmentMixing));
    add("blurMode", "Blur mode", PropertyType::Bool)
        .desc("Planned: soften pixels in place instead of carrying paint. Not implemented.");

    // ---------------------------------------------------------------------------------- Wet paint
    g = QStringLiteral("Wet Paint");
    auto wet = [&](const char *id, const char *name, double BrushSettings::*m, const char *what, const char *render,
                   double lo = 0, double hi = 1, double scale = 100, const char *unit = "%") {
        add(id, name, PropertyType::Number)
            .range(lo, hi, scale, unit)
            .engines(EngineWet)
            .desc(what)
            .render(render)
            .cost("Per pixel of each dab: two mixes (deposit, pick up); per dab: one refill of the patch.")
            .tests("tst_brushengine::wetPaint*, tst_brushengine::everyImplementedPropertyChangesRendering")
            .access(number(m));
    };
    wet("wetLoad", "Load", &BrushSettings::wetLoad, "How much brush colour the brush carries at the start of a stroke.",
        "The patch is refilled towards the brush colour by load × (remaining share of the load length) per pass.");
    wet("wetLoadLength", "Load length", &BrushSettings::wetLoadLength,
        "How far the brush paints before the load runs out (then it only drags paint). 0 never runs out.",
        "remaining = max(0, 1 − distance / length).", 0, 10000, 1, "px");
    wet("wetAttack", "Attack", &BrushSettings::wetAttack, "How much of the carried paint each pass lays down.",
        "Carried paint is composited over the pixel with alpha attack × tip, normalised to the spacing.");
    wet("wetPull", "Pull", &BrushSettings::wetPull, "How much canvas paint each pass picks up and drags along.",
        "carried += (pixel − carried) × pull × tip, normalised to the spacing.");
    wet("wetDilution", "Dilution", &BrushSettings::wetDilution,
        "Thins the brush paint (water): lower alpha, so it glazes and mixes more.",
        "The refill colour's alpha × (1 − dilution).");
    wet("wetnessJitter", "Wetness jitter", &BrushSettings::wetnessJitter,
        "Varies the dilution from dab to dab, at random.", "Dilution + jitter × random per dab, clamped to 1.");
    add("wetGrade", "Grade", PropertyType::Number)
        .percent()
        .desc("Planned: pressure-dependent load (Procreate 'grade'). Not implemented.");
    add("wetBlur", "Blur", PropertyType::Number)
        .percent()
        .desc("Planned: blur of the carried paint while mixing. Not implemented.");
    add("wetBlurJitter", "Blur jitter", PropertyType::Number)
        .percent()
        .desc("Planned. Not implemented.");
    add("dryingTime", "Drying time", PropertyType::Number)
        .range(0, 60, 1, "s")
        .desc("Planned: paint that stays wet across strokes and dries over time. Not implemented (no persistent "
              "wetness layer).");

    // ------------------------------------------------------------------------------ Stabilization
    g = QStringLiteral("Stabilization");
    add("smoothing", "Smoothing", PropertyType::Integer)
        .range(0, 100, 1, "")
        .basic()
        .desc("Pull-string stabiliser: the brush follows the pointer on a string, removing wobble (in screen pixels, "
              "so it feels the same at every zoom).")
        .render("Brush point moves only when the pointer is farther than the string length.")
        .cost("Walks the input in 0.5 px steps.")
        .tests("tst_core::smoothing*, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(integer(&BrushSettings::smoothing));
    add("pressureSmoothing", "Pressure smoothing", PropertyType::Number)
        .percent()
        .sensors("pressure")
        .desc("Evens out jumpy pressure readings (time constant up to 100 ms).")
        .render("Exponential smoothing of pressure by event time (per event without times).")
        .cost("None.")
        .needs("A pen that reports pressure")
        .tests("tst_brushengine::everyImplementedPropertyChangesRendering (simulated pressure)")
        .access(number(&BrushSettings::pressureSmoothing));
    add("jitterFilter", "Jitter filter", PropertyType::Number)
        .range(0, 20, 1, "px")
        .desc("Ignores pointer moves shorter than this (hand tremor); the end point is always drawn.")
        .render("Input samples closer than the distance to the last accepted one are held back.")
        .cost("None.")
        .tests(EffectTest)
        .access(number(&BrushSettings::jitterFilter));
    add("smoothingFinish", "Finish at the pen", PropertyType::Bool)
        .desc("With smoothing, lifting the pen draws the remaining line to where it was lifted. Off: the line ends "
              "where the string is.")
        .render("Pen-up completes (or drops) the smoothing string's tail.")
        .cost("None.")
        .needs("Smoothing above 0")
        .tests(EffectTest)
        .access(flag(&BrushSettings::smoothingFinish));
    add("streamline", "Speed-adaptive smoothing", PropertyType::Number)
        .percent()
        .desc("Planned: more smoothing for slow strokes, less for fast ones. Not implemented.");
    add("motionPrediction", "Motion prediction", PropertyType::Bool)
        .desc("Planned: draws slightly ahead of the pointer to hide latency. Not implemented.");
    add("stabilizerWindow", "Averaging stabiliser", PropertyType::Integer)
        .range(1, 64)
        .desc("Planned: weighted moving average of the last N samples (Krita 'stabilizer'). Not implemented.");
    add("lineSnap", "Straight line snap", PropertyType::Bool)
        .desc("Planned: hold at the end of a stroke to straighten it. Not implemented.");

    // -------------------------------------------------------------------------------------- Taper
    g = QStringLiteral("Taper");
    add("taperStart", "Start taper", PropertyType::Number)
        .range(0, 2000, 1, "px")
        .desc("The stroke grows from a thin tip to full size over this length.")
        .render("Size (and optionally opacity) × start size + (1 − start size) × t^shape, t = distance / length.")
        .cost("None.")
        .tests("tst_brushengine::taper*, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::taperStart));
    add("taperEnd", "End taper", PropertyType::Number)
        .range(0, 2000, 1, "px")
        .desc("The stroke thins to a tip over its last this-many pixels. The last part of the line is drawn when the "
              "pen lifts (that is when its end is known).")
        .render("Input within the taper length of the newest point is held back; at pen up it is drawn with the "
                "taper from the now known end.")
        .cost("The live line lags by the taper length.")
        .tests("tst_brushengine::taper*, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::taperEnd));
    add("taperStartSize", "Start tip size", PropertyType::Number)
        .percent()
        .desc("Size at the very start of a start taper, as a share of the size.")
        .render("See start taper.")
        .cost("None.")
        .needs("Start taper above 0")
        .tests(EffectTest)
        .access(number(&BrushSettings::taperStartSize));
    add("taperEndSize", "End tip size", PropertyType::Number)
        .percent()
        .desc("Size at the very end of an end taper, as a share of the size.")
        .render("See end taper.")
        .cost("None.")
        .needs("End taper above 0")
        .tests(EffectTest)
        .access(number(&BrushSettings::taperEndSize));
    add("taperOpacity", "Taper opacity", PropertyType::Number)
        .percent()
        .desc("How much the tapers also fade the opacity.")
        .render("Ceiling × (1 − amount × (1 − taper factor)).")
        .cost("None.")
        .needs("A taper")
        .tests(EffectTest)
        .access(number(&BrushSettings::taperOpacity));
    add("taperShape", "Taper shape", PropertyType::Number)
        .range(0.25, 4, 1, "")
        .desc("Below 1 the taper swells quickly (blunt); above 1 it stays thin longer (sharp).")
        .render("The exponent on t.")
        .cost("Per dab: one pow().")
        .needs("A taper")
        .tests(EffectTest)
        .access(number(&BrushSettings::taperShape));
    add("taperPenOnly", "Taper only without pressure", PropertyType::Bool)
        .desc("Planned: separate taper for mouse/touch and pressure pens (Procreate). Not implemented.");
    add("taperTipSharpness", "Tip sharpness", PropertyType::Number)
        .percent()
        .desc("Planned: roundness change towards the taper tip. Not implemented.");

    // ---------------------------------------------------------------------------- Special effects
    g = QStringLiteral("Special Effects");
    add("sprayDensity", "Spray particles", PropertyType::Number)
        .range(1, 500, 1, "per dab")
        .engines(EngineSpray)
        .desc("Particles in each spray dab.")
        .render("Particles at uniformly random positions over the dab's disc.")
        .cost("Linear in the particle count.")
        .needs("Spray dab type")
        .tests("tst_brushengine::sprayAndHatching, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::sprayDensity));
    add("sprayParticleSize", "Particle size", PropertyType::Number)
        .range(0.5, 64, 1, "px")
        .engines(EngineSpray)
        .desc("Diameter of each spray particle.")
        .render("Anti-aliased round particle.")
        .cost("Quadratic in the particle size.")
        .needs("Spray dab type")
        .tests(EffectTest)
        .access(number(&BrushSettings::sprayParticleSize));
    add("sprayParticleJitter", "Particle size jitter", PropertyType::Number)
        .percent()
        .engines(EngineSpray)
        .desc("Shrinks each particle by up to this share, at random.")
        .render("Particle diameter × (1 − jitter × random).")
        .cost("None.")
        .needs("Spray dab type")
        .tests(EffectTest)
        .access(number(&BrushSettings::sprayParticleJitter));
    add("hatchAngle", "Hatching angle", PropertyType::Number)
        .range(-180, 180, 1, "°")
        .engines(EngineHatching)
        .desc("Direction of the hatching lines.")
        .render("Lines fixed in document space, so dabs join into continuous hatching.")
        .cost("Per pixel: one distance.")
        .needs("Hatching dab type")
        .tests("tst_brushengine::sprayAndHatching, tst_brushengine::everyImplementedPropertyChangesRendering")
        .access(number(&BrushSettings::hatchAngle));
    add("hatchSpacing", "Hatching spacing", PropertyType::Number)
        .range(2, 100, 1, "px")
        .engines(EngineHatching)
        .desc("Distance between hatching line centres.")
        .render("Line period.")
        .cost("None.")
        .needs("Hatching dab type")
        .tests(EffectTest)
        .access(number(&BrushSettings::hatchSpacing));
    add("hatchThickness", "Hatching thickness", PropertyType::Number)
        .range(0.5, 50, 1, "px")
        .engines(EngineHatching)
        .desc("Width of each hatching line.")
        .render("Anti-aliased line coverage.")
        .cost("None.")
        .needs("Hatching dab type")
        .tests(EffectTest)
        .access(number(&BrushSettings::hatchThickness));

    // ------------------------------------------------------------------- Mixing (milestone 11)
    g = QStringLiteral("Mixing");
    const char *const MixTests = "tst_brushmedia::mix*";
    add("smudgeLength", "Smudge length", PropertyType::Number)
        .percent()
        .engines(EngineSmudge)
        .since(11)
        .desc("How long a picked-up colour persists: short lengths follow the canvas closely, long ones drag a colour "
              "far into the next one.")
        .render("Mix: the carried colour takes on the area under each dab by (1 − length), per pass.")
        .cost("None.")
        .needs("Smudge mode Mix")
        .tests(MixTests)
        .access(number(&BrushSettings::smudgeLength));
    add("smudgeDepletion", "Depletion", PropertyType::Number)
        .percent(0, 0.99)
        .engines(EngineSmudge)
        .since(11)
        .desc("The carried paint runs out as the stroke goes on: this share is lost per 100 px of travel.")
        .render("Mix: lay-down strength × (1 − depletion)^(distance / 100 px).")
        .cost("Per dab: one power.")
        .needs("Smudge mode Mix")
        .tests(MixTests)
        .access(number(&BrushSettings::smudgeDepletion));
    dyn(DynamicTarget::ColorRate, "Colour rate dynamics", EngineSmudge,
        "Colour rate × value: for example pressure adds paint while a light touch only blends.", "Colour rate above 0");
    e.back().p.since = 11;

    // ------------------------------------------------------------ Ink and stroke (milestone 11)
    g = QStringLiteral("Ink and Stroke");
    const char *const InkTests = "tst_brushmedia::ink*";
    add("curveInterpolation", "Curve interpolation", PropertyType::Bool)
        .engines(EngineAll)
        .since(11)
        .desc("Draws smooth curves between the pen's input samples instead of straight segments, so fast curves stay "
              "round. The line trails the pen by one input sample.")
        .render("Centripetal Catmull–Rom segment through the neighbouring samples, subdivided about every pixel; "
                "pressure, tilt and time interpolated linearly.")
        .cost("Per input sample: about one point per pixel of path (the spacer was already walking it).")
        .tests(InkTests)
        .access(flag(&BrushSettings::curveInterpolation));
    add("cornerAngle", "Corner angle", PropertyType::Number)
        .range(10, 180, 1, "°")
        .engines(EngineAll)
        .since(11)
        .desc("Turns sharper than this stay sharp corners instead of being rounded by the curve.")
        .render("At a corner the curve's tangent follows the segment itself (a straight start or end).")
        .cost("Per input sample: one angle.")
        .needs("Curve interpolation")
        .tests(InkTests)
        .access(number(&BrushSettings::cornerAngle));
    add("taperLive", "Live end taper", PropertyType::Bool)
        .engines(EngineCoverage & ~EngineBristle) // bristle state cannot be redrawn: the hold-back stays
        .since(11)
        .desc("Draws the line right up to the pen while drawing and tapers the end when the pen lifts, instead of "
              "holding the last taper-length of the line back until then.")
        .render("At pen-up the last taper-length is spaced again with the taper and the tiles it touched are rebuilt "
                "from the stroke's recorded dabs.")
        .cost("Per dab: about 150 bytes recorded for the stroke; at pen-up, the tail's tiles are rebuilt once.")
        .needs("End taper above 0")
        .tests(InkTests)
        .access(flag(&BrushSettings::taperLive));
    add("smoothingSpeedAdapt", "Speed-adaptive smoothing", PropertyType::Number)
        .percent()
        .engines(EngineAll)
        .since(11)
        .desc("Shortens the smoothing string when the pen moves fast, so quick strokes follow the hand closely while "
              "slow, careful ones stay smooth.")
        .render("String length × (1 − value × min(1, pointer speed / 1.5 px/ms)), speed smoothed over about 30 ms.")
        .cost("Per input sample: one distance and one exponential.")
        .needs("Smoothing above 0, a device that reports event times")
        .tests(InkTests)
        .access(number(&BrushSettings::smoothingSpeedAdapt));

    // ------------------------------------------------------------------ Watercolour (milestone 11)
    g = QStringLiteral("Watercolour");
    const char *const WcTests = "tst_brushmedia::watercolour*";
    auto wc = [&](const char *id, const char *name, double BrushSettings::*m, double lo, double hi, double scale,
                  const char *unit, const char *desc, const char *render, const char *cost) {
        add(id, name, PropertyType::Number)
            .range(lo, hi, scale, unit)
            .engines(EngineWatercolor)
            .since(11)
            .desc(desc)
            .render(render)
            .cost(cost)
            .tests(WcTests)
            .access(number(m));
    };
    wc("wcWater", "Water", &BrushSettings::wcWater, 0, 1, 100, "%",
       "How wet the brush is: the paper under it is wetted up to this level. Wetter paint spreads further and stays "
       "wet longer; a wetter brush in a damp wash adds water to it.",
       "Water → water + (value × min(1, 3 × tip) − water) × min(1, 4 × pass), where lower; pass = flow normalised to "
       "the spacing.",
       "Per pixel of each dab: one update of the wet cell.");
    wc("wcPigment", "Pigment", &BrushSettings::wcPigment, 0, 1, 100, "%",
       "How much colour the brush carries (with opacity and its pressure response): low values give pale washes "
       "that can be glazed once dry.",
       "The wash under the brush moves towards value × opacity of the brush colour by min(1, 4 × pass) × contact "
       "per dab; glaze opacity = 1 − e^(−2.5 × amount).",
       "None beyond the water update.");
    wc("wcSpread", "Spread", &BrushSettings::wcSpread, 0, 1, 100, "%",
       "How readily water and pigment move through the wet area: high values bloom into a wet wash, low values stay "
       "where they were put.", "Water diffusion between wet cells 0.08 + 0.6 × spread per step; pigment 0.02 + 0.2 × spread.",
       "Per wet cell and step: four neighbour exchanges.");
    wc("wcEdgeDarkening", "Edge darkening", &BrushSettings::wcEdgeDarkening, 0, 1, 100, "%",
       "Water flows to the edge of a wash and evaporates there, leaving its pigment: the darker rim of real "
       "watercolour.",
       "Pressure = water × (1 − 0.8 × value × edge) and evaporation × (1 + 4 × value × edge), edge = share of dry "
       "paper within two cells; the outward flow carries pigment.",
       "None beyond the step.");
    wc("wcGranulation", "Granulation", &BrushSettings::wcGranulation, 0, 1, 100, "%",
       "Pigment settles in the valleys of the paper, so dried washes show the paper's grain.",
       "Settling rate × (1 + 3 × value × (1 − paper height)).", "Per wet cell and step: one paper sample.");
    wc("wcDryingTime", "Drying time", &BrushSettings::wcDryingTime, 0.5, 60, 1, "s",
       "How long a wash stays wet (simulation time; on the canvas it dries in real time after the stroke). Painting "
       "into a wash before it dries mixes wet-on-wet.",
       "Evaporation per 16 ms step = 0.6 / (time / 16 ms), more at the wet edge.",
       "The canvas runs a bounded number of steps per timer tick until the paint is dry.");
    wc("wcLifting", "Lifting", &BrushSettings::wcLifting, 0, 1, 100, "%",
       "Clear water over paint that has settled lifts it: the damp brush takes part of the colour up and the rest "
       "moves in the wash (while the canvas keeps the paint's wet state).",
       "Per dab: settled pigment × lifting × contact × pass is loosened into the wash, where the brush's paint "
       "exchange takes part of it up.",
       "Per pixel of each dab: one mix.");

    // ------------------------------------------------------------------ Procedural (milestone 11)
    g = QStringLiteral("Procedural");
    const char *const ProcTests = "tst_brushmedia::procedural*";
    const unsigned Elements = EngineStrands | EngineGrass | EngineFoliage | EngineSplatter;
    const unsigned Lines = EngineStrands | EngineGrass;
    add("procCount", "Elements per dab", PropertyType::Integer)
        .range(1, 64, 1, "")
        .engines(Elements)
        .basic()
        .since(11)
        .desc("How many strands, grass blades, leaves or drops each dab adds.")
        .render("Elements generated per dab from the stroke seed and the dab index.")
        .cost("Linear in the count (each element: its own anti-aliased geometry).")
        .tests(ProcTests)
        .access(integer(&BrushSettings::procCount));
    add("procLength", "Element length", PropertyType::Number)
        .range(0.1, 4, 100, "% of size")
        .engines(Lines | EngineFoliage | EnginePattern)
        .basic()
        .since(11)
        .desc("Length of each strand, blade or leaf, and of the pattern's motif, relative to the brush size.")
        .render("Length = value × dab diameter (so size and its dynamics scale it).")
        .cost("Grows with the length (longer elements cover more pixels).")
        .tests(ProcTests)
        .access(number(&BrushSettings::procLength));
    add("procWidth", "Element width", PropertyType::Number)
        .range(0.3, 60, 1, "px")
        .engines(Elements | EnginePattern)
        .since(11)
        .desc("Width of strands and blades at the root, of leaves, of the largest splatter drops and of the pattern's "
              "lines, at the brush size (it scales with size dynamics).")
        .render("Width × dab diameter / brush size; below 1 px the coverage is scaled down instead.")
        .cost("Grows with the width.")
        .tests(ProcTests)
        .access(number(&BrushSettings::procWidth));
    add("procLengthJitter", "Length variation", PropertyType::Number)
        .percent()
        .engines(Elements)
        .since(11)
        .desc("Elements are shorter by up to this share, at random; for splatter, how strongly small drops outnumber "
              "large ones.")
        .render("Length × (1 − value × random); splatter: drop radius = largest × random^(1 + 3 × value).")
        .cost("None.")
        .tests(ProcTests)
        .access(number(&BrushSettings::procLengthJitter));
    add("procAngleJitter", "Angle variation", PropertyType::Number)
        .range(0, 180, 1, "°")
        .engines(Elements)
        .since(11)
        .desc("Turns each element by up to ± this angle from the direction, at random.")
        .render("Element heading = direction + (2 × random − 1) × value.")
        .cost("None.")
        .tests(ProcTests)
        .access(number(&BrushSettings::procAngleJitter));
    add("procCurl", "Curl", PropertyType::Number)
        .range(-1, 1, 100, "%")
        .engines(Lines)
        .since(11)
        .desc("Strands curl (a constant bend along their length) and grass blades droop (bending more towards the "
              "tip). The sign chooses the side.")
        .render("Strands: heading turns by value × π × (0.5 + random) over the length; grass: by value × π/2 × (0.5 + "
                "random) × t².")
        .cost("None (each element is a polyline of up to 16 capsules).")
        .tests(ProcTests)
        .access(number(&BrushSettings::procCurl));
    add("procTaper", "Element taper", PropertyType::Number)
        .percent()
        .engines(Lines)
        .since(11)
        .desc("How much strands and blades narrow towards their tips.")
        .render("Width at t = root width × (1 − value × t).")
        .cost("None.")
        .tests(ProcTests)
        .access(number(&BrushSettings::procTaper));
    add("procColorJitter", "Colour variation", PropertyType::Number)
        .percent()
        .engines(Elements)
        .since(11)
        .desc("Each element gets its own shade: brightness by up to ± half this amount and hue by up to ± 20° × this "
              "amount (paint only).")
        .render("Per element HSV shift of the dab colour; the stroke keeps a colour per pixel, mixed by coverage.")
        .cost("Per touched tile: 16 KiB of colour; per element: one colour conversion.")
        .tests(ProcTests)
        .access(number(&BrushSettings::procColorJitter));
    add("procSpread", "Spread", PropertyType::Number)
        .range(0, 2, 100, "% of radius")
        .engines(Elements)
        .since(11)
        .desc("How far from the stroke's centre line elements start: strands, leaves and drops over a disc, grass "
              "blades across the stroke.")
        .render("Root offset ≤ value × dab radius (disc: √random for an even spread; grass: across the path).")
        .cost("None.")
        .tests(ProcTests)
        .access(number(&BrushSettings::procSpread));
    add("procDirection", "Element direction", PropertyType::Choice)
        .options({"path", "up", "angle"}, {"Along the stroke", "Up", "Brush angle"})
        .engines(Elements | EnginePattern)
        .since(11)
        .desc("Which way elements point: along the stroke (plus the brush angle), towards the top of the canvas "
              "(grass grows up whichever way the stroke goes), or the brush angle alone.")
        .render("Base heading: stroke direction + angle, −90° + angle, or angle.")
        .cost("None.")
        .tests(ProcTests)
        .access(choice(&BrushSettings::procDirection, {"path", "up", "angle"}));
    add("procStretch", "Drop stretch", PropertyType::Number)
        .range(0, 4, 100, "%")
        .engines(EngineSplatter)
        .since(11)
        .desc("Splatter drops are drawn out along their direction, and large drops throw small satellite drops ahead "
              "of them.")
        .render("Drop ellipse length = radius × (1 + value × random); satellites at 1.6 to 3 × the drop length.")
        .cost("Per drop: an ellipse and up to three satellites.")
        .tests(ProcTests)
        .access(number(&BrushSettings::procStretch));
    add("patternShape", "Pattern", PropertyType::Choice)
        .options({"dots", "dashes", "chevrons", "zigzag", "scallops", "stitches"},
                 {"Dots", "Dashes", "Chevrons", "Zigzag", "Scallops", "Stitches"})
        .engines(EnginePattern)
        .basic()
        .since(11)
        .desc("The motif the Pattern dab type repeats along the stroke, turned to follow it. Zigzag and scallops join "
              "from dab to dab (their length is the spacing).")
        .render("Anti-aliased lines, arcs and dots in the dab's frame (along, across the stroke).")
        .cost("Per dab: one to three capsules or an arc of up to 12.")
        .tests(ProcTests)
        .access(choice(&BrushSettings::patternShape, {"dots", "dashes", "chevrons", "zigzag", "scallops", "stitches"}));
    add("hatchCrossPasses", "Cross-hatching passes", PropertyType::Integer)
        .range(0, 3, 1, "")
        .engines(EngineHatching)
        .since(11)
        .desc("Extra hatching directions that join in as pressure rises: across (90°), then the two diagonals. "
              "Without pressure data all passes draw.")
        .render("Pass k (1..n) at the hatching angle + 90°, + 45°, − 45° draws where pressure ≥ k / (n + 1); a pixel "
                "takes the strongest line.")
        .cost("Per pixel: one more distance per active pass.")
        .tests("tst_brushmedia::proceduralCrossHatching*")
        .access(integer(&BrushSettings::hatchCrossPasses));
    add("sprayDistribution", "Spray distribution", PropertyType::Choice)
        .options({"uniform", "gaussian"}, {"Even", "Centre-weighted"})
        .engines(EngineSpray)
        .since(11)
        .desc("Particles spread evenly over the dab, or more densely at its centre like an airbrush.")
        .render("Gaussian: radius from a truncated Rayleigh distribution, σ = radius / 2.5 (no particle outside the "
                "dab).")
        .cost("None.")
        .tests("tst_brushmedia::proceduralSprayDistribution*")
        .access(choice(&BrushSettings::sprayDistribution, {"uniform", "gaussian"}));

    // -------------------------------------------------------------------- Bristles (milestone 11)
    g = QStringLiteral("Bristles");
    const char *const BristleTests = "tst_brushmedia::bristle*";
    add("bristleCount", "Bristles", PropertyType::Integer)
        .range(1, 512, 1, "")
        .engines(EngineBristle)
        .basic()
        .since(11)
        .desc("How many bristles the brush has. Few bristles leave separate streaks; many fill the stroke.")
        .render("Bristles at seeded places in the footprint (the dab's size, roundness and angle).")
        .cost("Per dab: one anti-aliased segment per bristle (cost grows with bristles × segment area).")
        .tests(BristleTests)
        .access(integer(&BrushSettings::bristleCount));
    add("bristleThickness", "Bristle thickness", PropertyType::Number)
        .percent(0.005, 0.5)
        .engines(EngineBristle)
        .since(11)
        .desc("Width of each bristle as a share of the brush size (at least 0.8 px).")
        .render("Capsule width of each bristle's segment.")
        .cost("Grows with the width.")
        .tests(BristleTests)
        .access(number(&BrushSettings::bristleThickness));
    add("bristleSpread", "Spread with pressure", PropertyType::Number)
        .percent()
        .engines(EngineBristle)
        .since(11)
        .desc("How much pressure splays the bristles: light pressure gathers them into a narrow tip, full pressure "
              "spreads them over the whole footprint.")
        .render("Footprint × (1 − spread × (1 − pressure)).")
        .cost("None.")
        .needs("A pen that reports pressure")
        .tests(BristleTests)
        .access(number(&BrushSettings::bristleSpread));
    add("bristleLengthJitter", "Bristle lengths", PropertyType::Number)
        .percent()
        .engines(EngineBristle)
        .since(11)
        .desc("How different the bristles' lengths are: with light pressure only the longest touch the canvas.")
        .render("Each bristle touches once pressure passes its threshold (0 .. this value), rising over a 15 % range.")
        .cost("None.")
        .needs("A pen that reports pressure")
        .tests(BristleTests)
        .access(number(&BrushSettings::bristleLengthJitter));
    add("bristleLoad", "Paint load", PropertyType::Number)
        .percent()
        .engines(EngineBristle)
        .basic()
        .since(11)
        .desc("How much paint the bristles carry at the start of the stroke: low values give a dry brush that breaks "
              "up at once.")
        .render("Each bristle's reservoir starts at load × (0.75 .. 1).")
        .cost("None.")
        .tests(BristleTests)
        .access(number(&BrushSettings::bristleLoad));
    add("bristleDepletion", "Paint depletion", PropertyType::Number)
        .percent()
        .engines(EngineBristle)
        .since(11)
        .desc("How fast bristles run out of paint as they paint (each at its own rate), so a long stroke dries out "
              "into separate streaks.")
        .render("Reservoir × (1 − depletion × (0.5 .. 1.5))^(distance painted / 100 px); a drying bristle skips the "
                "paper's valleys first, then fades.")
        .cost("Per bristle and dab: one power.")
        .tests(BristleTests)
        .access(number(&BrushSettings::bristleDepletion));
    add("bristlePickup", "Colour pickup", PropertyType::Number)
        .percent()
        .engines(EngineBristle)
        .since(11)
        .desc("How fast each bristle takes on the colour of the paint it crosses (as it was before the stroke), so the "
              "stroke drags multicoloured streaks through existing paint.")
        .render("Per 10 px painted, the bristle colour moves this share towards the colour under it (× its alpha).")
        .cost("Per bristle and dab: one pixel read and one mix.")
        .tests(BristleTests)
        .access(number(&BrushSettings::bristlePickup));
    add("bristleWobble", "Bristle wobble", PropertyType::Number)
        .percent()
        .engines(EngineBristle)
        .since(11)
        .desc("Sideways jitter of each bristle from dab to dab, as a share of the space between bristles.")
        .render("Adds a seeded offset across the footprint per bristle and dab.")
        .cost("None.")
        .tests(BristleTests)
        .access(number(&BrushSettings::bristleWobble));

    // ------------------------------------------------------------------- Dry media (milestone 11)
    g = QStringLiteral("Dry Media");
    const char *const DryTests = "tst_brushmedia::dry*";
    add("dryDepth", "Penetration", PropertyType::Number)
        .percent()
        .engines(EngineDry)
        .basic()
        .since(11)
        .desc("How deep into the paper's tooth the medium reaches at full pressure: low values touch only the peaks "
              "(a light, grainy mark), high values fill the valleys too (a dense mark).")
        .render("Per pixel of each dab: tip × smoothstep((depth − (1 − paper height)) / softness); the paper is the "
                "texture in document space, so repeated strokes meet the same tooth.")
        .cost("Per pixel of each dab: one paper sample.")
        .tests(DryTests)
        .access(number(&BrushSettings::dryDepth));
    add("pressureDepth", "Pressure → penetration", PropertyType::Pressure)
        .engines(EngineDry)
        .sensors("pressure")
        .since(11)
        .desc("How pressure changes the penetration: depth × (minimum + (1 − minimum) × pressure^firmness). A pen without "
              "pressure, mouse and touch draw at full depth.")
        .render("Scales the dab's penetration before the paper comparison.")
        .cost("Per dab: one power.")
        .needs("A pen that reports pressure")
        .tests(DryTests)
        .access(pressure(&BrushSettings::pressureDepth));
    add("drySoftness", "Softness", PropertyType::Number)
        .percent(0.01, 0.5)
        .engines(EngineDry)
        .since(11)
        .desc("Width of the transition between touched and untouched tooth: low values give a crisp, hard-lead grain, "
              "high values a soft, smudgy one.")
        .render("Divides the depth difference before the smoothstep.")
        .cost("None.")
        .tests(DryTests)
        .access(number(&BrushSettings::drySoftness));
    add("dryStreaks", "Streaks", PropertyType::Number)
        .percent()
        .engines(EngineDry)
        .since(11)
        .desc("Grain that runs along the stroke, as charcoal and pastel leave it; it turns with the stroke direction.")
        .render("The paper height is mixed with a streak field sampled in the dab's frame (along the heading).")
        .cost("Per pixel of each dab: one bilinear sample of the streak field.")
        .tests(DryTests)
        .access(number(&BrushSettings::dryStreaks));
    add("dryStreakLength", "Streak length", PropertyType::Number)
        .range(1, 16, 1, "×")
        .engines(EngineDry)
        .since(11)
        .desc("How elongated the streaks are along the stroke (1 = round grain).")
        .render("Stretches the streak field along the heading.")
        .cost("None.")
        .needs("Streaks above 0")
        .tests(DryTests)
        .access(number(&BrushSettings::dryStreakLength));
    add("dryStreakScale", "Streak size", PropertyType::Number)
        .range(0.25, 4, 100, "%")
        .engines(EngineDry)
        .since(11)
        .desc("Size of the streak grain.")
        .render("Divides the streak field coordinates.")
        .cost("None.")
        .needs("Streaks above 0")
        .tests(DryTests)
        .access(number(&BrushSettings::dryStreakScale));
    add("dryEdgeBreakup", "Edge breakup", PropertyType::Number)
        .percent()
        .engines(EngineDry)
        .since(11)
        .desc("Makes the rim of the mark crumble, as charcoal and chalk do, instead of fading evenly.")
        .render("Lowers the penetration towards the dab's rim by a document-space noise term.")
        .cost("Per pixel of each dab: one extra paper sample near the rim.")
        .tests(DryTests)
        .access(number(&BrushSettings::dryEdgeBreakup));
    add("dryGrit", "Grit", PropertyType::Number)
        .percent(0, 0.9)
        .engines(EngineDry)
        .since(11)
        .desc("Pigment that skips: a share of pixels the stroke leaves bare, different for each stroke (seeded).")
        .render("Pixels whose per-stroke hash is below the grit keep only a fifth of the deposit.")
        .cost("Per pixel of each dab: one hash.")
        .tests(DryTests)
        .access(number(&BrushSettings::dryGrit));
    add("dryTiltDepth", "Tilt → penetration", PropertyType::Number)
        .percent()
        .engines(EngineDry)
        .sensors("tilt")
        .since(11)
        .desc("Penetration at full tilt, as a share of the upright value: shading with the side of the lead touches only "
              "the peaks.")
        .render("Penetration × (1 + (value − 1) × tilt amount).")
        .cost("None.")
        .needs("A pen that reports tilt")
        .tests(DryTests)
        .access(number(&BrushSettings::dryTiltDepth));

    std::vector<BrushProperty> out;
    out.reserve(e.size());
    for (Entry &x : e)
        out.push_back(std::move(x.p));
    return out;
}

} // namespace

QVariant BrushProperty::defaultValue() const
{
    return get ? get(BrushSettings()) : QVariant();
}

QStringList BrushProperties::designGroups()
{
    return {QStringLiteral("Brush Tip"),         QStringLiteral("Stroke Path"),     QStringLiteral("Pressure and Tilt"),
            QStringLiteral("Opacity and Flow"),  QStringLiteral("Texture and Grain"), QStringLiteral("Shape Dynamics"),
            QStringLiteral("Color Dynamics"),    QStringLiteral("Blending and Smudge"), QStringLiteral("Wet Paint"),
            QStringLiteral("Stabilization"),     QStringLiteral("Taper"),           QStringLiteral("Special Effects")};
}

QStringList BrushProperties::engineGroups()
{
    return {QStringLiteral("Ink and Stroke"), QStringLiteral("Dry Media"), QStringLiteral("Mixing"), QStringLiteral("Bristles"),
            QStringLiteral("Watercolour"), QStringLiteral("Procedural")};
}

QStringList BrushProperties::groups()
{
    return designGroups() + engineGroups() + QStringList{QStringLiteral("Engine")};
}

const std::vector<BrushProperty> &BrushProperties::all()
{
    static const std::vector<BrushProperty> properties = build();
    return properties;
}

const BrushProperty *BrushProperties::find(const QString &id)
{
    for (const BrushProperty &p : all())
        if (p.id == id)
            return &p;
    return nullptr;
}

int BrushProperties::implementedCount()
{
    return int(std::count_if(all().begin(), all().end(),
                             [](const BrushProperty &p) { return p.implemented && p.group != QLatin1String("Engine"); }));
}

int BrushProperties::designTargetCount()
{
    const QStringList design = designGroups();
    return int(std::count_if(all().begin(), all().end(), [&](const BrushProperty &p) { return design.contains(p.group); }));
}

int BrushProperties::engineGroupCount()
{
    const QStringList engines = engineGroups();
    return int(std::count_if(all().begin(), all().end(), [&](const BrushProperty &p) { return engines.contains(p.group); }));
}

unsigned BrushProperties::engineOf(const BrushSettings &s)
{
    switch (s.blend) {
    case BrushBlend::Smudge: return EngineSmudge;
    case BrushBlend::WetPaint: return EngineWet;
    case BrushBlend::Watercolor: return EngineWatercolor;
    default: break;
    }
    if (s.stamp == BrushStamp::Spray)
        return EngineSpray;
    if (s.stamp == BrushStamp::Hatching)
        return EngineHatching;
    if (s.stamp == BrushStamp::Dry)
        return EngineDry;
    if (s.stamp == BrushStamp::Bristle)
        return EngineBristle;
    switch (s.stamp) {
    case BrushStamp::Strands: return EngineStrands;
    case BrushStamp::Grass: return EngineGrass;
    case BrushStamp::Foliage: return EngineFoliage;
    case BrushStamp::Splatter: return EngineSplatter;
    case BrushStamp::Pattern: return EnginePattern;
    default: break;
    }
    return s.blend == BrushBlend::Erase ? EngineErase : EnginePaint;
}

QString BrushProperties::engineName(unsigned engine)
{
    static const char *names[] = {"paint", "erase", "smudge", "wet paint", "spray", "hatching", "dry media",
                                  "bristles", "watercolour", "strands", "grass", "foliage", "splatter", "pattern"};
    for (int i = 0; i < 14; ++i)
        if (engine == (1u << i))
            return QLatin1String(names[i]);
    return QStringLiteral("engines %1").arg(engine);
}

bool BrushProperties::appliesTo(const BrushProperty &property, const BrushSettings &settings)
{
    return property.implemented && (property.engines & engineOf(settings));
}

QString BrushProperties::encodeImage(const QImage &image)
{
    if (image.isNull())
        return QString();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.convertToFormat(QImage::Format_Grayscale8).save(&buffer, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

QImage BrushProperties::decodeImage(const QString &text, bool *ok)
{
    *ok = true;
    if (text.isEmpty())
        return QImage();
    const QByteArray bytes = QByteArray::fromBase64(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    // Bounded: an embedded tip is at most 256 × 256; refuse anything that claims to be huge.
    QImage image;
    if (bytes.isEmpty() || bytes.size() > 1024 * 1024 || !image.loadFromData(bytes, "PNG")
        || image.width() > 4096 || image.height() > 4096) {
        *ok = false;
        return QImage();
    }
    return image;
}

QImage BrushProperties::tipFromImage(const QImage &source)
{
    if (source.isNull() || source.width() < 1 || source.height() < 1)
        return QImage();
    // Square canvas around the image, scaled to at most MaxTipImageSize.
    const int side = std::max(source.width(), source.height());
    const int target = std::min(side, BrushSettings::MaxTipImageSize);
    const bool alpha = source.hasAlphaChannel() && [&] {
        const QImage a = source.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < a.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(a.constScanLine(y));
            for (int x = 0; x < a.width(); ++x)
                if (qAlpha(row[x]) < 250)
                    return true;
        }
        return false;
    }();
    QImage gray(source.size(), QImage::Format_Grayscale8);
    const QImage argb = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb *in = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        uchar *out = gray.scanLine(y);
        for (int x = 0; x < argb.width(); ++x)
            out[x] = uchar(alpha ? qAlpha(in[x]) : 255 - qGray(in[x]));
    }
    QImage square(side, side, QImage::Format_Grayscale8);
    square.fill(0);
    {
        QPainter p(&square);
        p.drawImage((side - gray.width()) / 2, (side - gray.height()) / 2, gray);
    }
    if (target != side)
        square = square.scaled(target, target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .convertToFormat(QImage::Format_Grayscale8);
    return square;
}

QString BrushProperties::specificationMarkdown()
{
    auto typeName = [](PropertyType t) {
        switch (t) {
        case PropertyType::Number: return QStringLiteral("number");
        case PropertyType::Integer: return QStringLiteral("integer");
        case PropertyType::Bool: return QStringLiteral("boolean");
        case PropertyType::Choice: return QStringLiteral("option");
        case PropertyType::Text: return QStringLiteral("text");
        case PropertyType::Image: return QStringLiteral("image");
        case PropertyType::Pressure: return QStringLiteral("pressure response");
        case PropertyType::Dynamics: return QStringLiteral("dynamics");
        }
        return QString();
    };
    auto serialisation = [&](const BrushProperty &p) {
        const QString key = p.id.startsWith(QLatin1String("dynamics."))
            ? QStringLiteral("`dynamics` → `%1`").arg(p.id.mid(9))
            : QStringLiteral("`%1`").arg(p.id);
        switch (p.type) {
        case PropertyType::Number: return key + QStringLiteral(": JSON number");
        case PropertyType::Integer: return key + QStringLiteral(": JSON integer");
        case PropertyType::Bool: return key + QStringLiteral(": true/false");
        case PropertyType::Choice: {
            QStringList quoted;
            for (const QString &c : p.choices)
                quoted << (c.isEmpty() ? QStringLiteral("\"\"") : QStringLiteral("\"%1\"").arg(c));
            return key + QStringLiteral(": one of ") + quoted.join(QStringLiteral(", "));
        }
        case PropertyType::Text: return key + QStringLiteral(": string");
        case PropertyType::Image: return key + QStringLiteral(": base64 PNG (Grayscale8), \"\" for none");
        case PropertyType::Pressure: return key + QStringLiteral(": {enabled, minimum, gamma}");
        case PropertyType::Dynamics: return key + QStringLiteral(": {enabled, minimum, combine, sensors: [{sensor, curve, range}]}");
        }
        return key;
    };
    auto engineNames = [](unsigned e) {
        if (e == EngineAll)
            return QStringLiteral("all");
        QStringList n;
        const char *names[] = {"paint", "erase", "smudge", "wet paint", "spray", "hatching", "dry media",
                               "bristles", "watercolour", "strands", "grass", "foliage", "splatter", "pattern"};
        for (int i = 0; i < 14; ++i)
            if (e & (1u << i))
                n << QLatin1String(names[i]);
        return n.join(QStringLiteral(", "));
    };
    auto shown = [](const BrushProperty &p, const QVariant &v) -> QString {
        switch (p.type) {
        case PropertyType::Number:
        case PropertyType::Integer: {
            const double d = v.toDouble() * p.displayScale;
            return QString::number(d, 'g', 6) + (p.unit.isEmpty() ? QString() : QStringLiteral(" ") + p.unit);
        }
        case PropertyType::Bool: return v.toBool() ? QStringLiteral("on") : QStringLiteral("off");
        case PropertyType::Choice: {
            const int i = p.choices.indexOf(v.toString());
            return i >= 0 ? p.choiceLabels.value(i) : v.toString();
        }
        case PropertyType::Image: return v.toString().isEmpty() ? QStringLiteral("none") : QStringLiteral("(image)");
        case PropertyType::Pressure:
        case PropertyType::Dynamics:
            return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(v.toMap())).toJson(QJsonDocument::Compact));
        default: return v.toString().isEmpty() ? QStringLiteral("\"\"") : v.toString();
        }
    };
    auto rangeText = [&](const BrushProperty &p) -> QString {
        if (p.type == PropertyType::Number || p.type == PropertyType::Integer)
            return QStringLiteral("%1 … %2%3")
                .arg(p.minimum * p.displayScale, 0, 'g', 6)
                .arg(p.maximum * p.displayScale, 0, 'g', 6)
                .arg(p.unit.isEmpty() ? QString() : QStringLiteral(" ") + p.unit);
        if (p.type == PropertyType::Choice)
            return p.choiceLabels.join(QStringLiteral(" / "));
        if (p.type == PropertyType::Pressure)
            return QStringLiteral("minimum 0 … 100 %, firmness (gamma) 0.33 … 3");
        if (p.type == PropertyType::Dynamics)
            return QStringLiteral("minimum 0 … 100 %, up to 4 sensors, curves of 2 … 16 points in 0 … 1");
        if (p.type == PropertyType::Image)
            return QStringLiteral("square, up to 256 × 256 px");
        if (p.type == PropertyType::Bool)
            return QStringLiteral("on / off");
        return QStringLiteral("—");
    };

    QString md;
    md += QStringLiteral("# Brush settings specification\n\n");
    md += QStringLiteral("Generated from the property registry (`src/core/BrushProperties.cpp`) by "
                         "`tst_brushengine::specificationIsCurrent` (run with `DRAWAPP_WRITE_SPEC=1` to rewrite it). Do not "
                         "edit by hand.\n\n");
    md += QStringLiteral("A property counts as **implemented** only if it changes rendering: "
                         "`tst_brushengine::everyImplementedPropertyChangesRendering` renders a standard stroke with the "
                         "default and a changed value of every implemented property, on an engine it applies to, and "
                         "requires different pixels. **Planned** properties have no control and no effect; they are "
                         "the remaining part of the 120-property design target.\n\n");
    int total = 0, implemented = 0;
    QString summary = QStringLiteral("| Group | Implemented | Planned | Total |\n|---|---:|---:|---:|\n");
    for (const QString &group : groups()) {
        if (group == QLatin1String("Engine"))
            continue;
        int n = 0, done = 0;
        for (const BrushProperty &p : all())
            if (p.group == group) {
                ++n;
                done += p.implemented ? 1 : 0;
            }
        total += n;
        implemented += done;
        summary += QStringLiteral("| %1 | %2 | %3 | %4 |\n").arg(group).arg(done).arg(n - done).arg(n);
    }
    summary += QStringLiteral("| **Total** | **%1** | **%2** | **%3** |\n\n").arg(implemented).arg(total - implemented).arg(total);
    md += QStringLiteral("## Summary\n\n") + summary;
    md += QStringLiteral("The two engine selectors (`blend`, `stamp`) are listed under Engine and not counted.\n\n");
    md += QStringLiteral("Common rules: values are clamped into their range on load and when set (NaN becomes the "
                         "default); every random choice comes from the stroke seed and the dab index, so the same "
                         "input and seed always give the same pixels; no sensor value is invented when the device "
                         "does not report it (see `docs/brush-engine-architecture.md`).\n\n");
    for (const QString &group : groups()) {
        md += QStringLiteral("## %1\n\n").arg(group);
        for (const BrushProperty &p : all()) {
            if (p.group != group)
                continue;
            md += QStringLiteral("### %1 — `%2`%3\n\n")
                      .arg(p.name, p.id, p.implemented ? QString() : QStringLiteral(" (planned)"));
            md += p.description + QStringLiteral("\n\n");
            if (!p.implemented) {
                md += QStringLiteral("- Status: **planned, not implemented**\n- Type: %1\n\n").arg(typeName(p.type));
                continue;
            }
            md += QStringLiteral("- Status: implemented%1\n").arg(p.advanced ? QStringLiteral(" (Brush Studio)")
                                                                              : QStringLiteral(" (Brush Studio and quick panel)"));
            md += QStringLiteral("- Type: %1\n").arg(typeName(p.type));
            md += QStringLiteral("- Range: %1\n").arg(rangeText(p));
            md += QStringLiteral("- Default: %1\n").arg(shown(p, p.defaultValue()));
            md += QStringLiteral("- Engines: %1\n").arg(engineNames(p.engines));
            md += QStringLiteral("- Sensors: %1\n").arg(p.sensors);
            if (!p.requires.isEmpty())
                md += QStringLiteral("- Has an effect with: %1\n").arg(p.requires);
            md += QStringLiteral("- Serialisation: %1\n").arg(serialisation(p));
            md += QStringLiteral("- Rendering: %1\n").arg(p.rendering);
            md += QStringLiteral("- Cost: %1\n").arg(p.cost);
            md += QStringLiteral("- Tests: %1\n\n").arg(p.tests.isEmpty() ? QLatin1String(EffectTest) : p.tests);
        }
    }
    return md;
}

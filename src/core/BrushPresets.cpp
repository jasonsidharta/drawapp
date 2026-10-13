#include "BrushPresets.h"

namespace {

BrushPreset preset(const char *id, const char *name, BrushSettings s)
{
    s.name = QString::fromLatin1(name);
    return BrushPreset{QStringLiteral("builtin.") + QLatin1String(id), s.sanitized(), true};
}

std::vector<BrushPreset> makeBuiltIns()
{
    std::vector<BrushPreset> list;

    // Graphite-like: small, slightly soft, builds up within a stroke, and light pressure catches
    // only the tops of the paper tooth (Subtract texture) so shading stays grainy until pressed.
    {
        BrushSettings s;
        s.size = 5;
        s.hardness = 0.55;
        s.spacing = 0.08;
        s.opacity = 0.95;
        s.flow = 0.8;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {true, 0.55, 1.0};
        s.pressureOpacity = {true, 0.12, 1.25};
        s.tiltSize = 2.5; // a tilted pencil shades with its side, where the pen reports tilt
        s.tiltOpacity = 0.6;
        s.texture = QStringLiteral("paper");
        s.textureStrength = 0.45;
        s.textureMode = TextureMode::Subtract;
        s.smoothing = 10;
        list.push_back(preset("sketch-pencil", "Sketch Pencil", s));
    }
    // Crisp, anti-aliased, pressure-responsive line. Ends where the pen ends: no forced taper.
    {
        BrushSettings s;
        s.size = 8;
        s.hardness = 1.0;
        s.spacing = 0.06;
        s.pressureSize = {true, 0.12, 1.3};
        s.smoothing = 30;
        list.push_back(preset("clean-ink", "Clean Ink", s));
    }
    // Flat, even colour for blocking in: the milestone 5 default brush.
    {
        BrushSettings s;
        s.size = 12;
        list.push_back(preset("solid-round", "Solid Round", s));
    }
    // Wide soft falloff, builds up with time while the pen is held, pressure controls flow.
    {
        BrushSettings s;
        s.size = 90;
        s.hardness = 0.0;
        s.spacing = 0.05;
        s.flow = 0.25;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {false, 0.0, 1.0};
        s.pressureFlow = {true, 0.0, 1.0};
        s.airbrush = true;
        s.airbrushRate = 40;
        list.push_back(preset("soft-airbrush", "Soft Airbrush", s));
    }
    // Flat bristle tip that turns with the stroke (or the pen's tilt), on canvas grain.
    {
        BrushSettings s;
        s.tip = QStringLiteral("bristle");
        s.size = 40;
        s.followDirection = true;
        s.angleJitter = 2;
        s.spacing = 0.04;
        s.flow = 0.9;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {true, 0.55, 1.0};
        s.pressureFlow = {true, 0.45, 1.0};
        s.texture = QStringLiteral("canvas");
        s.textureStrength = 0.14; // stronger reads as a mesh rather than grain (brush sheet)
        s.smoothing = 15;
        list.push_back(preset("textured-paint", "Textured Paint", s));
    }
    // Pushes and blends the colours already on the layer. Not a paint simulation.
    {
        BrushSettings s;
        s.blend = BrushBlend::Smudge;
        s.size = 36;
        s.hardness = 0.35;
        s.spacing = 0.06;
        s.pressureSize = {true, 0.6, 1.0};
        s.pressureOpacity = {true, 0.15, 1.0}; // pressure sets the smudge strength
        s.smudgeStrength = 0.7;
        s.smudgePickup = 0.35;
        list.push_back(preset("blend-smudge", "Blend / Smudge", s));
    }
    {
        BrushSettings s;
        s.blend = BrushBlend::Erase;
        s.size = 24;
        s.pressureSize = {true, 0.3, 1.0};
        list.push_back(preset("hard-eraser", "Hard Eraser", s));
    }
    {
        BrushSettings s;
        s.blend = BrushBlend::Erase;
        s.size = 70;
        s.hardness = 0.0;
        s.spacing = 0.06;
        s.flow = 0.4;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {false, 0.0, 1.0};
        s.pressureFlow = {true, 0.1, 1.0};
        list.push_back(preset("soft-eraser", "Soft Eraser", s));
    }

    // --- Milestone 11: dry media (stamp Dry, DryDeposit) ---
    // Graphite: the lead's grade sets how dark it can go (opacity), how deep into the tooth it
    // reaches (penetration) and how crisp its grain is (softness). Pressure deepens the mark; the
    // side of a tilted pencil shades only the peaks.
    auto graphite = [](double size, double opacity, double depth, double softness, double tooth) {
        BrushSettings s;
        s.stamp = BrushStamp::Dry;
        s.size = size;
        s.hardness = 0.6;
        s.spacing = 0.07;
        s.opacity = opacity;
        s.flow = 0.55;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {true, 0.6, 1.0};
        s.pressureOpacity = {true, 0.35, 1.2};
        s.texture = QStringLiteral("paper");
        s.textureStrength = tooth;
        s.dryDepth = depth;
        s.pressureDepth = {true, 0.15, 1.1};
        s.drySoftness = softness;
        s.tiltSize = 2.5;
        s.dryTiltDepth = 0.55;
        s.smoothing = 10;
        return s;
    };
    list.push_back(preset("graphite-2h", "Graphite 2H", graphite(3.5, 0.6, 0.7, 0.12, 0.8)));
    list.push_back(preset("graphite-hb", "Graphite HB", graphite(4.5, 0.85, 0.78, 0.14, 0.8)));
    {
        BrushSettings s = graphite(7, 0.98, 0.82, 0.18, 0.75);
        s.hardness = 0.4;
        s.flow = 0.65;
        list.push_back(preset("graphite-6b", "Graphite 6B", s));
    }
    // Charcoal: streaks along the stroke, a crumbling rim and grit, on rough paper.
    {
        BrushSettings s;
        s.stamp = BrushStamp::Dry;
        s.size = 14;
        s.hardness = 0.25;
        s.roundness = 0.7;
        s.followDirection = true;
        s.spacing = 0.06;
        s.opacity = 0.85;
        s.flow = 0.5;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {true, 0.5, 1.0};
        s.pressureOpacity = {true, 0.3, 1.0};
        s.texture = QStringLiteral("rough");
        s.textureStrength = 0.85;
        s.dryDepth = 0.68;
        s.drySoftness = 0.15;
        s.dryStreaks = 0.45;
        s.dryStreakLength = 7;
        s.dryEdgeBreakup = 0.5;
        s.dryGrit = 0.08;
        s.tiltSize = 2.0;
        s.dryTiltDepth = 0.6;
        list.push_back(preset("vine-charcoal", "Vine Charcoal", s));
        s.size = 16;
        s.hardness = 0.5;
        s.opacity = 1.0;
        s.flow = 0.7;
        s.textureStrength = 0.75;
        s.dryDepth = 0.8;
        s.drySoftness = 0.1;
        s.dryStreaks = 0.3;
        s.dryEdgeBreakup = 0.35;
        s.dryGrit = 0.05;
        list.push_back(preset("compressed-charcoal", "Compressed Charcoal", s));
    }
    // Chalk and pastel: broad, flattened sticks; the paper shows through, the rim breaks up.
    {
        BrushSettings s;
        s.stamp = BrushStamp::Dry;
        s.size = 30;
        s.hardness = 0.35;
        s.roundness = 0.6;
        s.followDirection = true;
        s.spacing = 0.06;
        s.opacity = 0.95;
        s.flow = 0.6;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {true, 0.55, 1.0};
        s.texture = QStringLiteral("rough");
        s.textureStrength = 0.9;
        s.dryDepth = 0.62;
        s.drySoftness = 0.12;
        s.dryStreaks = 0.25;
        s.dryStreakLength = 4;
        s.dryEdgeBreakup = 0.5;
        s.dryGrit = 0.08;
        s.dryTiltDepth = 0.6;
        list.push_back(preset("chalk", "Chalk", s));
        s.size = 30;
        s.hardness = 0.3;
        s.roundness = 0.55;
        s.opacity = 1.0;
        s.flow = 0.75;
        s.texture = QStringLiteral("laid");
        s.textureStrength = 0.85;
        s.dryDepth = 0.68;
        s.drySoftness = 0.15;
        s.dryStreaks = 0.35;
        s.dryStreakLength = 5;
        s.dryEdgeBreakup = 0.45;
        s.dryGrit = 0.06;
        list.push_back(preset("soft-pastel", "Soft Pastel", s));
    }

    // --- Milestone 11: ink (curve interpolation, corners, live taper, speed-adaptive smoothing) ---
    // Speed thins the line (as a dip pen does when it moves fast); the curve keeps fast curves round.
    auto speedThins = [](double minimum) {
        ParameterDynamics d;
        d.enabled = true;
        d.minimum = minimum;
        d.mappings.push_back({Sensor::Speed, SensorCurve::inverted(), 3.0});
        return d;
    };
    {
        BrushSettings s;
        s.size = 10;
        s.hardness = 1.0;
        s.spacing = 0.05;
        s.pressureSize = {true, 0.04, 1.5};
        s.curveInterpolation = true;
        s.taperStart = 25;
        s.taperEnd = 45;
        s.taperStartSize = 0.05;
        s.taperEndSize = 0.05;
        s.taperLive = true;
        s.smoothing = 30;
        s.smoothingSpeedAdapt = 0.6;
        s.dynamicsFor(DynamicTarget::Size) = speedThins(0.65);
        list.push_back(preset("g-pen", "G-Pen", s));
    }
    {
        BrushSettings s;
        s.size = 4;
        s.hardness = 1.0;
        s.spacing = 0.05;
        s.pressureSize = {false, 0.0, 1.0};
        s.curveInterpolation = true;
        s.taperStart = 4;
        s.taperEnd = 6;
        s.taperStartSize = 0.4;
        s.taperEndSize = 0.4;
        s.taperLive = true;
        s.smoothing = 25;
        s.smoothingSpeedAdapt = 0.5;
        list.push_back(preset("fine-liner", "Fine Liner", s));
    }
    {
        BrushSettings s;
        s.size = 18;
        s.hardness = 0.9;
        s.roundness = 0.8;
        s.followDirection = true;
        s.spacing = 0.04;
        s.pressureSize = {true, 0.03, 1.2};
        s.curveInterpolation = true;
        s.taperStart = 60;
        s.taperEnd = 90;
        s.taperStartSize = 0.03;
        s.taperEndSize = 0.03;
        s.taperLive = true;
        s.smoothing = 35;
        s.smoothingSpeedAdapt = 0.5;
        s.dynamicsFor(DynamicTarget::Size) = speedThins(0.5);
        list.push_back(preset("brush-pen", "Brush Pen", s));
    }
    // Dry ink: the pen skips at the rim on rough paper (dry-media stamp, deep penetration).
    {
        BrushSettings s;
        s.stamp = BrushStamp::Dry;
        s.size = 9;
        s.hardness = 1.0;
        s.spacing = 0.05;
        s.pressureSize = {true, 0.1, 1.3};
        s.texture = QStringLiteral("rough");
        s.textureStrength = 0.85;
        s.dryDepth = 0.82;
        s.pressureDepth = {true, 0.45, 1.0};
        s.drySoftness = 0.05;
        s.dryEdgeBreakup = 1.0;
        s.curveInterpolation = true;
        s.taperEnd = 20;
        s.taperLive = true;
        s.smoothing = 20;
        list.push_back(preset("dry-ink", "Dry Ink", s));
    }

    // --- Milestone 11: smudge and colour mixing (CarriedDeposit, mix mode) ---
    // Blends what is on the canvas like paint, adding none of its own.
    {
        BrushSettings s;
        s.blend = BrushBlend::Smudge;
        s.smudgeMode = SmudgeMode::Mix;
        s.size = 40;
        s.hardness = 0.4;
        s.spacing = 0.06;
        s.pressureSize = {true, 0.6, 1.0};
        s.pressureOpacity = {true, 0.2, 1.0}; // pressure sets how firmly the mix is laid down
        s.smudgeStrength = 0.8;
        s.smudgeLength = 0.8;
        s.smudgeRadius = 0.6;
        s.pigmentMixing = true;
        list.push_back(preset("oil-blender", "Oil Blender", s));
    }
    // One tool for painting and blending: a light touch blends the canvas, pressure adds the brush
    // colour (colour rate follows pressure), mixed like paint.
    {
        BrushSettings s;
        s.blend = BrushBlend::Smudge;
        s.smudgeMode = SmudgeMode::Mix;
        s.size = 32;
        s.hardness = 0.55;
        s.roundness = 0.8;
        s.followDirection = true;
        s.spacing = 0.05;
        s.pressureSize = {true, 0.5, 1.0};
        s.smudgeStrength = 0.9;
        s.smudgeLength = 0.6;
        s.smudgeRadius = 0.7;
        s.smudgeColorRate = 0.8;
        s.smudgeDepletion = 0.05;
        s.pigmentMixing = true;
        ParameterDynamics d;
        d.enabled = true;
        d.minimum = 0.0;
        // Firm: below about a third of the pressure range almost no paint joins (0.3³ ≈ 3 %).
        d.mappings.push_back({Sensor::Pressure, SensorCurve::exponential(3.0), 0});
        s.dynamicsFor(DynamicTarget::ColorRate) = d;
        list.push_back(preset("color-mixer", "Colour Mixer", s));
    }
    // A flat knife that drags the paint as it is (smear), pixel by pixel.
    {
        BrushSettings s;
        s.blend = BrushBlend::Smudge;
        s.smudgeMode = SmudgeMode::Smear;
        s.size = 36;
        s.hardness = 0.9;
        s.roundness = 0.25;
        s.angle = 90; // the broad edge across the stroke, as a palette knife is dragged
        s.followDirection = true;
        s.spacing = 0.04;
        s.pressureSize = {false, 0.0, 1.0};
        s.smudgeStrength = 0.95;
        s.smudgePickup = 0.08;
        list.push_back(preset("smear-knife", "Smear Knife", s));
    }

    // --- Milestone 11: bristle brushes (BristleDeposit) ---
    // Round oil brush: loaded, splays with pressure, picks up some of the paint it crosses.
    {
        BrushSettings s;
        s.stamp = BrushStamp::Bristle;
        s.size = 34;
        s.roundness = 0.9;
        s.spacing = 0.05;
        s.flow = 0.85;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {true, 0.55, 1.0};
        s.bristleCount = 60;
        s.bristleThickness = 0.045;
        s.bristleSpread = 0.5;
        s.bristleLengthJitter = 0.5;
        s.bristleLoad = 1.0;
        s.bristleDepletion = 0.15;
        s.bristlePickup = 0.25;
        s.pigmentMixing = true;
        s.smoothing = 15;
        list.push_back(preset("round-bristle", "Round Bristle", s));
    }
    // Flat brush: a thin footprint turned across the stroke, streaky, runs dry on long strokes.
    {
        BrushSettings s;
        s.stamp = BrushStamp::Bristle;
        s.size = 44;
        s.roundness = 0.18;
        s.angle = 90;
        s.followDirection = true;
        s.spacing = 0.04;
        s.flow = 0.9;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {true, 0.7, 1.0};
        s.bristleCount = 40;
        s.bristleThickness = 0.035;
        s.bristleSpread = 0.2;
        s.bristleLengthJitter = 0.6;
        s.bristleLoad = 0.9;
        s.bristleDepletion = 0.3;
        s.bristlePickup = 0.3;
        s.pigmentMixing = true;
        s.smoothing = 15;
        list.push_back(preset("flat-bristle", "Flat Bristle", s));
    }
    // Dry brush: little paint, skips over canvas grain from the start.
    {
        BrushSettings s;
        s.stamp = BrushStamp::Bristle;
        s.size = 48;
        s.roundness = 0.35;
        s.angle = 90;
        s.followDirection = true;
        s.spacing = 0.04;
        s.flow = 0.8;
        s.accumulation = Accumulation::BuildUp;
        s.pressureSize = {true, 0.7, 1.0};
        s.texture = QStringLiteral("rough");
        s.textureStrength = 0.6;
        s.bristleCount = 90;
        s.bristleThickness = 0.025;
        s.bristleSpread = 0.15;
        s.bristleLengthJitter = 0.7;
        s.bristleLoad = 0.55;
        s.bristleDepletion = 0.35;
        list.push_back(preset("dry-brush", "Dry Brush", s));
    }

    // --- Milestone 11: watercolour (WetLayer; a bounded approximation, not a fluid simulation) ---
    // A soft round wash brush: wet, spreads, darkens at its edges, granulates on rough paper.
    {
        BrushSettings s;
        s.blend = BrushBlend::Watercolor;
        s.size = 50;
        s.hardness = 0.3;
        s.spacing = 0.06;
        s.flow = 0.7;
        s.pressureSize = {true, 0.4, 1.0};
        s.pressureOpacity = {true, 0.3, 1.0};
        s.texture = QStringLiteral("rough");
        s.textureStrength = 0.7;
        s.wcWater = 0.7;
        s.wcPigment = 0.45;
        s.wcSpread = 0.55;
        s.wcEdgeDarkening = 0.55;
        s.wcGranulation = 0.35;
        s.wcDryingTime = 8;
        s.wcLifting = 0.15;
        list.push_back(preset("watercolor-wash", "Watercolour Wash", s));
    }
    // A pointed round for detail: less water, more pigment, crisper edges.
    {
        BrushSettings s;
        s.blend = BrushBlend::Watercolor;
        s.size = 16;
        s.hardness = 0.6;
        s.spacing = 0.05;
        s.flow = 0.8;
        s.pressureSize = {true, 0.15, 1.0};
        s.texture = QStringLiteral("rough");
        s.textureStrength = 0.6;
        s.wcWater = 0.4;
        s.wcPigment = 0.7;
        s.wcSpread = 0.3;
        s.wcEdgeDarkening = 0.4;
        s.wcGranulation = 0.2;
        s.wcDryingTime = 4;
        s.wcLifting = 0.1;
        s.taperStart = 15;
        s.taperEnd = 25;
        list.push_back(preset("watercolor-round", "Watercolour Round", s));
    }
    return list;
}

} // namespace

const std::vector<BrushPreset> &BrushPresets::builtIns()
{
    static const std::vector<BrushPreset> list = makeBuiltIns();
    return list;
}

const BrushPreset *BrushPresets::builtIn(const QString &id)
{
    for (const BrushPreset &p : builtIns())
        if (p.id == id)
            return &p;
    return nullptr;
}

QString BrushPresets::defaultPaintId()
{
    return QStringLiteral("builtin.solid-round");
}

QString BrushPresets::defaultEraserId()
{
    return QStringLiteral("builtin.hard-eraser");
}

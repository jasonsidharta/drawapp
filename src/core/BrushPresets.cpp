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
        s.textureStrength = 0.25;
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

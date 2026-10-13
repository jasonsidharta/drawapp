#pragma once

#include "StrokeSurface.h"

#include <QImage>
#include <QRect>
#include <functional>
#include <map>
#include <memory>

class TileSnapshot;

namespace BrushTips {
struct Texture;
}

// Watercolour (milestone 11; research/m11-brush-architecture.md, A7; research/m11-paint-simulation.md
// §3). A bounded, tile-local cellular approximation of watery paint, **not** a shallow-water fluid
// solver (Curtis et al. 1997 and later work simulate the fluid; this model reproduces the
// recognisable effects with simple rules, on the CPU, only where the paper is wet).
//
// State, per 64 × 64 tile that has been wet (one cell per pixel):
//   water W          0 = dry
//   suspended S      pigment still moving: premultiplied colour (r, g, b on 0..1) and amount a
//   deposited D      pigment settled on the paper (same units)
//   base             the layer's pixels from before this paint arrived
// The pixels shown are always base glazed with D + S (multiply glaze, so washes layer like
// transparent paint: alpha α = 1 − e^(−k·a), out = base × (1 − α + α·colour) over paint,
// colour × α over transparency).
//
// One simulation step (all tiles read the previous state and write the next one, so the result
// does not depend on tile order and there are no seams):
//   - water flows only between wet cells, down a pressure difference: new wet paint spreads into
//     a wet wash (wet-on-wet, blooms) but stops at dry paper (wet-on-dry: hard edges);
//   - near the edge of the wet area the pressure is lower and evaporation faster, so water and the
//     pigment it carries flow outwards while the edge stays wet, and the water leaves there
//     without its pigment: the edges dry darker (edge darkening, the "coffee ring");
//   - suspended pigment travels with the water flux and diffuses a little between wet cells;
//   - suspended pigment settles, more in the paper's valleys (granulation);
//   - a cell whose water falls below a threshold has dried: all its pigment settles.
// A brush wets the paper to its own wetness and exchanges paint with the wash under it (the wash
// moves towards the brush's colour and concentration); where it wets settled paint it loosens part
// of it back into the wash (lifting), which a clean damp brush then takes up.
//
// Time: the simulation advances in fixed steps of StepMs of simulation time. A stroke maps its
// input event times to steps; after the stroke the canvas advances it on a timer (wall-clock).
// Everything else is deterministic: the same steps give the same pixels.
//
// Bounds: tiles are allocated only where the brush wets the paper, charged to the memory budget
// (Temporary), at most MaxTiles at once; a tile that has dried completely keeps its settled
// pigment for lifting until the layer is released.
class WetLayer
{
public:
    static constexpr double StepMs = 16.0;
    static constexpr int MaxTiles = 600; // about 140 MiB; beyond it new paper stays dry
    static constexpr float DryWater = 0.004f;
    static constexpr double PigmentScale = 1.0; // brush pigment 1 = this amount of paint in the wash

    struct Params
    {
        double spread = 0.5;        // diffusion of water and pigment, 0..1
        double edgeDarkening = 0.5; // extra evaporation at the edge of the wet area, 0..1
        double granulation = 0.3;   // pigment settling in the paper's valleys, 0..1
        double dryingSeconds = 6.0; // simulation time for a full wash to dry
        double lifting = 0.2;       // share of settled pigment new water loosens, per pass
        double density = 2.5;       // pigment amount → opacity (α = 1 − e^(−density·a))
        const BrushTips::Texture *paper = nullptr;
        double tooth = 0.0;         // paper texture strength
    };

    explicit WetLayer(QSize size);
    ~WetLayer();
    WetLayer(const WetLayer &) = delete;
    WetLayer &operator=(const WetLayer &) = delete;

    void setParams(const Params &params) { m_params = params; }
    const Params &params() const { return m_params; }
    QSize size() const { return m_size; }

    // Brush input over `rect`: at each pixel with contact c = min(1, 8 × footprint(x, y)) the brush
    // wets the paper towards its wetness `water`, loosens settled pigment (lifting × c × pass) and
    // moves the suspended paint towards its own (`color`, PigmentScale × `pigment`), each by
    // min(1, 4 × pass) × c of the way. `pass` is this dab's share of a full pass (flow normalised to
    // the spacing). Returns false when memory ran out.
    bool addPaint(const StrokeSurface &image, TileSnapshot &before, const QRect &rect,
                  const std::function<double(int, int)> &footprint, QRgb color, double water, double pigment,
                  double pass);

    // Advances `steps` steps and writes the pixels of every tile that changed (through the
    // selection mask, if any). Returns the changed area.
    QRect step(StrokeSurface &image, TileSnapshot &before, int steps);
    // Writes the pixels of the tiles changed since the last write (after addPaint without a step).
    QRect render(StrokeSurface &image, TileSnapshot &before);

    void setClip(const QImage &mask) { m_clip = mask; }
    bool isWet() const { return m_wetCells > 0; }
    qint64 wetCells() const { return m_wetCells; }
    int tileCount() const { return int(m_tiles.size()); }
    qsizetype bytes() const;
    bool allocationFailed() const { return m_allocationFailed; }
    // Water, suspended and settled pigment amount at a pixel (tests).
    float water(int x, int y) const;
    float suspended(int x, int y) const;
    float settled(int x, int y) const;

private:
    struct Tile;
    Tile *tileAt(int column, int row) const;
    Tile *ensureTile(int column, int row, const StrokeSurface &image);
    void writeTile(StrokeSurface &image, TileSnapshot &before, qint64 key, Tile &tile, QRect &changed);
    double paperHeight(int x, int y) const;

    QSize m_size;
    Params m_params;
    std::map<qint64, std::unique_ptr<Tile>> m_tiles; // ordered: deterministic iteration
    QImage m_clip;
    qint64 m_wetCells = 0;
    bool m_allocationFailed = false;
};

// Renders the brush test sheet: every built-in preset through the real StrokeEngine on a fixed set
// of strokes, for inspecting gaps, stamping, jagged edges, darkening, seams and halos.
//
//   brush_sheet <output.png>
//
// Columns: pen stroke with a pressure ramp 0 -> 1 -> 0; the same with mouse input (no pressure);
// dots, a slow diagonal and sharp corners; a self-crossing loop and three repeated strokes; the
// pen stroke on a transparent layer (checkerboard); a 4x crop of the pen stroke's edge.
// Paint brushes draw on an empty layer; smudge and erasers work on a striped, partly transparent
// layer.

#include "BrushPresets.h"
#include "StrokeEngine.h"
#include "TileSnapshot.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPainter>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {

constexpr int CellW = 280, CellH = 160, LabelW = 130, HeaderH = 34;
constexpr int Columns = 6;

StrokeSample sample(QPointF pos, qreal pressure, double timeMs)
{
    StrokeSample s;
    s.pos = pos;
    s.pressure = pressure;
    s.timeMs = timeMs;
    return s;
}

// `events` input events along the curve f(t), t in [0, 1], 8 ms apart (a 125 Hz pen).
std::vector<StrokeSample> curve(int events, const std::function<QPointF(double)> &f,
                                const std::function<qreal(double)> &pressure)
{
    std::vector<StrokeSample> out;
    for (int i = 0; i <= events; ++i) {
        const double t = double(i) / events;
        out.push_back(sample(f(t), pressure(t), i * 8.0));
    }
    return out;
}

QPointF sCurve(double t)
{
    return QPointF(20 + 240 * t, 80 + 45 * std::sin(t * 2 * M_PI));
}

qreal ramp(double t)
{
    return std::sin(t * M_PI); // 0 -> 1 -> 0
}

void stroke(QImage &layer, const BrushSettings &s, const QColor &color, const std::vector<StrokeSample> &samples,
            quint64 seed)
{
    TileSnapshot before;
    StrokeEngine engine(s, color, seed);
    engine.begin(layer, samples.front(), before);
    for (size_t i = 1; i < samples.size(); ++i)
        engine.moveTo(layer, samples[i], before);
    engine.finish(layer, before);
}

QImage emptyLayer()
{
    QImage image(CellW, CellH, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

// Coloured stripes, the right part fading out to transparent: something to smudge and erase.
QImage stripedLayer()
{
    QImage image = emptyLayer();
    const QColor colors[] = {QColor(210, 60, 50), QColor(240, 200, 60), QColor(50, 110, 200), QColor(40, 150, 90)};
    for (int y = 0; y < CellH; ++y)
        for (int x = 0; x < CellW; ++x) {
            const QColor c = colors[(x / 35) % 4];
            const int a = x < 180 ? 255 : std::max(0, 255 - (x - 180) * 255 / 80);
            image.setPixel(x, y, qPremultiply(qRgba(c.red(), c.green(), c.blue(), a)));
        }
    return image;
}

void checker(QPainter &p, const QRect &r)
{
    for (int y = r.top(); y < r.bottom(); y += 8)
        for (int x = r.left(); x < r.right(); x += 8)
            p.fillRect(QRect(x, y, 8, 8) & r, ((x - r.left()) / 8 + (y - r.top()) / 8) % 2 ? QColor(200, 200, 200)
                                                                                         : QColor(245, 245, 245));
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv); // fonts for the labels
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("brush_sheet.png");
    const auto &presets = BrushPresets::builtIns();
    QImage sheet(LabelW + Columns * CellW, HeaderH + int(presets.size()) * CellH, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor(250, 250, 248));
    QPainter p(&sheet);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font = p.font();
    font.setPixelSize(13);
    p.setFont(font);
    const char *headers[Columns] = {"pen, pressure 0-1-0", "mouse (no pressure)", "dots, slow diagonal, corners",
                                    "crossing loop, 3 repeats", "pen on transparent", "4x crop of the pen stroke"};
    for (int c = 0; c < Columns; ++c)
        p.drawText(QRect(LabelW + c * CellW, 0, CellW, HeaderH), Qt::AlignCenter, QString::fromLatin1(headers[c]));

    const QColor ink(25, 30, 45);
    for (size_t row = 0; row < presets.size(); ++row) {
        const BrushPreset &preset = presets[row];
        const BrushSettings &s = preset.settings;
        const int top = HeaderH + int(row) * CellH;
        p.drawText(QRect(8, top, LabelW - 12, CellH), Qt::AlignVCenter | Qt::TextWordWrap, s.name);
        const bool usesLayer = s.blend != BrushBlend::Paint;
        auto base = [&] { return usesLayer ? stripedLayer() : emptyLayer(); };

        // 1, 5, 6: pen with pressure.
        QImage pen = base();
        stroke(pen, s, ink, curve(90, sCurve, ramp), 11);
        // 2: mouse.
        QImage mouse = base();
        stroke(mouse, s, ink, curve(90, sCurve, [](double) { return 1.0; }), 12);
        // 3: dots, a slow diagonal (many events), corners.
        QImage misc = base();
        for (int i = 0; i < 5; ++i)
            stroke(misc, s, ink, {sample(QPointF(25 + i * 26, 25), 0.3 + 0.15 * i, 0)}, 20 + i);
        stroke(misc, s, ink, curve(300, [](double t) { return QPointF(20 + 110 * t, 55 + 90 * t); },
                                   [](double) { return 0.7; }),
               30);
        stroke(misc, s, ink, curve(40, [](double t) {
                   const double u = t * 4;
                   const int k = std::min(3, int(u));
                   const double f = u - k;
                   const QPointF pts[5] = {{150, 140}, {180, 60}, {210, 140}, {240, 60}, {265, 140}};
                   return pts[k] + (pts[k + 1] - pts[k]) * f;
               }, [](double) { return 0.8; }),
               31);
        // 4: a self-crossing loop and three overlapping repeats.
        QImage loops = base();
        stroke(loops, s, ink, curve(80, [](double t) {
                   const double a = t * 2 * M_PI;
                   return QPointF(75 + 55 * std::sin(a), 80 + 50 * std::sin(2 * a));
               }, [](double t) { return 0.6 + 0.4 * std::sin(t * M_PI); }),
               40);
        for (int k = 0; k < 3; ++k)
            stroke(loops, s, ink, curve(30, [k](double t) { return QPointF(160 + 100 * t, 70 + 4 * k + 10 * t); },
                                        [](double) { return 0.8; }),
                   41 + k);

        const QImage cells[4] = {pen, mouse, misc, loops};
        for (int c = 0; c < 4; ++c) {
            const QRect r(LabelW + c * CellW, top, CellW, CellH);
            if (usesLayer)
                checker(p, r.adjusted(1, 1, -1, -1));
            else
                p.fillRect(r.adjusted(1, 1, -1, -1), Qt::white);
            p.drawImage(r.topLeft(), cells[c]);
        }
        // 5: on transparency (the checkerboard shows halos and fringes).
        const QRect r5(LabelW + 4 * CellW, top, CellW, CellH);
        checker(p, r5.adjusted(1, 1, -1, -1));
        p.drawImage(r5.topLeft(), pen);
        // 6: 4x nearest-neighbour crop around the top of the S-curve, on white.
        QImage onWhite(pen.size(), QImage::Format_ARGB32_Premultiplied);
        onWhite.fill(Qt::white);
        {
            QPainter w(&onWhite);
            w.drawImage(0, 0, pen);
        }
        const QRect crop(QPoint(int(sCurve(0.25).x()) - 35, int(sCurve(0.25).y()) - 20), QSize(70, 40));
        const QImage zoom = onWhite.copy(crop).scaled(CellW, CellH, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        p.drawImage(LabelW + 5 * CellW, top, zoom);
        p.setPen(QColor(220, 220, 220));
        p.drawLine(0, top, sheet.width(), top);
        p.setPen(Qt::black);
    }
    p.end();
    if (!sheet.save(out)) {
        std::fprintf(stderr, "could not write %s\n", qPrintable(out));
        return 1;
    }
    std::printf("wrote %s (%d presets)\n", qPrintable(out), int(presets.size()));
    return 0;
}

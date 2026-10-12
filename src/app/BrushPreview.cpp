#include "BrushPreview.h"

#include "CanvasItem.h"
#include "StrokeEngine.h"
#include "TileSnapshot.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <cmath>

BrushPreview::BrushPreview(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    connect(this, &BrushPreview::settingsChanged, this, [this] { update(); });
}

void BrushPreview::paint(QPainter *painter)
{
    const QSize size = boundingRect().size().toSize();
    if (size.isEmpty())
        return;

    // Checkerboard, so opacity and erasing are visible.
    for (int y = 0; y < size.height(); y += 8)
        for (int x = 0; x < size.width(); x += 8)
            painter->fillRect(x, y, 8, 8, ((x + y) / 8) % 2 ? QColor(204, 204, 204) : QColor(255, 255, 255));

    // Graph area on the right, stroke on the left.
    const int graph = std::min(size.height() - 8, 64);
    const QRectF strokeArea(4, 4, size.width() - graph - 16, size.height() - 8);

    QImage layer(size, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    if (m_eraser) // something to erase
        layer.fill(QColor(110, 110, 110));

    // Figure eight with pressure 0 -> 1 -> 0 and a little deterministic hand jitter.
    const qreal diameter = std::min<qreal>(m_brushSize, strokeArea.height() * 0.3);
    const QPointF center = strokeArea.center();
    const qreal rx = strokeArea.width() / 2 - diameter / 2 - 2;
    const qreal ry = strokeArea.height() / 2 - diameter / 2 - 2;
    std::vector<StrokeSample> input;
    const int count = 160;
    for (int i = 0; i <= count; ++i) {
        const qreal t = qreal(i) / count;
        const qreal a = t * 2 * M_PI;
        const qreal jitter = m_smoothing > 0 ? 1.5 * std::sin(i * 2.7) : 0.0;
        input.push_back({QPointF(center.x() + rx * std::sin(a), center.y() + ry * std::sin(2 * a) * 0.9 + jitter),
                         std::sin(t * M_PI)});
    }

    BrushSettings settings;
    settings.blend = m_eraser ? BrushBlend::Erase : BrushBlend::Paint;
    settings.size = diameter;
    settings.opacity = m_opacity;
    settings.pressureSize = {true, m_minSize, PressureResponse::gammaForFirmness(m_firmness)};
    StrokeEngine engine(settings, m_color, 1, m_smoothing / 100.0 * CanvasItem::MaxSmoothingRadius);
    TileSnapshot before;
    engine.begin(layer, input.front(), before);
    for (size_t i = 1; i < input.size(); ++i)
        engine.moveTo(layer, input[i], before);
    engine.finish(layer, before);

    if (m_smoothing > 0) {
        QPainterPath raw;
        raw.moveTo(input.front().pos);
        for (const StrokeSample &s : input)
            raw.lineTo(s.pos);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(QColor(230, 80, 60, 200), 1));
        painter->drawPath(raw);
    }
    painter->drawImage(0, 0, layer);

    // Pressure (x) to size (y) curve.
    const QRectF box(size.width() - graph - 6, (size.height() - graph) / 2.0, graph, graph);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->fillRect(box, QColor(255, 255, 255, 230));
    painter->setPen(QPen(QColor(150, 150, 150), 1));
    painter->drawRect(box);
    const PressureResponse response{m_minSize, PressureResponse::gammaForFirmness(m_firmness)};
    QPainterPath curve;
    for (int i = 0; i <= 32; ++i) {
        const qreal p = i / 32.0;
        const QPointF pt(box.left() + p * box.width(), box.bottom() - response.sizeFactor(p) * box.height());
        if (i == 0)
            curve.moveTo(pt);
        else
            curve.lineTo(pt);
    }
    painter->setPen(QPen(QColor(30, 100, 220), 2));
    painter->drawPath(curve);
}

#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// Live preview for the brush settings: a figure-eight stroke whose pressure rises and falls, drawn
// through the same smoother, spacer and compositor as the canvas (so the crossing shows that a
// stroke does not darken itself), plus the pressure-to-size curve. With smoothing on, the jittery
// input path is drawn as a thin line under the stroke.
class BrushPreview : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int brushSize MEMBER m_brushSize NOTIFY settingsChanged)
    Q_PROPERTY(QColor color MEMBER m_color NOTIFY settingsChanged)
    Q_PROPERTY(qreal brushOpacity MEMBER m_opacity NOTIFY settingsChanged)
    Q_PROPERTY(qreal pressureMinSize MEMBER m_minSize NOTIFY settingsChanged)
    Q_PROPERTY(qreal pressureFirmness MEMBER m_firmness NOTIFY settingsChanged)
    Q_PROPERTY(int smoothing MEMBER m_smoothing NOTIFY settingsChanged)
    Q_PROPERTY(bool eraser MEMBER m_eraser NOTIFY settingsChanged)

public:
    explicit BrushPreview(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

signals:
    void settingsChanged();

private:
    int m_brushSize = 12;
    QColor m_color = Qt::black;
    qreal m_opacity = 1.0;
    qreal m_minSize = 0.1;
    qreal m_firmness = 0.0;
    int m_smoothing = 0;
    bool m_eraser = false;
};

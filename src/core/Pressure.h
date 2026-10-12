#pragma once

#include <QtGlobal>

enum class PointerKind { Mouse, Pen, Touch };

// Pressure is used only for a pen whose device reports pressure. Mouse, touch (even when a
// touchscreen reports a "pressure" value) and pens without pressure all draw at full size.
inline qreal effectivePressure(PointerKind kind, bool deviceReportsPressure, qreal rawPressure)
{
    if (kind != PointerKind::Pen || !deviceReportsPressure)
        return 1.0;
    if (!(rawPressure >= 0.0)) // NaN or negative
        return 1.0;
    return rawPressure > 1.0 ? 1.0 : rawPressure;
}

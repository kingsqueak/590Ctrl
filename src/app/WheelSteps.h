#pragma once

#include <QWheelEvent>

// Converts a wheel event into whole steps for tuning-style controls. Positive
// means the wheel was rolled away / fingers pushed up, regardless of macOS
// "natural scrolling" (which inverts the reported delta). `remainder` carries
// partial deltas between events; keep one per control.
inline int wheelSteps(const QWheelEvent *event, int &remainder)
{
    int delta = event->angleDelta().y();
    if (event->inverted())
        delta = -delta;
    if (delta == 0)
        return 0;
#ifdef Q_OS_MACOS
    // A mouse wheel (no scroll phase) sends exactly one event per detent, but
    // with system acceleration applied the delta is often far below 120.
    // Count events, not delta, so every click of the wheel is one step.
    if (event->phase() == Qt::NoScrollPhase) {
        remainder = 0;
        return delta > 0 ? 1 : -1;
    }
#endif
    // Trackpads, and high-resolution wheels elsewhere, send many small deltas
    // per notch: accumulate to the standard 120 per step.
    if (event->phase() == Qt::ScrollBegin)
        remainder = 0;
    remainder += delta;
    const int steps = remainder / QWheelEvent::DefaultDeltasPerStep;
    remainder -= steps * QWheelEvent::DefaultDeltasPerStep;
    return steps;
}

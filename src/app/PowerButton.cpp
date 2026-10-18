#include "PowerButton.h"

#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int kDefaultSize = 26;
constexpr double kGapDegrees = 70;   // opening at the top of the circle

}   // namespace

PowerButton::PowerButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void PowerButton::setPowered(bool powered)
{
    if (powered == m_powered)
        return;
    m_powered = powered;
    update();
}

QSize PowerButton::sizeHint() const
{
    return {kDefaultSize, kDefaultSize};
}

void PowerButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double side = std::min(width(), height());
    const QRectF square((width() - side) / 2, (height() - side) / 2, side, side);

    QColor color = m_powered ? palette().color(QPalette::Highlight) : palette().color(QPalette::PlaceholderText);
    if (!isEnabled())
        color.setAlphaF(0.4);
    else if (isDown())
        color = color.darker(130);

    // Soft disc behind the symbol on hover / press.
    if (isEnabled() && (underMouse() || isDown())) {
        QColor halo = color;
        halo.setAlphaF(isDown() ? 0.25 : 0.12);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(square);
    }

    // Everything scales with the button's size.
    const double stroke = std::max(1.5, side * 0.1);
    const QRectF ring = square.adjusted(side * 0.22, side * 0.22, -side * 0.22, -side * 0.22);
    QPen pen(color, stroke, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // Qt angles: 0 at 3 o'clock, counter-clockwise, in 1/16 degree.
    const double start = 90 + kGapDegrees / 2;
    p.drawArc(ring, int(start * 16), int((360 - kGapDegrees) * 16));
    const QPointF center = ring.center();
    p.drawLine(QPointF(center.x(), ring.top() - side * 0.06), QPointF(center.x(), center.y()));

    if (hasFocus()) {
        p.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DotLine));
        p.drawEllipse(square.adjusted(1, 1, -1, -1));
    }
}

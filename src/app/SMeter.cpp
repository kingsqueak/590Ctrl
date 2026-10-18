#include "SMeter.h"

#include "CatCodec.h"

#include <QFontDatabase>
#include <QPainter>

using namespace ts590;

namespace {

constexpr int kPeakHoldMs = 1500;
constexpr int kTop = 2;
constexpr int kSegmentGap = 2;
constexpr int kSpacing = 10;

// Receive layout.
constexpr int kRxBarHeight = 16;

// Transmit layout: power bar, then three thin lines.
constexpr int kTxBarHeight = 10;
constexpr int kTxLineHeight = 6;
constexpr int kTxLineGap = 2;
constexpr int kTxLabelPixelSize = 8;

const QColor kGreen(0x2e, 0xcc, 0x71);
const QColor kYellow(0xf1, 0xc4, 0x0f);
const QColor kOrange(0xf3, 0x9c, 0x12);
const QColor kRed(0xe7, 0x4c, 0x3c);

struct ScaleMark {
    int dots;
    const char *label;
};
constexpr ScaleMark kScale[] = {
    {sMeterDotsForS(1), "1"},
    {sMeterDotsForS(3), "3"},
    {sMeterDotsForS(5), "5"},
    {sMeterDotsForS(7), "7"},
    {sMeterDotsForS(9), "9"},
    {sMeterDotsForDbOverS9(20), "+20"},
    {sMeterDotsForDbOverS9(40), "+40"},
    {sMeterDotsForDbOverS9(60), "+60"},
};

const char *const kTxLineLabels[] = {"SWR", "COMP", "ALC"};

// Zone colors for the thin transmit lines, by dot: SWR green up to 10 dots
// (2.0), yellow to 13, red above; ALC green up to 20, red above.
QColor txLineColor(int line, int dot, const QColor &neutral)
{
    switch (line) {
    case 0: return dot <= 10 ? kGreen : dot <= 13 ? kYellow : kRed;
    case 2: return dot <= 20 ? kGreen : kRed;
    default: return neutral;
    }
}

}   // namespace

SMeter::SMeter(int peakPointSize, QWidget *parent)
    : QWidget(parent)
    , m_peakFont(QFontDatabase::systemFont(QFontDatabase::FixedFont))
    , m_scaleFont(font())
    , m_txLabelFont(font())
{
    m_peakFont.setPointSize(peakPointSize);
    m_peakFont.setBold(true);
    m_scaleFont.setPointSizeF(font().pointSizeF() * 0.85);
    m_txLabelFont.setPixelSize(kTxLabelPixelSize);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(tr("Signal strength with peak hold. While transmitting: power, with SWR, COMP and ALC below."));

    m_peakHold.setSingleShot(true);
    m_peakHold.setInterval(kPeakHoldMs);
    // The radio only reports changes, so a steady signal sends nothing:
    // the peak has to fall on a timer, not on the next reading.
    connect(&m_peakHold, &QTimer::timeout, this, [this] {
        m_peak = m_value;
        update();
    });
}

void SMeter::setValue(int dots)
{
    dots = std::clamp(dots, 0, kSMeterMaxDots);
    if (dots >= m_peak) {
        m_peak = dots;
        m_peakHold.start();
    }
    m_value = dots;
    update();
}

void SMeter::setTransmitting(bool transmitting)
{
    if (transmitting == m_transmitting)
        return;
    m_transmitting = transmitting;
    // Power and S-units don't compare: start the peak over.
    m_peak = m_value;
    m_peakHold.start();
    update();
}

void SMeter::setTxMeters(int swr, int comp, int alc)
{
    m_txMeters[0] = std::clamp(swr, 0, kSMeterMaxDots);
    m_txMeters[1] = std::clamp(comp, 0, kSMeterMaxDots);
    m_txMeters[2] = std::clamp(alc, 0, kSMeterMaxDots);
    if (m_transmitting)
        update();
}

void SMeter::reset()
{
    m_value = m_peak = 0;
    m_transmitting = false;
    std::fill(std::begin(m_txMeters), std::end(m_txMeters), 0);
    m_peakHold.stop();
    update();
}

int SMeter::peakTextWidth() const
{
    return QFontMetrics(m_peakFont).horizontalAdvance(QStringLiteral("S9+60"));
}

int SMeter::receiveHeight() const
{
    return kTop + kRxBarHeight + 2 + QFontMetrics(m_scaleFont).height();
}

int SMeter::transmitHeight() const
{
    return kTop + kTxBarHeight + 3 + 3 * kTxLineHeight + 2 * kTxLineGap;
}

QSize SMeter::sizeHint() const
{
    // Same height in both modes so the layout doesn't jump on key-up.
    const int height = std::max({receiveHeight(), transmitHeight(), QFontMetrics(m_peakFont).height()});
    return {400, height + 2};
}

QSize SMeter::minimumSizeHint() const
{
    return {200 + peakTextWidth(), sizeHint().height()};
}

QColor SMeter::segmentColor(int segment, bool transmit) const
{
    const QColor blue = palette().color(QPalette::Highlight);
    // Segment n covers dots (n-1, n].
    if (transmit || segment <= sMeterDotsForS(5))
        return blue;
    if (segment <= kSMeterS9Dots)
        return kGreen;
    if (segment <= sMeterDotsForDbOverS9(20))
        return kOrange;
    return kRed;
}

void SMeter::paintSegments(QPainter &p, const QRect &bar, bool transmit)
{
    const double segmentWidth = double(bar.width() + kSegmentGap) / kSMeterMaxDots;
    for (int segment = 1; segment <= kSMeterMaxDots; ++segment) {
        const QRectF rect(bar.left() + (segment - 1) * segmentWidth, bar.top(), segmentWidth - kSegmentGap,
                          bar.height());
        QColor color = segmentColor(segment, transmit);
        const bool lit = segment <= m_value;
        const bool peak = !transmit && segment == m_peak && m_peak > m_value;
        if (!lit && !peak)
            color.setAlphaF(0.15);
        p.fillRect(rect, color);
    }
}

void SMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int peakWidth = peakTextWidth();
    const QRect area(0, 0, width() - peakWidth - kSpacing, height());
    if (m_transmitting)
        paintTransmit(p, area);
    else
        paintReceive(p, area);

    // Readout on the right: peak S-units colored like the zone reached, or
    // peak output power while transmitting.
    p.setFont(m_peakFont);
    const bool colored = !m_transmitting && m_peak > sMeterDotsForS(5);
    p.setPen(colored ? segmentColor(m_peak, false) : palette().color(QPalette::Text));
    p.drawText(QRect(width() - peakWidth, 0, peakWidth, height()), Qt::AlignRight | Qt::AlignVCenter,
               m_transmitting ? tr("%1 W").arg(txPowerWatts(m_peak)) : sMeterText(m_peak));
}

void SMeter::paintReceive(QPainter &p, const QRect &area)
{
    const QRect bar(area.left(), kTop, area.width(), kRxBarHeight);
    paintSegments(p, bar, false);

    // Scale labels, centered on the end of their segment.
    const double segmentWidth = double(bar.width() + kSegmentGap) / kSMeterMaxDots;
    const QFontMetrics metrics(m_scaleFont);
    const int top = bar.bottom() + 2;
    p.setFont(m_scaleFont);
    p.setPen(palette().color(QPalette::PlaceholderText));
    p.drawText(QRect(bar.left(), top, bar.width(), metrics.height()), Qt::AlignLeft, QStringLiteral("S"));
    for (const ScaleMark &mark : kScale) {
        const QString label = QString::fromLatin1(mark.label);
        const int labelWidth = metrics.horizontalAdvance(label);
        const int x = int(bar.left() + mark.dots * segmentWidth - kSegmentGap - labelWidth / 2.0);
        p.drawText(QRect(std::min(x, bar.right() - labelWidth), top, labelWidth, metrics.height()), Qt::AlignCenter,
                   label);
    }
}

void SMeter::paintTransmit(QPainter &p, const QRect &area)
{
    const QFontMetrics metrics(m_txLabelFont);
    const int labelWidth = metrics.horizontalAdvance(QStringLiteral("SWR 3.0")) + 6;
    // Value column at the right end of the lines (COMP in dB).
    const int valueWidth = metrics.horizontalAdvance(QStringLiteral("30 dB")) + 6;
    const int left = area.left() + labelWidth;
    const int width = area.width() - labelWidth - valueWidth;
    const QColor blue = palette().color(QPalette::Highlight);

    p.setFont(m_txLabelFont);
    p.setPen(palette().color(QPalette::PlaceholderText));
    auto label = [&](const QRect &row, const QString &text) {
        p.drawText(QRect(area.left(), row.center().y() - metrics.height() / 2, labelWidth, metrics.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text);
    };

    const QRect bar(left, kTop, width, kTxBarHeight);
    paintSegments(p, bar, true);
    label(bar, QStringLiteral("PWR"));

    // SWR / COMP / ALC as thin continuous lines over a faint track, each dot
    // filled in its zone color.
    int top = bar.bottom() + 3;
    for (int i = 0; i < 3; ++i) {
        const QRect line(left, top, width, kTxLineHeight);
        QColor track = blue;
        track.setAlphaF(0.15);
        p.fillRect(line, track);
        for (int dot = 1; dot <= m_txMeters[i]; ++dot) {
            const int x0 = left + width * (dot - 1) / kSMeterMaxDots;
            const int x1 = left + width * dot / kSMeterMaxDots;
            p.fillRect(QRect(x0, top, x1 - x0, kTxLineHeight), txLineColor(i, dot, blue));
        }
        QString text = QString::fromLatin1(kTxLineLabels[i]);
        if (i == 0)
            text += QLatin1Char(' ') + swrText(m_txMeters[0]);
        label(line, text);
        if (i == 1) {
            // Each COMP dot is 1 dB of compression.
            p.drawText(QRect(line.right() + 6, line.center().y() - metrics.height() / 2, valueWidth - 6,
                             metrics.height()),
                       Qt::AlignRight | Qt::AlignVCenter, tr("%1 dB").arg(m_txMeters[1]));
        }
        top += kTxLineHeight + kTxLineGap;
    }
}

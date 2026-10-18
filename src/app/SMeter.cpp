#include "SMeter.h"

#include "CatCodec.h"

#include <QFontDatabase>
#include <QLinearGradient>
#include <QPainter>

using namespace ts590;

namespace {

constexpr int kPeakHoldMs = 1500;
constexpr int kAnimationMs = 150;
constexpr int kSpacing = 10;

// The meter scales to the height it's given, within these limits.
constexpr int kMinHeight = 40;
constexpr int kPreferredHeight = 60;
constexpr int kMaxHeight = 64;
constexpr int kMaxRxBarHeight = 18;
constexpr int kMaxTxBarHeight = 12;

// How far either side of a zone boundary the colors blend, as a fraction of
// full scale.
constexpr double kBlend = 0.04;
constexpr double kTrackAlpha = 0.18;

const QColor kGreen(0x2e, 0xcc, 0x71);
const QColor kYellow(0xf1, 0xc4, 0x0f);
const QColor kOrange(0xf3, 0x9c, 0x12);
const QColor kRed(0xe7, 0x4c, 0x3c);
// Brighter and purer than kYellow so it stays distinct from an orange-zone
// peak marker sitting next to it.
const QColor kRfGainMarker(0xff, 0xe6, 0x3c);

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

double fraction(double dots)
{
    return std::clamp(dots / kSMeterMaxDots, 0.0, 1.0);
}

// A color scale as (upper dot limit, color) zones, lowest first.
using Zones = std::initializer_list<std::pair<int, QColor>>;

// Horizontal gradient across `bar` that changes color at each zone boundary,
// blending smoothly over a short distance rather than stepping.
QLinearGradient zoneGradient(const QRectF &bar, Zones zones, double alpha = 1.0)
{
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    auto color = [alpha](QColor c) {
        c.setAlphaF(alpha);
        return c;
    };
    const auto *zone = zones.begin();
    gradient.setColorAt(0, color(zone->second));
    for (; zone + 1 != zones.end(); ++zone) {
        const double edge = fraction(zone->first);
        gradient.setColorAt(std::max(0.0, edge - kBlend), color(zone->second));
        gradient.setColorAt(std::min(1.0, edge + kBlend), color((zone + 1)->second));
    }
    gradient.setColorAt(1, color(zone->second));
    return gradient;
}

// A rounded bar: the whole scale as a faint track, then the reading filled
// in, both painted with the same gradient so colors sit at fixed positions.
void paintBar(QPainter &p, const QRectF &bar, double dots, Zones zones)
{
    const double radius = bar.height() / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(zoneGradient(bar, zones, kTrackAlpha));
    p.drawRoundedRect(bar, radius, radius);

    const double fill = bar.width() * fraction(dots);
    if (fill <= 0.5)
        return;
    // Never narrower than the bar is tall, so the ends stay round.
    p.setBrush(zoneGradient(bar, zones));
    p.drawRoundedRect(QRectF(bar.left(), bar.top(), std::max(fill, bar.height()), bar.height()), radius, radius);
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
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMaximumHeight(kMaxHeight);
    setToolTip(tr("Signal strength with peak hold; the yellow marker shows the RF gain setting. "
                  "While transmitting: power, with SWR, COMP and ALC below."));

    m_peakHold.setSingleShot(true);
    m_peakHold.setInterval(kPeakHoldMs);
    // The radio only reports changes, so a steady signal sends nothing:
    // the peak has to fall on a timer, not on the next reading.
    connect(&m_peakHold, &QTimer::timeout, this, [this] {
        m_peak = m_value;
        update();
    });

    // The bar glides to each new reading instead of jumping.
    m_animation.setDuration(kAnimationMs);
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_shown = value.toDouble();
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
    m_animation.stop();
    m_animation.setStartValue(m_shown);
    m_animation.setEndValue(double(dots));
    m_animation.start();
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

void SMeter::setRfGain(int level)
{
    level = std::clamp(level, 0, 255);
    if (level == m_rfGain)
        return;
    m_rfGain = level;
    if (!m_transmitting)
        update();
}

void SMeter::setSquelchOpen(bool open)
{
    if (open == m_squelchOpen)
        return;
    m_squelchOpen = open;
    update();
}

void SMeter::reset()
{
    m_squelchOpen = false;
    m_value = m_peak = 0;
    m_shown = 0;
    m_animation.stop();
    m_transmitting = false;
    std::fill(std::begin(m_txMeters), std::end(m_txMeters), 0);
    m_peakHold.stop();
    m_rfGain = 255;
    update();
}

int SMeter::peakTextWidth() const
{
    return QFontMetrics(m_peakFont).horizontalAdvance(QStringLiteral("S9+60"));
}

QSize SMeter::sizeHint() const
{
    return {400, kPreferredHeight};
}

QSize SMeter::minimumSizeHint() const
{
    // Both modes lay out in whatever height there is, so key-up never
    // changes the widget's size.
    return {200 + peakTextWidth(), std::max(kMinHeight, QFontMetrics(m_peakFont).height())};
}

QColor SMeter::zoneColor(int dots) const
{
    if (dots <= sMeterDotsForS(5))
        return palette().color(QPalette::Accent);
    if (dots <= kSMeterS9Dots)
        return kGreen;
    if (dots <= sMeterDotsForDbOverS9(20))
        return kOrange;
    return kRed;
}

void SMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
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
    p.setPen(colored ? zoneColor(m_peak) : palette().color(QPalette::Text));
    p.drawText(QRect(width() - peakWidth, 0, peakWidth, height()), Qt::AlignRight | Qt::AlignVCenter,
               m_transmitting ? tr("%1 W").arg(txPowerWatts(m_peak)) : sMeterText(m_peak));

    // Squelch open: small green letters in the space above the readout.
    if (m_squelchOpen && !m_transmitting) {
        QFont font = m_scaleFont;
        font.setBold(true);
        p.setFont(font);
        p.setPen(kGreen);
        const int above = (height() - QFontMetrics(m_peakFont).height()) / 2;
        p.drawText(QRect(width() - peakWidth, 0, peakWidth, std::max(above, QFontMetrics(font).height())),
                   Qt::AlignRight | Qt::AlignBottom, tr("SQL"));
    }
}

void SMeter::paintReceive(QPainter &p, const QRect &area)
{
    // Bar with the scale under it, centered vertically as a block.
    const QFontMetrics metrics(m_scaleFont);
    const int barHeight = std::min(kMaxRxBarHeight, area.height() - metrics.height() - 6);
    const int blockTop = (area.height() - barHeight - 3 - metrics.height()) / 2;
    const QRectF bar(area.left(), blockTop, area.width(), barHeight);

    const QColor blue = palette().color(QPalette::Accent);
    const Zones zones = {{sMeterDotsForS(5), blue},
                         {kSMeterS9Dots, kGreen},
                         {sMeterDotsForDbOverS9(20), kOrange},
                         {kSMeterMaxDots, kRed}};
    paintBar(p, bar, m_shown, zones);

    // Markers on the bar, both the same size: RF gain setting in yellow
    // (hidden at full gain), and the held peak in its zone color.
    const double markerWidth = std::max(3.0, barHeight / 4.0);
    auto marker = [&](double dots, const QColor &color) {
        const double x = bar.left() + bar.width() * fraction(dots);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(std::clamp(x - markerWidth, bar.left(), bar.right() - markerWidth), bar.top(),
                                 markerWidth, bar.height()),
                          markerWidth / 2, markerWidth / 2);
    };
    if (const double rfDots = rfGainMeterDots(m_rfGain); rfDots > 0)
        marker(rfDots, kRfGainMarker);
    if (m_peak > m_value)
        marker(m_peak, zoneColor(m_peak));

    // Scale labels, centered on their position along the bar.
    const int top = int(bar.bottom()) + 3;
    p.setFont(m_scaleFont);
    p.setPen(palette().color(QPalette::PlaceholderText));
    p.drawText(QRect(int(bar.left()), top, int(bar.width()), metrics.height()), Qt::AlignLeft, QStringLiteral("S"));
    for (const ScaleMark &mark : kScale) {
        const QString label = QString::fromLatin1(mark.label);
        const int labelWidth = metrics.horizontalAdvance(label);
        const int x = int(bar.left() + bar.width() * fraction(mark.dots) - labelWidth / 2.0);
        p.drawText(QRect(std::min(x, int(bar.right()) - labelWidth), top, labelWidth, metrics.height()),
                   Qt::AlignCenter, label);
    }
}

void SMeter::paintTransmit(QPainter &p, const QRect &area)
{
    // Power bar on top, then three thin lines, sized from the height and
    // centered vertically as a block.
    const int available = area.height() - 4;
    const int lineHeight = std::max(4, available * 12 / 100);
    const int lineGap = std::max(3, available * 6 / 100);
    const int barHeight = std::min(kMaxTxBarHeight, available - 3 * lineHeight - 3 * lineGap);
    const int blockHeight = barHeight + 3 * (lineGap + lineHeight);
    // No taller than a row pitch, so the four labels never touch.
    m_txLabelFont.setPixelSize(std::clamp(lineHeight + lineGap, 8, 11));
    const QFontMetrics metrics(m_txLabelFont);
    const int labelWidth = metrics.horizontalAdvance(QStringLiteral("SWR 3.0")) + 6;
    // Value column at the right end of the lines (COMP in dB).
    const int valueWidth = metrics.horizontalAdvance(QStringLiteral("30 dB")) + 6;
    const int left = area.left() + labelWidth;
    const int width = area.width() - labelWidth - valueWidth;
    const QColor blue = palette().color(QPalette::Accent);

    auto label = [&](const QRectF &row, const QString &text) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(QRect(area.left(), int(row.center().y()) - metrics.height() / 2, labelWidth, metrics.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text);
    };
    p.setFont(m_txLabelFont);

    double top = (area.height() - blockHeight) / 2.0;
    const QRectF bar(left, top, width, barHeight);
    paintBar(p, bar, m_shown, {{kSMeterMaxDots, blue}});
    label(bar, QStringLiteral("PWR"));

    // SWR: green to 10 dots (2.0), yellow to 13, red above. ALC: green to
    // 20, red above. COMP: plain accent.
    const Zones lineZones[3] = {
        {{10, kGreen}, {13, kYellow}, {kSMeterMaxDots, kRed}},
        {{kSMeterMaxDots, blue}},
        {{20, kGreen}, {kSMeterMaxDots, kRed}},
    };
    top = bar.bottom() + lineGap;
    for (int i = 0; i < 3; ++i) {
        const QRectF line(left, top, width, lineHeight);
        paintBar(p, line, m_txMeters[i], lineZones[i]);
        QString text = QString::fromLatin1(kTxLineLabels[i]);
        if (i == 0)
            text += QLatin1Char(' ') + swrText(m_txMeters[0]);
        label(line, text);
        if (i == 1) {
            // Each COMP dot is 1 dB of compression.
            p.drawText(QRect(int(line.right()) + 6, int(line.center().y()) - metrics.height() / 2, valueWidth - 6,
                             metrics.height()),
                       Qt::AlignRight | Qt::AlignVCenter, tr("%1 dB").arg(m_txMeters[1]));
        }
        top += lineHeight + lineGap;
    }
}

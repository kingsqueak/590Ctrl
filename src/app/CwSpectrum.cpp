#include "CwSpectrum.h"

#include "AppStyle.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kStepHz = 5;
constexpr float kFallDb = 1.5f;        // a frame, 25 frames a second
constexpr float kMinSpanDb = 36;       // noise stays low in the display until a signal stands this far above it
constexpr float kBelowFloorDb = 6;
constexpr float kHeadroomDb = 3;
constexpr float kQuietestFloorDb = -100;
constexpr int kLabelStepHz = 400;
constexpr int kTickStepHz = 100;

QColor reticleColor()
{
    return darkAppearance() ? QColor(0xE5, 0x48, 0x4D) : QColor(0xD9, 0x3A, 0x3F);
}

}   // namespace

CwSpectrum::CwSpectrum(QWidget *parent)
    : QWidget(parent), m_spectrum(kLowHz, kHighHz, kStepHz), m_trace(size_t(m_spectrum.bins()), 0.f)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(tr("The audio being received, %1 to %2 Hz. The red line is the CW pitch: tune until the signal's "
                  "peak sits on it.")
                   .arg(kLowHz)
                   .arg(kHighHz));
}

QSize CwSpectrum::sizeHint() const
{
    return {260, 120};
}

QSize CwSpectrum::minimumSizeHint() const
{
    return {160, 72};
}

void CwSpectrum::setPitchHz(int hz)
{
    if (hz == m_pitchHz)
        return;
    m_pitchHz = hz;
    update();
}

void CwSpectrum::start()
{
    m_message.clear();
    if (!m_running) {
        m_running = true;
        m_scaled = false;
        m_spectrum.reset();
    }
    update();
}

void CwSpectrum::stop(const QString &message)
{
    m_running = false;
    m_message = message;
    update();
}

void CwSpectrum::addSamples(const float *samples, qsizetype count)
{
    if (!m_running || !m_spectrum.push(samples, count))
        return;
    const std::vector<float> &levels = m_spectrum.levels();

    // The noise level: most of the band is noise, a signal only a few steps.
    std::vector<float> sorted = levels;
    const auto middle = sorted.begin() + qsizetype(sorted.size()) * 7 / 10;
    std::nth_element(sorted.begin(), middle, sorted.end());
    const float floor = std::max(*middle, kQuietestFloorDb);
    const float peak = *std::max_element(levels.begin(), levels.end());
    if (!m_scaled) {
        m_scaled = true;
        m_floor = floor;
        m_top = peak;
        m_trace = levels;
    } else {
        m_floor += 0.05f * (floor - m_floor);
        m_top = std::max(peak, m_top - 0.25f);   // holds through the spaces in the keying
        for (size_t i = 0; i < levels.size(); ++i)
            m_trace[i] = std::max(levels[i], m_trace[i] - kFallDb);
    }
    update();
}

void CwSpectrum::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    emit shownChanged();
}

void CwSpectrum::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    emit shownChanged();
}

void CwSpectrum::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QColor line = pal.color(QPalette::Dark);
    const QColor dim = pal.color(QPalette::PlaceholderText);
    const QColor accent = pal.color(QPalette::Accent);

    // The same box as the decoded text beside it.
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(line, 1));
    p.setBrush(pal.color(QPalette::Base));
    p.drawRoundedRect(box, 4, 4);

    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    p.setFont(small);
    const QFontMetrics metrics(small);
    const int scaleHeight = metrics.height() + 5;
    const QRectF plot = box.adjusted(1, 4, -1, -scaleHeight);
    auto xAt = [&](double hz) { return plot.left() + (hz - kLowHz) / (kHighHz - kLowHz) * plot.width(); };

    // The frequency scale along the bottom.
    for (int hz = kLowHz + kTickStepHz; hz < kHighHz; hz += kTickStepHz) {
        const double x = std::round(xAt(hz)) + 0.5;
        const bool labelled = hz % kLabelStepHz == 0;
        p.setPen(QPen(line, 1));
        p.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + (labelled ? 4 : 2)));
        if (labelled) {
            p.setPen(dim);
            const QString text = QString::number(hz);
            p.drawText(QPointF(x - metrics.horizontalAdvance(text) / 2.0, box.bottom() - 3), text);
        }
    }
    p.setPen(QPen(line, 1));
    p.drawLine(QPointF(box.left(), std::round(plot.bottom()) + 0.5), QPointF(box.right(), std::round(plot.bottom()) + 0.5));

    if (m_running && m_scaled) {
        const float bottomDb = m_floor - kBelowFloorDb;
        const float spanDb = std::max(kMinSpanDb, m_top + kHeadroomDb - bottomDb);
        QPainterPath trace;
        for (int b = 0; b < m_spectrum.bins(); ++b) {
            const double level = std::clamp((m_trace[size_t(b)] - bottomDb) / spanDb, 0.f, 1.f);
            const QPointF point(xAt(m_spectrum.frequency(b)), plot.bottom() - level * plot.height());
            if (b == 0)
                trace.moveTo(point);
            else
                trace.lineTo(point);
        }
        QPainterPath fill = trace;
        fill.lineTo(plot.right(), plot.bottom());
        fill.lineTo(plot.left(), plot.bottom());
        fill.closeSubpath();
        p.save();
        p.setClipRect(plot);
        QColor wash = accent;
        wash.setAlpha(70);
        p.fillPath(fill, wash);
        p.setPen(QPen(accent, 1.25));
        p.setBrush(Qt::NoBrush);
        p.drawPath(trace);
        p.restore();
    }
    const bool note = !(m_running && m_scaled) && !m_message.isEmpty();
    if (note) {
        p.setPen(dim);
        p.setFont(font());
        p.drawText(plot.adjusted(8, 0, -8, 0), Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }

    // The reticle: a line at the pitch, with a pointer at each end.
    if (m_pitchHz >= kLowHz && m_pitchHz <= kHighHz) {
        const QColor red = reticleColor();
        const double x = std::round(xAt(m_pitchHz)) + 0.5;
        if (!note) {   // the line would run through the words
            p.setPen(QPen(red, 1.5));
            p.drawLine(QPointF(x, box.top() + 1), QPointF(x, plot.bottom()));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(red);
        const double w = 4.5, h = 6;
        p.drawPolygon(QPolygonF({QPointF(x - w, box.top() + 1), QPointF(x + w, box.top() + 1),
                                 QPointF(x, box.top() + 1 + h)}));
        p.drawPolygon(QPolygonF({QPointF(x - w, plot.bottom()), QPointF(x + w, plot.bottom()),
                                 QPointF(x, plot.bottom() - h)}));
    }
}

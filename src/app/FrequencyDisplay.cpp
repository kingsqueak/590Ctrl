#include "FrequencyDisplay.h"

#include "CatCodec.h"
#include "WheelSteps.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kHoldMs = 400;
constexpr int kLabelWidthChars = 3;

// Accepts "14.195", "14,195.5 kHz", "7074k", "7074000" etc.
std::optional<qint64> parseFrequency(QString text)
{
    text = text.trimmed().toLower().remove(QLatin1Char(' '));
    double scale = 0;
    if (text.endsWith(QLatin1String("mhz")) || text.endsWith(QLatin1Char('m')))
        scale = 1e6;
    else if (text.endsWith(QLatin1String("khz")) || text.endsWith(QLatin1Char('k')))
        scale = 1e3;
    else if (text.endsWith(QLatin1String("hz")))
        scale = 1;
    text.remove(QRegularExpression(QStringLiteral("[a-z]+$")));
    text.replace(QLatin1Char(','), QLatin1Char('.'));

    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || value <= 0)
        return std::nullopt;
    if (scale == 0) {
        // Bare numbers: guess the unit from magnitude.
        scale = value < 100 ? 1e6 : value < 100'000 ? 1e3 : 1;
    }
    return qint64(std::llround(value * scale));
}

}   // namespace

FrequencyDisplay::FrequencyDisplay(int pointSize, QWidget *parent)
    : QWidget(parent)
    , m_font(QFontDatabase::systemFont(QFontDatabase::FixedFont))
{
    m_font.setPointSize(pointSize);
    m_font.setBold(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeVerCursor);
    setToolTip(tr("Scroll over a digit to tune it. Double-click to type a frequency."));
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(kHoldMs);
    connect(&m_holdTimer, &QTimer::timeout, this, [this] {
        if (m_heldUpdate)
            setFrequency(*std::exchange(m_heldUpdate, std::nullopt));
    });
}

void FrequencyDisplay::setRole(const QString &role)
{
    if (role == m_role)
        return;
    m_role = role;
    update();
}

void FrequencyDisplay::setLabel(const QString &label)
{
    m_label = label;
    update();
}

void FrequencyDisplay::setFrequency(qint64 hz)
{
    if (m_holdTimer.isActive()) {
        m_heldUpdate = hz;
        return;
    }
    if (hz == m_hz)
        return;
    m_hz = hz;
    update();
}

QString FrequencyDisplay::text() const
{
    const qint64 mhz = m_hz / 1'000'000;
    const qint64 khz = (m_hz / 1000) % 1000;
    const qint64 hz = m_hz % 1000;
    return QStringLiteral("%1.%2.%3")
        .arg(mhz, 2, 10, QLatin1Char(' '))
        .arg(khz, 3, 10, QLatin1Char('0'))
        .arg(hz, 3, 10, QLatin1Char('0'));
}

QSize FrequencyDisplay::sizeHint() const
{
    const QFontMetrics fm(m_font);
    const int labelWidth = fm.horizontalAdvance(QLatin1Char('M')) * kLabelWidthChars;
    return {labelWidth + fm.horizontalAdvance(text()) + 16, fm.height() + 8};
}

QRect FrequencyDisplay::textRect() const
{
    const QFontMetrics fm(m_font);
    const int width = fm.horizontalAdvance(text());
    return {rect().right() - width - 8, (height() - fm.height()) / 2, width, fm.height()};
}

int FrequencyDisplay::placeAt(int x) const
{
    const QFontMetrics fm(m_font);
    const QString t = text();
    int left = textRect().left();
    // Map each character to its power of ten, skipping the separators.
    int place = 7;
    for (const QChar c : t) {
        const int w = fm.horizontalAdvance(c);
        if (c != QLatin1Char('.')) {
            if (x >= left && x < left + w)
                return place;
            --place;
        }
        left += w;
    }
    return -1;
}

void FrequencyDisplay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    p.setFont(m_font);
    const QFontMetrics fm(m_font);

    if (!m_label.isEmpty()) {
        QFont labelFont = font();
        labelFont.setBold(true);
        p.setFont(labelFont);
        const QRect labelArea = rect().adjusted(8, 0, 0, 0);
        p.setPen(palette().color(QPalette::PlaceholderText));
        if (m_role.isEmpty()) {
            p.drawText(labelArea, Qt::AlignLeft | Qt::AlignVCenter, m_label);
        } else {
            // Label just above the middle, role just below it.
            const int middle = labelArea.center().y();
            p.drawText(labelArea.adjusted(0, 0, 0, middle - labelArea.bottom()), Qt::AlignLeft | Qt::AlignBottom,
                       m_label);
            p.setPen(palette().color(QPalette::Accent));
            p.drawText(labelArea.adjusted(0, middle - labelArea.top() + 2, 0, 0), Qt::AlignLeft | Qt::AlignTop,
                       m_role);
        }
        p.setFont(m_font);
    }

    const QRect area = textRect();
    const QString t = text();
    int x = area.left();
    int place = 7;
    for (const QChar c : t) {
        const int w = fm.horizontalAdvance(c);
        const bool isDigit = c != QLatin1Char('.');
        // Dim the digits below 1 kHz, like the radio's display.
        const bool small = isDigit && place < 3;
        p.setPen(small ? palette().color(QPalette::PlaceholderText) : palette().color(QPalette::Text));
        p.drawText(QRect(x, area.top(), w, area.height()), Qt::AlignCenter, QString(c));
        if (isDigit && place == m_hoverPlace && hasFocus()) {
            p.fillRect(QRect(x + 1, area.bottom() - 2, w - 2, 3), palette().accent());
        } else if (isDigit && place == m_hoverPlace) {
            p.fillRect(QRect(x + 1, area.bottom() - 1, w - 2, 2), palette().mid());
        }
        if (isDigit)
            --place;
        x += w;
    }
}

void FrequencyDisplay::step(qint64 delta)
{
    const qint64 hz = std::clamp(m_hz + delta, ts590::kMinFrequencyHz, ts590::kMaxFrequencyHz);
    if (hz == m_hz)
        return;
    m_hz = hz;
    m_heldUpdate.reset();
    m_holdTimer.start();
    update();
    emit frequencyRequested(hz);
}

void FrequencyDisplay::wheelEvent(QWheelEvent *event)
{
    const int place = placeAt(int(event->position().x()));
    if (place < 0) {
        m_wheelRemainder = 0;
        event->ignore();
        return;
    }
    event->accept();
    if (const int notches = wheelSteps(event, m_wheelRemainder))
        step(notches * qint64(std::pow(10, place)));
}

void FrequencyDisplay::mouseMoveEvent(QMouseEvent *event)
{
    const int place = placeAt(int(event->position().x()));
    if (place != m_hoverPlace) {
        m_hoverPlace = place;
        update();
    }
}

void FrequencyDisplay::leaveEvent(QEvent *)
{
    m_hoverPlace = -1;
    update();
}

void FrequencyDisplay::keyPressEvent(QKeyEvent *event)
{
    const qint64 unit = qint64(std::pow(10, m_hoverPlace >= 0 ? m_hoverPlace : 1));
    switch (event->key()) {
    case Qt::Key_Up: step(unit); break;
    case Qt::Key_Down: step(-unit); break;
    case Qt::Key_Return:
    case Qt::Key_Enter: startEditing(); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void FrequencyDisplay::mouseDoubleClickEvent(QMouseEvent *)
{
    startEditing();
}

void FrequencyDisplay::startEditing()
{
    if (!m_editor) {
        m_editor = new QLineEdit(this);
        m_editor->setFont(m_font);
        m_editor->setAlignment(Qt::AlignRight);
        m_editor->setPlaceholderText(tr("MHz"));
        connect(m_editor, &QLineEdit::editingFinished, this, &FrequencyDisplay::finishEditing);
    }
    m_editor->setGeometry(textRect().adjusted(-4, -2, 4, 2));
    m_editor->setText(QString::number(double(m_hz) / 1e6, 'f', 6));
    m_editor->selectAll();
    m_editor->show();
    m_editor->setFocus();
}

void FrequencyDisplay::finishEditing()
{
    if (!m_editor || !m_editor->isVisible())
        return;
    const auto hz = parseFrequency(m_editor->text());
    m_editor->hide();
    setFocus();
    if (hz)
        step(*hz - m_hz);
}

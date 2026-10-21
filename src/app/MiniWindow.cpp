#include "MiniWindow.h"

#include "FrequencyDisplay.h"
#include "SMeter.h"

#include <QBoxLayout>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QFrame>
#include <QPushButton>
#include <QShortcut>
#include <QToolButton>

#include <cmath>

namespace {

// Smallest scale the window shrinks to, and how far past full size it can be
// dragged (the content stops growing at full size).
constexpr double kMinScale = 0.4;
constexpr double kMaxWidthFactor = 1.25;
constexpr double kMaxHeightFactor = 1.1;

}   // namespace

MiniWindow::MiniWindow(int vfoPointSize, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_vfoPointSize(vfoPointSize)
    , m_baseFontSize(font().pointSizeF())
{
    setWindowTitle(QStringLiteral("590Ctrl"));

    m_vfoA = new FrequencyDisplay(vfoPointSize);
    m_vfoA->setLabel(QStringLiteral("A"));
    m_vfoB = new FrequencyDisplay(vfoPointSize / 2);
    m_vfoB->setLabel(QStringLiteral("B"));

    m_vfoButtons = new QButtonGroup(this);
    m_vfoButtons->setExclusive(true);
    m_buttonRow = new QHBoxLayout;
    for (int id : {0, 1}) {
        auto *button = new QPushButton(id == 0 ? QStringLiteral("A") : QStringLiteral("B"));
        button->setCheckable(true);
        button->setToolTip(tr("Receive and transmit on VFO %1").arg(button->text()));
        m_vfoButtons->addButton(button, id);
        m_buttonRow->addWidget(button);
    }
    m_copyButton = new QPushButton(tr("A=B"));
    m_copyButton->setToolTip(tr("Copy the receive VFO's frequency and mode to the other VFO"));
    m_buttonRow->addWidget(m_copyButton);
    m_buttonRow->addStretch();

    m_pinButton = new QToolButton;
    m_pinButton->setText(QStringLiteral("📌"));
    m_pinButton->setCheckable(true);
    m_pinButton->setToolTip(tr("Keep this window on top of other apps"));
    connect(m_pinButton, &QToolButton::clicked, this, &MiniWindow::setPinned);
    m_fullButton = new QToolButton;
    m_fullButton->setText(QStringLiteral("⤢"));
    m_fullButton->setObjectName(QStringLiteral("miniFull"));
    m_fullButton->setToolTip(tr("Full window (%1)").arg(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M)
                                                            .toString(QKeySequence::NativeText)));
    connect(m_fullButton, &QToolButton::clicked, this, &MiniWindow::fullModeRequested);
    m_buttonRow->addWidget(m_pinButton);
    m_buttonRow->addWidget(m_fullButton);

    // The same inset display box as the main window's VFO panel.
    m_vfoBox = new QFrame;
    m_vfoBox->setFrameShape(QFrame::StyledPanel);
    m_vfoBox->setFrameShadow(QFrame::Sunken);
    m_vfoBox->setAutoFillBackground(true);
    m_vfoBox->setBackgroundRole(QPalette::Base);
    m_boxLayout = new QVBoxLayout(m_vfoBox);
    m_boxLayout->addWidget(m_vfoA);
    m_boxLayout->addWidget(m_vfoB);
    m_boxLayout->addLayout(m_buttonRow);

    m_meter = new SMeter(vfoPointSize / 2);

    m_muteButton = new QPushButton(tr("MUT"));
    m_muteButton->setObjectName(QStringLiteral("miniMute"));
    m_muteButton->setCheckable(true);
    m_muteButton->setToolTip(tr("Mute the received audio: the speaker and the USB audio output"));
    m_txButton = new QPushButton(tr("TX"));
    m_txButton->setObjectName(QStringLiteral("miniTx"));
    m_txButton->setToolTip(tr("Hold to transmit"));
    for (QPushButton *button : {m_muteButton, m_txButton})
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_bottomRow = new QHBoxLayout;
    m_bottomRow->addWidget(m_muteButton, 1);
    m_bottomRow->addWidget(m_txButton, 3);

    m_layout = new QVBoxLayout(this);
    m_layout->addWidget(m_vfoBox);
    m_layout->addWidget(m_meter);
    m_layout->addLayout(m_bottomRow, 1);
    // The window sets its own limits; the content follows the window's size
    // rather than holding it at full size.
    m_layout->setSizeConstraint(QLayout::SetNoConstraint);

    auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), this);
    connect(shortcut, &QShortcut::activated, this, &MiniWindow::fullModeRequested);

    applyScale(1.0);
    m_referenceSize = m_layout->sizeHint();
    setMinimumSize((QSizeF(m_referenceSize) * kMinScale).toSize());
    setMaximumSize(int(m_referenceSize.width() * kMaxWidthFactor), int(m_referenceSize.height() * kMaxHeightFactor));
    resize((QSizeF(m_referenceSize) * 0.6).toSize());
    setPinned(true);
}

QList<QWidget *> MiniWindow::radioControls() const
{
    QList<QWidget *> controls = {m_vfoA, m_vfoB, m_copyButton, m_muteButton, m_txButton};
    for (QAbstractButton *button : m_vfoButtons->buttons())
        controls << button;
    return controls;
}

bool MiniWindow::isPinned() const
{
    return m_pinButton->isChecked();
}

void MiniWindow::setPinned(bool pinned)
{
    m_pinButton->setChecked(pinned);
    if (pinned == bool(windowFlags() & Qt::WindowStaysOnTopHint))
        return;
    const bool visible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, pinned);
    if (visible)
        show();   // changing the flags hides the window
}

void MiniWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_referenceSize.isEmpty())
        return;
    const double scale = std::min(double(width()) / m_referenceSize.width(),
                                  double(height()) / m_referenceSize.height());
    applyScale(std::clamp(scale, kMinScale, 1.0));
}

void MiniWindow::closeEvent(QCloseEvent *event)
{
    if (m_quitting) {
        event->accept();
        return;
    }
    // Closing the mini window goes back to the full one; quit from there.
    event->ignore();
    emit fullModeRequested();
}

void MiniWindow::applyScale(double scale)
{
    if (std::abs(scale - m_scale) < 0.01)
        return;
    m_scale = scale;
    auto px = [scale](double pixels) { return std::max(1, int(std::lround(pixels * scale))); };

    m_vfoA->setPointSize(std::max(10, int(std::lround(m_vfoPointSize * scale))));
    m_vfoB->setPointSize(std::max(7, int(std::lround(m_vfoPointSize / 2.0 * scale))));
    // The A/B labels and RX/TX tags are drawn in the widget font.
    QFont labelFont = font();
    labelFont.setPointSizeF(std::max(7.0, m_baseFontSize * scale));
    m_vfoA->setFont(labelFont);
    m_vfoB->setFont(labelFont);
    m_meter->setScale(scale);

    m_boxLayout->setContentsMargins(px(4), px(4), px(8), px(6));
    m_boxLayout->setSpacing(px(2));
    m_buttonRow->setSpacing(px(6));
    m_layout->setContentsMargins(px(10), px(10), px(10), px(10));
    m_layout->setSpacing(px(6));
    m_bottomRow->setSpacing(px(8));
    m_txButton->setMinimumHeight(px(44));
    m_muteButton->setMinimumHeight(px(44));

    // Styled rather than native so the buttons can shrink with the window.
    const double fontSize = std::max(7.0, m_baseFontSize * scale);
    setStyleSheet(QStringLiteral(
                      "QPushButton, QToolButton { font-size: %1pt; padding: %2px %3px; border: 1px solid palette(mid);"
                      "  border-radius: %4px; background: palette(button); color: palette(button-text); }"
                      "QPushButton:checked, QToolButton:checked { background: palette(highlight);"
                      "  color: palette(highlighted-text); }"
                      "QPushButton:pressed { background: palette(mid); }"
                      "QPushButton:disabled { color: palette(mid); }"
                      "#miniFull { font-size: %6pt; font-weight: bold; padding: 0px %3px; }"
                      "#miniMute { font-weight: bold; }"
                      "#miniMute:checked { background: #e74c3c; color: white; border-color: #c0392b; }"
                      "#miniTx { font-size: %5pt; font-weight: bold; color: white; background: #2ecc71; border: none; }"
                      "#miniTx:pressed { background: #e74c3c; }"
                      "#miniTx:disabled { background: palette(midlight); color: palette(mid); }")
                      .arg(fontSize)
                      .arg(px(3))
                      .arg(px(12))
                      .arg(px(5))
                      .arg(std::max(9.0, m_baseFontSize * 1.6 * scale))
                      .arg(std::max(9.0, m_baseFontSize * 1.4 * scale)));
}

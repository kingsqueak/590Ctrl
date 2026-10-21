#pragma once

#include <QWidget>

class QButtonGroup;
class QFrame;
class QHBoxLayout;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class FrequencyDisplay;
class SMeter;

// Compact window to park in a corner: VFO A and B with A / B / A=B, the
// meter, then MUT and a large hold-to-transmit TX. Everything scales with the
// window, from corner size up to the full window's size. MainWindow wires the
// widgets to the radio exactly as it does its own.
class MiniWindow : public QWidget
{
    Q_OBJECT

public:
    // vfoPointSize: VFO A's digits at full size, as in the main window.
    explicit MiniWindow(int vfoPointSize, QWidget *parent = nullptr);

    FrequencyDisplay *vfoA() const { return m_vfoA; }
    FrequencyDisplay *vfoB() const { return m_vfoB; }
    QButtonGroup *vfoButtons() const { return m_vfoButtons; }
    QPushButton *copyButton() const { return m_copyButton; }
    SMeter *meter() const { return m_meter; }
    QPushButton *muteButton() const { return m_muteButton; }
    QPushButton *txButton() const { return m_txButton; }
    // Controls that only work while connected.
    QList<QWidget *> radioControls() const;

    bool isPinned() const;
    void setPinned(bool pinned);   // stays on top of other apps
    // While the app quits, closing really closes instead of going back to
    // the full window.
    void setQuitting(bool quitting) { m_quitting = quitting; }

signals:
    void fullModeRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void applyScale(double scale);

    int m_vfoPointSize;
    double m_baseFontSize;
    double m_scale = 0;
    QSize m_referenceSize;   // the layout at full size
    bool m_quitting = false;

    QVBoxLayout *m_layout = nullptr;
    QFrame *m_vfoBox = nullptr;
    QVBoxLayout *m_boxLayout = nullptr;
    QHBoxLayout *m_buttonRow = nullptr;
    QHBoxLayout *m_bottomRow = nullptr;
    FrequencyDisplay *m_vfoA = nullptr;
    FrequencyDisplay *m_vfoB = nullptr;
    QButtonGroup *m_vfoButtons = nullptr;
    QPushButton *m_copyButton = nullptr;
    QToolButton *m_fullButton = nullptr;
    QToolButton *m_pinButton = nullptr;
    SMeter *m_meter = nullptr;
    QPushButton *m_muteButton = nullptr;
    QPushButton *m_txButton = nullptr;
};

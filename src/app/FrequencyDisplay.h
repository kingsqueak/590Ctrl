#pragma once

#include <QTimer>
#include <QWidget>

#include <optional>

class QLineEdit;

// Shows a frequency as "14.195.000". Scrolling over a digit tunes that digit,
// Up/Down tune the hovered digit (or 10 Hz), and double-click opens direct
// entry. Updates from the radio are held back briefly while the user is
// tuning so late echoes don't make the display jump backwards.
class FrequencyDisplay : public QWidget
{
    Q_OBJECT

public:
    explicit FrequencyDisplay(int pointSize, QWidget *parent = nullptr);

    qint64 frequency() const { return m_hz; }
    void setFrequency(qint64 hz);   // from the radio
    void setLabel(const QString &label);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void frequencyRequested(qint64 hz);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString text() const;
    QRect textRect() const;
    // Power of ten for the digit at x, or -1 if x isn't over a digit.
    int placeAt(int x) const;
    void step(qint64 delta);
    void startEditing();
    void finishEditing();

    QFont m_font;
    QString m_label;
    qint64 m_hz = 0;
    int m_hoverPlace = -1;
    QTimer m_holdTimer;
    std::optional<qint64> m_heldUpdate;
    QLineEdit *m_editor = nullptr;
};

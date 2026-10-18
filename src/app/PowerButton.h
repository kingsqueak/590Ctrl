#pragma once

#include <QAbstractButton>

// The standard power symbol (IEC 60417-5009: a circle broken at the top with
// a vertical line through the gap), drawn at any size. Blue while the radio
// is on, muted otherwise.
class PowerButton : public QAbstractButton
{
    Q_OBJECT

public:
    explicit PowerButton(QWidget *parent = nullptr);

    void setPowered(bool powered);
    bool isPowered() const { return m_powered; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_powered = false;
};

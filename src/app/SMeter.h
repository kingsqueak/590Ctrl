#pragma once

#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

// Meter for the radio's 30-dot readings, drawn as rounded bars with smooth
// gradient color zones; the bar glides between readings.
//
// Receive: S-meter with an S-unit scale and a numeric peak-hold readout on
// the right. Below S5 is the system accent blue, S5..S9 green, S9..+20 orange,
// above +20 red.
//
// Transmit: the radio's power meter as the main bar, with SWR, COMP and ALC
// as three thin lines underneath, in the same footprint. The readout shows
// peak watts; SWR is shown beside its label and COMP (1 dB per dot) at the
// right end of its line.
class SMeter : public QWidget
{
    Q_OBJECT

public:
    // peakPointSize: size of the numeric peak readout.
    explicit SMeter(int peakPointSize, QWidget *parent = nullptr);

    void setValue(int dots);
    void setTransmitting(bool transmitting);
    void setTxMeters(int swr, int comp, int alc);   // dots, 0..30
    // RF gain setting (0..255), shown as a yellow marker on receive.
    void setRfGain(int level);
    void reset();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void paintReceive(QPainter &p, const QRect &area);
    void paintTransmit(QPainter &p, const QRect &area);
    QColor zoneColor(int dots) const;   // receive zone color at a reading
    int peakTextWidth() const;

    QFont m_peakFont;
    QFont m_scaleFont;
    QFont m_txLabelFont;
    int m_value = 0;
    double m_shown = 0;   // animated position of the main bar, in dots
    QVariantAnimation m_animation;
    int m_peak = 0;
    QTimer m_peakHold;   // when it fires, the peak falls back to the reading
    bool m_transmitting = false;
    int m_txMeters[3] = {};   // SWR, COMP, ALC
    int m_rfGain = 255;
};

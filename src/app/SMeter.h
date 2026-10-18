#pragma once

#include <QTimer>
#include <QWidget>

// Segmented meter matching the radio's 30-dot display.
//
// Receive: S-meter with an S-unit scale and a numeric peak-hold readout on
// the right. Below S5 is the interface blue, S5..S9 green, S9..+20 orange,
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
    void reset();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void paintReceive(QPainter &p, const QRect &area);
    void paintTransmit(QPainter &p, const QRect &area);
    void paintSegments(QPainter &p, const QRect &bar, bool transmit);
    QColor segmentColor(int segment, bool transmit) const;
    int peakTextWidth() const;
    int receiveHeight() const;
    int transmitHeight() const;

    QFont m_peakFont;
    QFont m_scaleFont;
    QFont m_txLabelFont;
    int m_value = 0;
    int m_peak = 0;
    QTimer m_peakHold;   // when it fires, the peak falls back to the reading
    bool m_transmitting = false;
    int m_txMeters[3] = {};   // SWR, COMP, ALC
};

#pragma once

#include "AudioSpectrum.h"

#include <QWidget>

#include <vector>

// The CW tab's tuning display: the receive audio from 100 to 1300 Hz, with a
// red reticle at the CW pitch. A signal is zero beat when its peak sits on
// the reticle.
class CwSpectrum : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kLowHz = 100;
    static constexpr int kHighHz = 1300;

    explicit CwSpectrum(QWidget *parent = nullptr);

    void setPitchHz(int hz);
    // Running: audio is expected. Stopped: the trace is cleared and the
    // message, if any, says why there's nothing to show.
    void start();
    void stop(const QString &message = {});
    bool isRunning() const { return m_running; }
    // Mono receive audio at 48 kHz.
    void addSamples(const float *samples, qsizetype count);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void shownChanged();   // on or off the screen: whether it wants audio

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    ts590::AudioSpectrum m_spectrum;
    std::vector<float> m_trace;   // dB, smoothed: up at once, down slowly
    float m_floor = 0;            // the noise level, and
    float m_top = 0;              // the strongest signal lately: the scale runs between them
    bool m_scaled = false;
    bool m_running = false;
    int m_pitchHz = 0;
    QString m_message;
};

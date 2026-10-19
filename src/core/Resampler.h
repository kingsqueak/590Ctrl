#pragma once

#include <QList>

namespace ts590 {

// Integer-ratio sample rate converter for interleaved audio: decimates by
// `factor` (Down) or interpolates by it (Up), with a windowed-sinc low-pass
// that keeps the passband below 0.45 of the lower rate. Factor 1 copies.
// Used between the codec's 48 kHz and the 24/12/8 kHz TCI rates.
class Resampler
{
public:
    enum class Direction { Down, Up };

    Resampler(int factor = 1, Direction direction = Direction::Down, int channels = 1);

    int factor() const { return m_factor; }
    void reset();
    // `frames` frames of interleaved input; returns interleaved output.
    QList<float> process(const float *input, qsizetype frames);

private:
    float filter(int channel) const;
    void push(int channel, float sample);

    int m_factor;
    Direction m_direction;
    int m_channels;
    QList<float> m_taps;
    QList<float> m_history;   // per channel, m_taps.size() long, circular
    int m_head = 0;           // next write position in each channel's history
    int m_phase = 0;          // decimation: input frames since the last output
};

}   // namespace ts590

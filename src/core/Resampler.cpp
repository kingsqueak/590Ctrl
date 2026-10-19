#include "Resampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ts590 {

namespace {

constexpr int kTapsPerFactor = 24;

// Blackman-windowed sinc with cutoff `fc` cycles per sample.
QList<float> lowPass(int length, double fc, double gain)
{
    QList<float> taps(length);
    const double middle = (length - 1) / 2.0;
    for (int n = 0; n < length; ++n) {
        const double x = n - middle;
        const double sinc = x == 0 ? 2 * fc : std::sin(2 * std::numbers::pi * fc * x) / (std::numbers::pi * x);
        const double w = 0.42 - 0.5 * std::cos(2 * std::numbers::pi * n / (length - 1))
                         + 0.08 * std::cos(4 * std::numbers::pi * n / (length - 1));
        taps[n] = float(sinc * w * gain);
    }
    return taps;
}

}   // namespace

Resampler::Resampler(int factor, Direction direction, int channels)
    : m_factor(std::max(factor, 1))
    , m_direction(direction)
    , m_channels(std::max(channels, 1))
{
    if (m_factor > 1) {
        // Interpolation stuffs zeros, so the filter makes up the lost gain.
        m_taps = lowPass(kTapsPerFactor * m_factor + 1, 0.45 / m_factor,
                         direction == Direction::Up ? m_factor : 1);
    }
    reset();
}

void Resampler::reset()
{
    m_history.fill(0.f, m_taps.size() * m_channels);
    m_head = 0;
    m_phase = 0;
}

void Resampler::push(int channel, float sample)
{
    m_history[channel * m_taps.size() + m_head] = sample;
}

float Resampler::filter(int channel) const
{
    // m_head is the newest sample; walk back through the circular history.
    const qsizetype length = m_taps.size();
    const float *history = m_history.constData() + channel * length;
    float sum = 0.f;
    qsizetype pos = m_head;
    for (qsizetype i = 0; i < length; ++i) {
        sum += m_taps[i] * history[pos];
        pos = pos == 0 ? length - 1 : pos - 1;
    }
    return sum;
}

QList<float> Resampler::process(const float *input, qsizetype frames)
{
    if (m_factor == 1)
        return QList<float>(input, input + frames * m_channels);

    const qsizetype length = m_taps.size();
    QList<float> out;
    if (m_direction == Direction::Down) {
        out.reserve(frames / m_factor * m_channels + m_channels);
        for (qsizetype f = 0; f < frames; ++f) {
            m_head = int((m_head + 1) % length);
            for (int c = 0; c < m_channels; ++c)
                push(c, input[f * m_channels + c]);
            if (++m_phase == m_factor) {
                m_phase = 0;
                for (int c = 0; c < m_channels; ++c)
                    out.append(filter(c));
            }
        }
    } else {
        out.reserve(frames * m_factor * m_channels);
        for (qsizetype f = 0; f < frames; ++f) {
            for (int k = 0; k < m_factor; ++k) {
                m_head = int((m_head + 1) % length);
                for (int c = 0; c < m_channels; ++c)
                    push(c, k == 0 ? input[f * m_channels + c] : 0.f);
                for (int c = 0; c < m_channels; ++c)
                    out.append(filter(c));
            }
        }
    }
    return out;
}

}   // namespace ts590

#include "Resampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ts590 {

namespace {

constexpr int kTapsPerFactor = 24;
// RateConverter: taps per output frame, and how finely the positions between
// two input frames are tabulated (the rest is interpolated).
constexpr int kTaps = 32;
constexpr int kPhases = 256;

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

RateConverter::RateConverter(int inputRate, int outputRate, int channels)
    : m_inputRate(std::max(inputRate, 1))
    , m_outputRate(std::max(outputRate, 1))
    , m_channels(std::max(channels, 1))
{
    if (m_inputRate != m_outputRate) {
        // Cycles per input sample; 0.45 of the lower rate, as Resampler.
        const double fc = 0.45 * std::min(1.0, double(m_outputRate) / m_inputRate);
        const double half = kTaps / 2.0;
        m_table.resize((kPhases + 1) * kTaps);
        for (int phase = 0; phase <= kPhases; ++phase) {
            // The output sits `mu` past tap kTaps/2 - 1.
            const double mu = double(phase) / kPhases;
            float *row = m_table.data() + phase * kTaps;
            double sum = 0;
            for (int k = 0; k < kTaps; ++k) {
                const double x = k - (half - 1) - mu;
                const double sinc = x == 0 ? 2 * fc : std::sin(2 * std::numbers::pi * fc * x) / (std::numbers::pi * x);
                const double t = x / half;   // -1..1 across the window
                const double w = std::abs(t) >= 1 ? 0
                                                  : 0.42 + 0.5 * std::cos(std::numbers::pi * t)
                                                        + 0.08 * std::cos(2 * std::numbers::pi * t);
                row[k] = float(sinc * w);
                sum += sinc * w;
            }
            for (int k = 0; k < kTaps; ++k)
                row[k] = float(row[k] / sum);   // unity gain at every position
        }
    }
    reset();
}

void RateConverter::reset()
{
    // Silence ahead of the first sample, so output starts with the input.
    m_buffer.assign(size_t(kTaps / 2 - 1) * m_channels, 0.f);
    m_index = 0;
    m_fraction = 0;
}

QList<float> RateConverter::process(const float *input, qsizetype frames)
{
    if (m_inputRate == m_outputRate)
        return QList<float>(input, input + frames * m_channels);

    m_buffer.insert(m_buffer.end(), input, input + frames * m_channels);
    const qsizetype available = qsizetype(m_buffer.size()) / m_channels;
    QList<float> out;
    out.reserve((frames * m_outputRate / m_inputRate + 2) * m_channels);
    while (m_index + kTaps <= available) {
        const double position = double(m_fraction) / m_outputRate * kPhases;
        const int phase = std::min(int(position), kPhases - 1);
        const float blend = float(position - phase);
        const float *a = m_table.constData() + phase * kTaps;
        const float *b = a + kTaps;
        const float *in = m_buffer.data() + m_index * m_channels;
        for (int c = 0; c < m_channels; ++c) {
            float sum = 0.f;
            for (int k = 0; k < kTaps; ++k)
                sum += in[k * m_channels + c] * (a[k] + blend * (b[k] - a[k]));
            out.append(sum);
        }
        m_fraction += m_inputRate;
        m_index += m_fraction / m_outputRate;
        m_fraction %= m_outputRate;
    }
    const qsizetype done = std::min(m_index, available);
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + done * m_channels);
    m_index -= done;
    return out;
}

}   // namespace ts590

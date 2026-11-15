#include "AudioSpectrum.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ts590 {

AudioSpectrum::AudioSpectrum(double lowHz, double highHz, double stepHz)
    : m_lowHz(lowHz), m_stepHz(stepHz), m_window(kWindow), m_samples(kWindow), m_frame(kWindow)
{
    double sum = 0;
    for (int n = 0; n < kWindow; ++n) {
        m_window[size_t(n)] = float(0.5 - 0.5 * std::cos(2 * std::numbers::pi * n / kWindow));
        sum += m_window[size_t(n)];
    }
    for (float &w : m_window)
        w = float(w * 2 / sum);
    const int bins = int(std::floor((highHz - lowHz) / stepHz + 0.5)) + 1;
    m_coeff.resize(size_t(bins));
    for (int b = 0; b < bins; ++b)
        m_coeff[size_t(b)] = 2 * std::cos(2 * std::numbers::pi * frequency(b) / kSampleRate);
    m_levels.assign(size_t(bins), kSilence);
}

void AudioSpectrum::reset()
{
    m_filled = 0;
    m_sinceHop = 0;
    std::fill(m_levels.begin(), m_levels.end(), kSilence);
}

bool AudioSpectrum::push(const float *samples, qsizetype count)
{
    bool fresh = false;
    while (count > 0) {
        // Up to the next hop, then analyse.
        const qsizetype take = std::min<qsizetype>(count, kHop - m_sinceHop);
        std::move(m_samples.begin() + take, m_samples.end(), m_samples.begin());
        std::copy(samples, samples + take, m_samples.end() - take);
        m_filled = std::min<qsizetype>(m_filled + take, kWindow);
        m_sinceHop += take;
        samples += take;
        count -= take;
        if (m_sinceHop >= kHop) {
            m_sinceHop = 0;
            if (m_filled >= kWindow) {
                analyse();
                fresh = true;
            }
        }
    }
    return fresh;
}

// The Goertzel recurrence for each frequency: a transform of just the band
// shown, at whatever spacing, with no need for a power-of-two FFT.
void AudioSpectrum::analyse()
{
    for (int n = 0; n < kWindow; ++n)
        m_frame[size_t(n)] = m_samples[size_t(n)] * m_window[size_t(n)];
    for (size_t b = 0; b < m_coeff.size(); ++b) {
        const double c = m_coeff[b];
        double s1 = 0, s2 = 0;
        for (const float x : m_frame) {
            const double s0 = x + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = std::max(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0);
        m_levels[b] = power > 1e-14 ? float(10 * std::log10(power)) : kSilence;
    }
}

}   // namespace ts590

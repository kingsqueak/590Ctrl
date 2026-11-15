#pragma once

#include <QtTypes>

#include <vector>

namespace ts590 {

// The level of receive audio across a band of audio frequencies, for the CW
// tab's tuning display. Mono audio at 48 kHz goes in; each time enough has
// arrived there's a new set of levels, one per frequency step.
class AudioSpectrum
{
public:
    static constexpr int kSampleRate = 48'000;
    static constexpr int kWindow = 4096;   // 85 ms: about 25 Hz between two tones it can tell apart
    static constexpr int kHop = 1920;      // 25 spectra a second
    static constexpr float kSilence = -140.f;

    AudioSpectrum(double lowHz, double highHz, double stepHz);

    // True if this audio completed at least one new spectrum.
    bool push(const float *samples, qsizetype count);
    void reset();

    int bins() const { return int(m_levels.size()); }
    double frequency(int bin) const { return m_lowHz + bin * m_stepHz; }
    // dB relative to a full-scale sine, kSilence at the lowest.
    const std::vector<float> &levels() const { return m_levels; }

private:
    void analyse();

    double m_lowHz;
    double m_stepHz;
    std::vector<float> m_window;    // Hann, scaled so a full-scale sine reads 0 dB
    std::vector<double> m_coeff;    // Goertzel: 2 cos(w) for each bin
    std::vector<float> m_samples;   // the last kWindow samples, oldest first once full
    std::vector<float> m_frame;
    std::vector<float> m_levels;
    qsizetype m_filled = 0;         // samples held, up to kWindow
    qsizetype m_sinceHop = 0;
};

}   // namespace ts590

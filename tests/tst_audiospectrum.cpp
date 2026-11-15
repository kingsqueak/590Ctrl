#include "AudioSpectrum.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace ts590;

namespace {

std::vector<float> tone(double hz, double amplitude, int count)
{
    std::vector<float> out(size_t(count), 0.f);
    for (int i = 0; i < count; ++i)
        out[size_t(i)] = float(amplitude * std::sin(2 * std::numbers::pi * hz * i / AudioSpectrum::kSampleRate));
    return out;
}

int peakBin(const AudioSpectrum &spectrum)
{
    const auto &levels = spectrum.levels();
    return int(std::max_element(levels.begin(), levels.end()) - levels.begin());
}

}   // namespace

class TestAudioSpectrum : public QObject
{
    Q_OBJECT

private slots:
    void nothingUntilAWindowIsFull()
    {
        AudioSpectrum spectrum(100, 1300, 5);
        QCOMPARE(spectrum.bins(), 241);
        QCOMPARE(spectrum.frequency(100), 600.0);
        const auto audio = tone(600, 1, AudioSpectrum::kHop);
        QVERIFY(!spectrum.push(audio.data(), qsizetype(audio.size())));
        QVERIFY(!spectrum.push(audio.data(), qsizetype(audio.size())));
        QVERIFY(spectrum.push(audio.data(), qsizetype(audio.size())));   // 5760 samples: past the window
    }

    void toneAtItsFrequencyAndLevel_data()
    {
        QTest::addColumn<double>("hz");
        QTest::addColumn<double>("amplitude");
        QTest::addColumn<double>("dB");
        QTest::newRow("600 Hz full scale") << 600.0 << 1.0 << 0.0;
        QTest::newRow("300 Hz at -20 dB") << 300.0 << 0.1 << -20.0;
        QTest::newRow("1000 Hz at -40 dB") << 1000.0 << 0.01 << -40.0;
    }

    void toneAtItsFrequencyAndLevel()
    {
        QFETCH(double, hz);
        QFETCH(double, amplitude);
        QFETCH(double, dB);
        AudioSpectrum spectrum(100, 1300, 5);
        const auto audio = tone(hz, amplitude, AudioSpectrum::kSampleRate / 2);
        QVERIFY(spectrum.push(audio.data(), qsizetype(audio.size())));
        const int peak = peakBin(spectrum);
        QCOMPARE(spectrum.frequency(peak), hz);
        QVERIFY(std::abs(spectrum.levels()[size_t(peak)] - dB) < 0.2);
        // Well down 100 Hz away.
        QVERIFY(spectrum.levels()[size_t(peak + 20)] < dB - 60);
        QVERIFY(spectrum.levels()[size_t(peak - 20)] < dB - 60);
    }

    void sameInSmallPieces()
    {
        AudioSpectrum whole(100, 1300, 10), pieces(100, 1300, 10);
        const auto audio = tone(750, 0.5, AudioSpectrum::kSampleRate / 2);
        whole.push(audio.data(), qsizetype(audio.size()));
        for (size_t at = 0; at < audio.size(); at += 480)
            pieces.push(audio.data() + at, qsizetype(std::min<size_t>(480, audio.size() - at)));
        QCOMPARE(pieces.levels(), whole.levels());
    }

    void resetForgets()
    {
        AudioSpectrum spectrum(100, 1300, 10);
        const auto audio = tone(600, 1, AudioSpectrum::kSampleRate / 2);
        spectrum.push(audio.data(), qsizetype(audio.size()));
        spectrum.reset();
        QCOMPARE(spectrum.levels()[50], AudioSpectrum::kSilence);
        QVERIFY(!spectrum.push(audio.data(), AudioSpectrum::kHop));
    }
};

QTEST_APPLESS_MAIN(TestAudioSpectrum)
#include "tst_audiospectrum.moc"

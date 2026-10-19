#include "Resampler.h"

#include <QTest>

#include <cmath>
#include <numbers>

using namespace ts590;

namespace {

QList<float> tone(double hz, int rate, int frames, int channels = 1)
{
    QList<float> out;
    for (int i = 0; i < frames; ++i) {
        const float v = float(std::sin(2 * std::numbers::pi * hz * i / rate));
        for (int c = 0; c < channels; ++c)
            out.append(v);
    }
    return out;
}

// Level of one channel relative to a full-scale sine (RMS, so it doesn't
// depend on where the samples fall on the wave), skipping the filter's
// start-up.
float level(const QList<float> &samples, int channels = 1, int channel = 0)
{
    double sum = 0;
    int n = 0;
    for (qsizetype i = samples.size() / 4 / channels * channels + channel; i < samples.size(); i += channels, ++n)
        sum += double(samples[i]) * samples[i];
    return float(std::sqrt(sum / n) * std::numbers::sqrt2);
}

}   // namespace

class TestResampler : public QObject
{
    Q_OBJECT

private slots:
    void factorOneCopies()
    {
        Resampler r(1, Resampler::Direction::Down, 2);
        const QList<float> in = tone(1000, 48000, 100, 2);
        QCOMPARE(r.process(in.constData(), 100), in);
    }

    void decimatesKeepingPassband()
    {
        Resampler r(6, Resampler::Direction::Down, 2);
        const QList<float> in = tone(1500, 48000, 48000, 2);
        const QList<float> out = r.process(in.constData(), 48000);
        QCOMPARE(out.size(), 8000 * 2);
        QVERIFY(std::abs(level(out, 2, 0) - 1.f) < 0.01f);
        QVERIFY(std::abs(level(out, 2, 1) - 1.f) < 0.01f);
    }

    void decimationRejectsAliases()
    {
        // 10 kHz would fold to 2 kHz at 8 kHz without the filter.
        Resampler r(6, Resampler::Direction::Down, 1);
        const QList<float> in = tone(10000, 48000, 48000);
        QVERIFY(level(r.process(in.constData(), 48000)) < 0.001f);   // below -60 dB
    }

    void interpolatesKeepingLevel()
    {
        Resampler r(2, Resampler::Direction::Up, 1);
        const QList<float> in = tone(1000, 24000, 24000);
        const QList<float> out = r.process(in.constData(), 24000);
        QCOMPARE(out.size(), 48000);
        QVERIFY(std::abs(level(out) - 1.f) < 0.01f);
    }

    void streamsAcrossCalls()
    {
        // Feeding in pieces gives the same result as all at once.
        const QList<float> in = tone(700, 48000, 4800);
        Resampler whole(4, Resampler::Direction::Down, 1), pieces(4, Resampler::Direction::Down, 1);
        const QList<float> expected = whole.process(in.constData(), in.size());
        QList<float> got;
        for (qsizetype i = 0; i < in.size(); i += 333)
            got.append(pieces.process(in.constData() + i, std::min<qsizetype>(333, in.size() - i)));
        QCOMPARE(got, expected);
    }
};

QTEST_MAIN(TestResampler)
#include "tst_resampler.moc"

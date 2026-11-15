#include "DeepCwDecoder.h"

#include <QHash>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <numbers>
#include <vector>

using namespace ts590;

namespace {

const QHash<QChar, QString> kMorse = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},     {'F', "..-."},
    {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
    {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},
    {'S', "..."},   {'T', "-"},     {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
    {'Y', "-.--"},  {'Z', "--.."},  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
};

// Keyed tone at 48 kHz with a little noise under it, and silence at each end.
std::vector<float> keyed(const QString &text, int wpm, double pitchHz)
{
    constexpr int rate = 48'000;
    const int dit = rate * 12 / (10 * wpm);   // 1.2 / wpm seconds
    const int ramp = rate * 5 / 1000;
    std::vector<float> envelope(size_t(rate), 0.0f);   // 1 s lead-in
    const auto add = [&envelope](int samples, float level) { envelope.insert(envelope.end(), size_t(samples), level); };
    for (const QChar c : text) {
        if (c == ' ') {
            add(4 * dit, 0);   // 7 dits with the 3 after the last character
            continue;
        }
        for (const QChar element : kMorse.value(c)) {
            add(element == '-' ? 3 * dit : dit, 1);
            add(dit, 0);
        }
        add(2 * dit, 0);
    }
    add(5 * rate, 0);   // long enough for the last character to be committed
    std::vector<float> out(envelope.size());
    float shaped = 0;
    quint32 noise = 12345;
    for (size_t i = 0; i < out.size(); ++i) {
        shaped += (envelope[i] - shaped) * (3.0f / ramp);   // soften the edges
        noise = noise * 1664525u + 1013904223u;
        const float hiss = (float(noise >> 8) / float(1 << 24) - 0.5f) * 0.02f;
        out[i] = 0.3f * shaped * float(std::sin(2 * std::numbers::pi * pitchHz * double(i) / rate)) + hiss;
    }
    return out;
}

}   // namespace

class TestDeepCw : public QObject
{
    Q_OBJECT

private slots:
    void decodesKeyedTone()
    {
        QVERIFY(DeepCwDecoder::isBuilt());
        DeepCwDecoder decoder;
        QSignalSpy ready(&decoder, &DeepCwDecoder::ready);
        QSignalSpy failed(&decoder, &DeepCwDecoder::failed);
        QString text;
        connect(&decoder, &DeepCwDecoder::decoded, this, [&text](const QString &more) { text += more; });
        decoder.start();
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failed.isEmpty(), 20'000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));

        const QString sent = QStringLiteral("CQ CQ DE KC2RGW KC2RGW K");
        const std::vector<float> audio = keyed(sent, 20, 600);
        decoder.push(audio.data(), qsizetype(audio.size()));
        QTRY_COMPARE_WITH_TIMEOUT(text, sent, 30'000);
        QVERIFY(failed.isEmpty());
    }

    // Going away mid-stream doesn't hang or crash.
    void stopsWhileBusy()
    {
        DeepCwDecoder decoder;
        QSignalSpy ready(&decoder, &DeepCwDecoder::ready);
        decoder.start();
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 20'000);
        const std::vector<float> audio = keyed(QStringLiteral("TEST TEST TEST"), 25, 700);
        decoder.push(audio.data(), qsizetype(audio.size()));
    }
};

QTEST_GUILESS_MAIN(TestDeepCw)
#include "tst_deepcw.moc"

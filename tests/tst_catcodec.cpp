#include "CatCodec.h"

#include <QTest>

using namespace ts590;

class TestCatCodec : public QObject
{
    Q_OBJECT

private slots:
    void encodesFrequency()
    {
        QCOMPARE(cmd::frequency(Vfo::A, 14'195'000), QByteArray("FA00014195000;"));
        QCOMPARE(cmd::frequency(Vfo::B, 7'000'000), QByteArray("FB00007000000;"));
        // Clamped to receive coverage.
        QCOMPARE(cmd::frequency(Vfo::A, 1), QByteArray("FA00000030000;"));
        QCOMPARE(cmd::frequency(Vfo::A, 99'000'000), QByteArray("FA00060000000;"));
    }

    void encodesGains()
    {
        QCOMPARE(cmd::afGain(7), QByteArray("AG0007;"));
        QCOMPARE(cmd::afGain(300), QByteArray("AG0255;"));
        QCOMPARE(cmd::squelch(42), QByteArray("SQ0042;"));
        QCOMPARE(cmd::rfGain(255), QByteArray("RG255;"));
    }

    void encodesModeAndFilters()
    {
        QCOMPARE(cmd::mode(Mode::FSKR), QByteArray("MD9;"));
        QCOMPARE(cmd::dataMode(true), QByteArray("DA1;"));
        QCOMPARE(cmd::filterSlot(FilterSlot::B), QByteArray("FL2;"));
        QCOMPARE(cmd::filterValue(FilterCommand::SH, 3), QByteArray("SH03;"));
        QCOMPARE(cmd::filterValue(FilterCommand::FW, 500), QByteArray("FW0500;"));
        QCOMPARE(cmd::filterValue(FilterCommand::IS, 800), QByteArray("IS 0800;"));
    }

    void encodesVfoSelection()
    {
        QCOMPARE(cmd::rxVfo(Vfo::B), QByteArray("FR1;"));
        QCOMPARE(cmd::txVfo(Vfo::A), QByteArray("FT0;"));
        QCOMPARE(cmd::copyVfo(), QByteArray("VV;"));
    }

    void encodesRitXitAndPower()
    {
        QCOMPARE(cmd::rit(true), QByteArray("RT1;"));
        QCOMPARE(cmd::xit(false), QByteArray("XT0;"));
        QCOMPARE(cmd::ritXitOffset(0), QByteArray("RC;"));
        QCOMPARE(cmd::ritXitOffset(120), QByteArray("RC;RU00120;"));
        QCOMPARE(cmd::ritXitOffset(-2500), QByteArray("RC;RD02500;"));
        QCOMPARE(cmd::ritXitOffset(20000), QByteArray("RC;RU09999;"));
        QCOMPARE(cmd::txPower(50), QByteArray("PC050;"));
        QCOMPARE(cmd::txPower(1), QByteArray("PC005;"));
        QCOMPARE(cmd::tunePower(100), QByteArray("TP100;"));
        QCOMPARE(txPowerMax(Mode::AM), 25);
        QCOMPARE(txPowerMax(Mode::USB), 100);
    }

    void encodesNotch()
    {
        QCOMPARE(cmd::notch(NotchMode::Off, true), QByteArray("NT00;"));
        QCOMPARE(cmd::notch(NotchMode::Auto, true), QByteArray("NT10;"));
        QCOMPARE(cmd::notch(NotchMode::Manual, false), QByteArray("NT20;"));
        QCOMPARE(cmd::notch(NotchMode::Manual, true), QByteArray("NT21;"));
        QCOMPARE(cmd::notchPosition(7), QByteArray("BP007;"));
        QCOMPARE(cmd::notchPosition(500), QByteArray("BP127;"));
    }

    void encodesNoiseBlankerAndReduction()
    {
        QCOMPARE(cmd::noiseBlanker(NoiseBlanker::Both), QByteArray("NB3;"));
        QCOMPARE(cmd::nbLevel(7), QByteArray("NL007;"));
        QCOMPARE(cmd::nbLevel(0), QByteArray("NL001;"));
        QCOMPARE(cmd::noiseReduction(NoiseReduction::NR2), QByteArray("NR2;"));
        QCOMPARE(cmd::nrLevel(NoiseReduction::NR1, 0), QByteArray("RL01;"));
        QCOMPARE(cmd::nrLevel(NoiseReduction::NR2, 0), QByteArray("RL00;"));
        QCOMPARE(cmd::nrLevel(NoiseReduction::NR2, 10), QByteArray("RL09;"));
        QVERIFY(!nbLevelAdjustable(NoiseBlanker::Both));
        QVERIFY(!noiseBlankerSupported(Mode::FM));
        QVERIFY(!nr2Supported(Mode::FM));
    }

    void menuNumbersDependOnModel()
    {
        const MenuNumbers sg = menuNumbersFor(kModelTs590SG);
        QCOMPARE(sg.usbAudioIn, 71);
        QCOMPARE(sg.usbAudioOut, 72);
        QCOMPARE(sg.ssbFilterMode, 28);
        const MenuNumbers s = menuNumbersFor(kModelTs590S);
        QCOMPARE(s.usbAudioIn, 64);
        QCOMPARE(s.usbAudioOut, 65);
        QCOMPARE(s.ssbFilterMode, -1);
        QCOMPARE(sg.powerFine, 54);
        QCOMPARE(s.powerFine, 48);
        QCOMPARE(sg.txFilter[int(TxFilter::SsbHighCut)], 32);
        QCOMPARE(s.txFilter[int(TxFilter::SsbDataHighCut)], 28);
    }

    void encodesMenusAndEq()
    {
        QCOMPARE(cmd::readMenu(28), QByteArray("EX0280000;"));
        QCOMPARE(cmd::setMenu(36, "7"), QByteArray("EX03600007;"));
        QCOMPARE(cmd::readEqCurve(EqType::Rx, EqMode::SsbData), QByteArray("EQ11;"));
        QCOMPARE(cmd::eqCurve(EqType::Tx, EqMode::Ssb, EqCurve::User), QByteArray("EQ007;"));
        QCOMPARE(cmd::readEqLevels(EqType::Tx), QByteArray("UT;"));

        EqLevels levels = flatEqLevels();
        levels[0] = 0;
        levels[17] = 30;
        QCOMPARE(cmd::eqLevels(EqType::Rx, levels),
                 QByteArray("UR000606060606060606060606060606060630;"));
    }

    void eqDbConversion()
    {
        QCOMPARE(eqRawToDb(0), 6);
        QCOMPARE(eqRawToDb(6), 0);
        QCOMPARE(eqRawToDb(30), -24);
        QCOMPARE(eqDbToRaw(6), 0);
        QCOMPARE(eqDbToRaw(-24), 30);
        QCOMPARE(eqDbToRaw(12), 0);
        QCOMPARE(eqDbToRaw(-40), 30);
        QCOMPARE(eqBandFrequencyHz(17), 5100);
    }

    void eqModeMapping()
    {
        QCOMPARE(eqModeFor(Mode::USB, false), EqMode::Ssb);
        QCOMPARE(eqModeFor(Mode::LSB, true), EqMode::SsbData);
        QCOMPARE(eqModeFor(Mode::CWR, false), EqMode::Cw);
        QCOMPARE(eqModeFor(Mode::FM, true), EqMode::FmData);
        QCOMPARE(eqModeFor(Mode::AM, true), EqMode::AmData);
        QCOMPARE(eqModeFor(Mode::FSKR, false), EqMode::Fsk);
        QVERIFY(!eqCurveSelectable(EqType::Tx, EqMode::Cw));
        QVERIFY(eqCurveSelectable(EqType::Rx, EqMode::Cw));
    }

    // Frames below were captured from a TS-590SG (firmware 1.07).
    void parsesRealStatusFrame()
    {
        const auto info = parseStatus("IF00007222000      010000114010000080");
        QVERIFY(info);
        QCOMPARE(info->frequencyHz, 7'222'000);
        QCOMPARE(info->ritXitOffsetHz, 100);   // positive offsets use a space as sign
        QVERIFY(!info->rit);
        QVERIFY(!info->xit);
        QCOMPARE(info->memoryChannel, 114);
        QVERIFY(!info->transmitting);
        QCOMPARE(info->mode, Mode::LSB);
        QCOMPARE(info->function, Vfo::A);
        QVERIFY(!info->split);
        QCOMPARE(info->toneNumber, 8);
    }

    void parsesNegativeOffsetAndRejectsGarbage()
    {
        // freq | 5 spaces | offset | RIT XIT ch TX mode fn scan split tone tone# P15
        const auto info = parseStatus("IF00014195000"
                                      "     "
                                      "-0120"
                                      "1" "0" "000" "1" "2" "0" "0" "0" "0" "08" "0");
        QVERIFY(info);
        QCOMPARE(info->ritXitOffsetHz, -120);
        QVERIFY(info->rit);
        QVERIFY(info->transmitting);
        QCOMPARE(info->mode, Mode::USB);
        QVERIFY(!parseStatus("IF0001419"));
        QVERIFY(!parseStatus("FA00014195000"));
    }

    void parsesRealEqFrames()
    {
        const auto rx = parseEqLevels("UR060606060708091011121314151617181920");
        QVERIFY(rx);
        QCOMPARE(rx->first, EqType::Rx);
        QCOMPARE(rx->second[0], 6);
        QCOMPARE(rx->second[17], 20);
        QCOMPARE(eqRawToDb(rx->second[17]), -14);

        const auto tx = parseEqLevels("UT060606060606060606060606060606060608");
        QVERIFY(tx);
        QCOMPARE(tx->first, EqType::Tx);
        QCOMPARE(tx->second[17], 8);

        QVERIFY(!parseEqLevels("UR0606"));
        QVERIFY(!parseEqLevels("UR990606060708091011121314151617181920"));   // > 30

        const auto curve = parseEqCurve("EQ017");
        QVERIFY(curve);
        QCOMPARE(curve->type, EqType::Tx);
        QCOMPARE(curve->mode, EqMode::SsbData);
        QCOMPARE(curve->curve, EqCurve::User);
    }

    void parsesRealMenuFrame()
    {
        const auto menu = parseMenu("EX06800005");
        QVERIFY(menu);
        QCOMPARE(menu->number, 68);
        QCOMPARE(menu->value, QByteArray("5"));
        QVERIFY(!parseMenu("EXabc"));
    }

    void filterLayouts()
    {
        const auto hiLo = filterLayoutFor(Mode::LSB, false, false);
        QCOMPARE(hiLo.first.command, FilterCommand::SH);
        QCOMPARE(hiLo.first.valuesHz.value(11), 3400);   // radio reported SH11
        QCOMPARE(hiLo.second.valuesHz.value(1), 50);     // and SL01

        const auto widthShift = filterLayoutFor(Mode::USB, true, true);
        QCOMPARE(widthShift.first.label, QStringLiteral("Shift"));
        QCOMPARE(widthShift.second.label, QStringLiteral("Width"));

        const auto cw = filterLayoutFor(Mode::CW, false, false);
        QCOMPARE(cw.first.command, FilterCommand::IS);
        QCOMPARE(cw.second.command, FilterCommand::FW);
        // FW rounds unlisted values down, like the radio does.
        QCOMPARE(cw.second.valuesHz.value(filterIndexForRaw(cw.second, 1400)), 1000);
        QCOMPARE(filterRawForIndex(cw.second, 0), 50);

        const auto fsk = filterLayoutFor(Mode::FSK, false, false);
        QCOMPARE(fsk.first.command, FilterCommand::None);
        QCOMPARE(fsk.second.valuesHz.size(), 4);

        const auto am = filterLayoutFor(Mode::AM, false, false);
        QCOMPARE(am.first.valuesHz.size(), 4);
    }
};

QTEST_GUILESS_MAIN(TestCatCodec)
#include "tst_catcodec.moc"

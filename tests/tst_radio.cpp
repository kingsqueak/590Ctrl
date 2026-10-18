#include "Radio.h"
#include "SimulatorTransport.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTest>

using namespace ts590;

class TestRadio : public QObject
{
    Q_OBJECT

private:
    // Connects to a fresh simulator and waits for the initial sync to settle.
    SimulatorTransport *connectSim(Radio &radio)
    {
        auto transport = std::make_unique<SimulatorTransport>();
        SimulatorTransport *sim = transport.get();
        radio.connectTransport(std::move(transport));
        radio.setMeterPolling(false);
        settle(radio);
        return sim;
    }

    static void settle(Radio &radio)
    {
        const bool synced = QTest::qWaitFor([&] {
            return radio.state() == Radio::State::Connected && radio.link()->pendingCount() == 0;
        });
        QVERIFY2(synced, "radio did not finish its initial sync");
    }

private slots:
    void syncsInitialState()
    {
        Radio radio;
        connectSim(radio);
        QCOMPARE(radio.modelId(), kModelTs590SG);
        QCOMPARE(radio.firmware(), QStringLiteral("1.07"));
        QCOMPARE(radio.frequency(Vfo::A), 14'195'000);
        QCOMPARE(radio.frequency(Vfo::B), 7'074'000);
        QCOMPARE(radio.mode(), Mode::USB);
        QCOMPARE(radio.afGain(), 80);
        QCOMPARE(radio.filterValue(FilterCommand::SH), 10);
        QCOMPARE(radio.eqCurve(EqType::Rx), EqCurve::Off);
        QVERIFY(radio.eqLevels(EqType::Rx));
    }

    void enablesAndRestoresAutoInfo()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QVERIFY(sim->received().contains("AI2"));
        QPointer<SimulatorTransport> guard(sim);
        radio.disconnectFromRadio();
        QCOMPARE(radio.state(), Radio::State::Disconnected);
        // The restore is written straight to the transport before it closes.
        QVERIFY(guard);
        QCOMPARE(sim->received().last(), QByteArray("AI0"));
    }

    void setsReachTheRadio()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.setAfGain(120);
        radio.setFrequency(Vfo::A, 7'074'000);
        QTRY_COMPARE(sim->afGain(), 120);
        QTRY_COMPARE(sim->frequency(Vfo::A), 7'074'000);
    }

    void selectsVfoAndSplit()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QVERIFY(!radio.split());

        radio.selectVfo(Vfo::B);
        QTRY_COMPARE(radio.rxVfo(), Vfo::B);
        QTRY_COMPARE(radio.txVfo(), Vfo::B);
        QCOMPARE(sim->rxVfo(), Vfo::B);

        radio.setSplit(true);
        QTRY_COMPARE(radio.txVfo(), Vfo::A);
        QCOMPARE(radio.rxVfo(), Vfo::B);
        QVERIFY(radio.split());
        QCOMPARE(sim->txVfo(), Vfo::A);

        radio.setSplit(false);
        QTRY_VERIFY(!radio.split());
        QCOMPARE(radio.txVfo(), Vfo::B);
    }

    void copiesVfo()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.copyVfo();   // RX on A: A=B copies A onto B
        QTRY_COMPARE(radio.frequency(Vfo::B), 14'195'000);
        QCOMPARE(sim->frequency(Vfo::B), 14'195'000);
    }

    void usbAudioLevels()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.usbAudioIn(), std::optional(4));
        QCOMPARE(radio.usbAudioOut(), std::optional(5));

        radio.setUsbAudioIn(7);
        radio.setUsbAudioOut(12);   // clamped to 9
        QTRY_COMPARE(sim->menu(71), 7);
        QTRY_COMPARE(sim->menu(72), 9);
        QCOMPARE(radio.usbAudioOut(), std::optional(9));
    }

    void ritXitOffset()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QVERIFY(!radio.rit() && !radio.xit());

        radio.setRitXitOffset(500);   // ignored while both are off
        radio.setRit(true);
        QTRY_VERIFY(radio.rit());
        QTRY_COMPARE(radio.link()->pendingCount(), 0);
        QCOMPARE(sim->ritXitOffset(), 0);

        radio.setRitXitOffset(120);
        radio.setRitXitOffset(-250);   // absolute, not cumulative
        QTRY_COMPARE(sim->ritXitOffset(), -250);
        QTRY_COMPARE(radio.link()->pendingCount(), 0);
        QCOMPARE(radio.ritXitOffset(), -250);

        radio.setXit(true);
        radio.setRit(false);
        QTRY_VERIFY(radio.xit() && !radio.rit());
        radio.setRitXitOffset(0);
        QTRY_COMPARE(sim->ritXitOffset(), 0);
    }

    void txPower()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.txPower(), std::optional(100));
        QCOMPARE(radio.tunePower(), std::optional(10));
        QVERIFY(!radio.powerFine());

        radio.setTxPower(47);   // power fine off: 5 W steps
        QTRY_COMPARE(sim->txPower(), 45);
        radio.setTunePower(30);
        QTRY_COMPARE(sim->tunePower(), 30);

        radio.setMode(Mode::AM);
        QTRY_COMPARE(radio.mode(), Mode::AM);
        radio.setTxPower(80);   // AM tops out at 25 W
        QTRY_COMPARE(sim->txPower(), 25);
        QTRY_COMPARE(radio.txPower(), std::optional(25));
    }

    void txFilters()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.txFilter(TxFilter::SsbLowCut), std::optional(1));
        QCOMPARE(radio.txFilter(TxFilter::SsbDataHighCut), std::optional(4));

        radio.setTxFilter(TxFilter::SsbHighCut, 5);
        radio.setTxFilter(TxFilter::SsbDataLowCut, 99);   // clamped to the table
        QTRY_COMPARE(sim->menu(32), 5);
        QTRY_COMPARE(sim->menu(33), 5);
        QCOMPARE(txFilterValuesHz(TxFilter::SsbHighCut).value(5), 3000);
        QCOMPARE(txFilterValuesHz(TxFilter::SsbDataLowCut).value(5), 500);
    }

    void notch()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.notchMode(), NotchMode::Off);
        QCOMPARE(radio.notchPosition(), std::optional(64));

        radio.setNotch(NotchMode::Auto, false);
        QTRY_COMPARE(radio.notchMode(), NotchMode::Auto);
        QCOMPARE(sim->notchMode(), 1);

        radio.setNotch(NotchMode::Manual, true);
        QTRY_VERIFY(radio.notchWide());
        QCOMPARE(radio.notchMode(), NotchMode::Manual);

        radio.setNotchPosition(100);
        QTRY_COMPARE(sim->notchPosition(), 100);

        radio.setNotch(NotchMode::Off, true);   // wide is meaningless when off
        QTRY_COMPARE(radio.notchMode(), NotchMode::Off);
        QVERIFY(!radio.notchWide());

        // Not available in FM: Radio doesn't send it, so nothing is rejected.
        radio.setMode(Mode::FM);
        QTRY_COMPARE(radio.mode(), Mode::FM);
        QTRY_COMPARE(radio.link()->pendingCount(), 0);
        QSignalSpy messages(&radio, &Radio::message);
        radio.setNotch(NotchMode::Auto, false);
        QTest::qWait(50);
        QCOMPARE(sim->notchMode(), 0);
        QCOMPARE(messages.size(), 0);
    }

    void noiseBlankerLevels()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.noiseBlanker(), NoiseBlanker::Off);
        QVERIFY(!radio.nbLevel());

        radio.setNoiseBlanker(NoiseBlanker::NB2);
        QTRY_COMPARE(radio.nbLevel(), std::optional(6));   // NB2's own level
        radio.setNbLevel(9);
        QTRY_COMPARE(sim->nbLevel(2), 9);

        radio.setNoiseBlanker(NoiseBlanker::NB1);
        QTRY_COMPARE(radio.nbLevel(), std::optional(3));
        QCOMPARE(sim->nbLevel(2), 9);

        radio.setNoiseBlanker(NoiseBlanker::Both);
        QTRY_COMPARE(radio.noiseBlanker(), NoiseBlanker::Both);
        QVERIFY(!radio.nbLevel());
        QTRY_COMPARE(radio.link()->pendingCount(), 0);
        radio.setNbLevel(4);   // not adjustable with both on: not sent
        QCOMPARE(radio.link()->pendingCount(), 0);
    }

    void noiseReductionLevels()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.setNoiseReduction(NoiseReduction::NR1);
        QTRY_COMPARE(radio.nrLevel(), std::optional(5));
        radio.setNrLevel(8);
        QTRY_COMPARE(sim->nrLevel(1), 8);

        radio.setNoiseReduction(NoiseReduction::NR2);
        QTRY_COMPARE(radio.nrLevel(), std::optional(2));
        radio.setNrLevel(0);
        QTRY_COMPARE(sim->nrLevel(2), 0);

        radio.setNoiseReduction(NoiseReduction::Off);
        QTRY_COMPARE(radio.noiseReduction(), NoiseReduction::Off);
        QVERIFY(!radio.nrLevel());
        QCOMPARE(sim->noiseReduction(), 0);
    }

    void txMetersPolledWhileTransmitting()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.setMeterPolling(true);
        QTest::qWait(300);
        QVERIFY(!sim->received().contains("RM"));   // not polled on receive

        sim->simulateTransmit(true, 4, 12, 9);
        QTRY_VERIFY(radio.transmitting());
        QTRY_COMPARE(radio.txMeter(TxMeter::Swr), 4);
        QTRY_COMPARE(radio.txMeter(TxMeter::Comp), 12);
        QTRY_COMPARE(radio.txMeter(TxMeter::Alc), 9);

        sim->simulateTransmit(false);
        QTRY_VERIFY(!radio.transmitting());
    }

    void carrierMicAndMonitor()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.carrierLevel(), std::optional(50));
        QCOMPARE(radio.micGain(), std::optional(50));
        QCOMPARE(radio.txMonitor(), std::optional(0));

        radio.setCarrierLevel(25);
        radio.setMicGain(70);
        radio.setTxMonitor(12);
        QTRY_COMPARE(sim->carrierLevel(), 25);
        QTRY_COMPARE(sim->micGain(), 70);
        QTRY_COMPARE(sim->txMonitor(), 12);

        radio.setTxMonitor(99);   // SG tops out at 20
        QTRY_COMPARE(sim->txMonitor(), 20);
        radio.setTxMonitor(0);
        QTRY_COMPARE(sim->txMonitor(), 0);
    }

    void modeChangeRefreshesFilterLayout()
    {
        Radio radio;
        connectSim(radio);
        QSignalSpy modeSpy(&radio, &Radio::modeChanged);
        radio.setMode(Mode::CW);
        QTRY_COMPARE(radio.mode(), Mode::CW);
        QTRY_COMPARE(radio.filterValue(FilterCommand::FW), 500);
        QCOMPARE(radio.filterLayout().second.command, FilterCommand::FW);
        QVERIFY(modeSpy.size() >= 1);
    }

    void widthShiftMenuChangesLayout()
    {
        Radio radio;
        auto transport = std::make_unique<SimulatorTransport>();
        transport->setMenu(28, 1);
        radio.connectTransport(std::move(transport));
        radio.setMeterPolling(false);
        settle(radio);
        QCOMPARE(radio.filterLayout().first.label, QStringLiteral("Shift"));
    }

    void eqLevelsRejectedWhileOff()
    {
        Radio radio;
        connectSim(radio);
        QSignalSpy messages(&radio, &Radio::message);
        EqLevels boost = flatEqLevels();
        boost[5] = 0;
        radio.setEqLevels(EqType::Rx, boost);
        QTRY_COMPARE(messages.size(), 1);
        // Re-read restores what the radio really has.
        QTRY_COMPARE(radio.eqLevels(EqType::Rx), std::optional(flatEqLevels()));
    }

    void programsUserEq()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.setEqCurve(EqType::Tx, EqCurve::User);
        QTRY_COMPARE(sim->eqCurve(EqType::Tx, EqMode::Ssb), EqCurve::User);
        QTRY_COMPARE(radio.link()->pendingCount(), 0);

        EqLevels levels;
        for (int band = 0; band < kEqBandCount; ++band)
            levels[band] = band;   // gentle roll-off
        radio.setEqLevels(EqType::Tx, levels);
        QTRY_COMPARE(sim->eqLevels(EqType::Tx), levels);
    }

    void reportsSilentRadio()
    {
        Radio radio;
        radio.link()->setReplyTimeout(30);
        auto transport = std::make_unique<SimulatorTransport>();
        transport->setUnresponsive(true);
        QSignalSpy messages(&radio, &Radio::message);
        radio.connectTransport(std::move(transport));
        QTRY_COMPARE(radio.state(), Radio::State::Disconnected);
        QCOMPARE(messages.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestRadio)
#include "tst_radio.moc"

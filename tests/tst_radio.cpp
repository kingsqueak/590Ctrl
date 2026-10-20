#include "MenuTable.h"
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

    void agcModesKeepSeparateTimeConstants()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.agc(), Agc::Slow);
        QTRY_COMPARE(radio.agcTimeConstant(), std::optional(12));

        radio.setAgc(Agc::Fast);
        QTRY_COMPARE(radio.agcTimeConstant(), std::optional(4));
        radio.setAgcTimeConstant(7);
        QTRY_COMPARE(sim->agcTime(2), 7);
        QCOMPARE(sim->agcTime(1), 12);

        radio.setAgc(Agc::Off);
        QTRY_COMPARE(radio.agc(), Agc::Off);
        QVERIFY(!radio.agcTimeConstant());
        QTRY_COMPARE(radio.link()->pendingCount(), 0);
        radio.setAgcTimeConstant(9);   // nothing to adjust with AGC off: not sent
        QCOMPARE(radio.link()->pendingCount(), 0);

        // Back on from off (it was FAST): GC3 first, then the mode asked for.
        QSignalSpy rejections(radio.link(), &CatLink::commandRejected);
        radio.setAgc(Agc::Slow);
        QTRY_COMPARE(radio.agc(), Agc::Slow);
        QTRY_COMPARE(radio.link()->pendingCount(), 0);
        QCOMPARE(rejections.size(), 0);
        const QByteArrayList sent = sim->received();
        const qsizetype resume = sent.lastIndexOf("GC3");
        QVERIFY(resume >= 0);
        QCOMPARE(sent.value(resume + 1), QByteArray("GC1"));
        QTRY_COMPARE(radio.agcTimeConstant(), std::optional(12));

        // Off again, then FAST: GC3 restores SLOW, GC2 moves on to FAST.
        radio.setAgc(Agc::Off);
        QTRY_COMPARE(radio.agc(), Agc::Off);
        radio.setAgc(Agc::Fast);
        QTRY_COMPARE(radio.agc(), Agc::Fast);
        QTRY_COMPARE(radio.agcTimeConstant(), std::optional(7));
        QCOMPARE(rejections.size(), 0);

        // In FM the radio refuses GC; that must not surface as an error.
        QSignalSpy messages(&radio, &Radio::message);
        radio.setMode(Mode::FM);
        QTRY_COMPARE(radio.mode(), Mode::FM);
        QTRY_COMPARE(radio.link()->pendingCount(), 0);
        QCOMPARE(messages.size(), 0);
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

    void connectsToSwitchedOffRadioAndPowersItOn()
    {
        Radio radio;
        auto transport = std::make_unique<SimulatorTransport>();
        SimulatorTransport *sim = transport.get();
        sim->setPowered(false);
        radio.connectTransport(std::move(transport));
        radio.setMeterPolling(false);
        QTRY_COMPARE(radio.state(), Radio::State::PoweredOff);
        QCOMPARE(radio.frequency(Vfo::A), 0);

        radio.setPower(true);
        QTRY_COMPARE(radio.state(), Radio::State::Connected);
        QVERIFY(sim->powered());
        QTRY_COMPARE(radio.frequency(Vfo::A), 14'195'000);
    }

    void powersOff()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.setPower(false);
        QCOMPARE(radio.state(), Radio::State::PoweredOff);
        QTRY_VERIFY(!sim->powered());
        QVERIFY(sim->received().contains("PS0"));   // PS0, not PS9: PS1 can wake it
    }

    void wakesPanelOffRadio()
    {
        Radio radio;
        auto transport = std::make_unique<SimulatorTransport>();
        transport->setPanelOff(true);
        SimulatorTransport *sim = transport.get();
        radio.powerOnAndConnect(std::move(transport));
        radio.setMeterPolling(false);
        QTRY_COMPARE_WITH_TIMEOUT(radio.state(), Radio::State::Connected, 6000);
        QVERIFY(sim->powered());
        const QByteArrayList sent = sim->received();
        QCOMPARE(sent.value(0), QByteArray());   // the ";" before PS1
        QCOMPARE(sent.value(1), QByteArray("PS1"));
    }

    void panelOffRadioFailsPlainConnect()
    {
        Radio radio;
        radio.link()->setReplyTimeout(30);
        auto transport = std::make_unique<SimulatorTransport>();
        transport->setPanelOff(true);
        QSignalSpy messages(&radio, &Radio::message);
        radio.connectTransport(std::move(transport));
        QTRY_COMPARE(radio.state(), Radio::State::Disconnected);
        QVERIFY(messages.value(0).value(0).toString().contains(QStringLiteral("power button")));
    }

    void transmitAndVox()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.setVox(true);
        QTRY_VERIFY(radio.vox());
        QVERIFY(sim->vox());

        radio.setTransmit(true);
        QTRY_VERIFY(radio.transmitting());
        QVERIFY(sim->transmitting());
        radio.setTransmit(false);
        QTRY_VERIFY(!radio.transmitting());
        QVERIFY(!sim->transmitting());
    }

    void tuneCycleAndTuneCarrier()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.setTunerTuning(true);
        QTRY_VERIFY(radio.tunerTuning());
        QVERIFY(radio.tuner());               // tuning puts the tuner in line
        QTRY_VERIFY(!radio.tunerTuning());    // and finishes by itself
        QTRY_VERIFY(!radio.transmitting());

        radio.setTuneCarrier(true);
        QTRY_VERIFY(radio.transmitting());
        QVERIFY(sim->received().contains("TX2"));
        radio.setTuneCarrier(false);
        QTRY_VERIFY(!radio.transmitting());
    }

    void disconnectReleasesTransmit()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.setTransmit(true);
        QTRY_VERIFY(radio.transmitting());
        QPointer<SimulatorTransport> guard(sim);
        radio.disconnectFromRadio();
        QVERIFY(guard);
        const QByteArrayList sent = sim->received();
        QCOMPARE(sent.value(sent.size() - 2), QByteArray("RX"));   // then the AI restore
        QVERIFY(!sim->transmitting());
    }

    void voxGainAntennaAndAttenuator()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.voxGain(), std::optional(4));
        QCOMPARE(radio.antenna(), 1);

        radio.setVoxGain(7);
        QTRY_COMPARE(sim->voxGain(), 7);

        radio.setAntenna(2);
        QTRY_COMPARE(radio.antenna(), 2);
        radio.setRxAntenna(true);
        QTRY_VERIFY(radio.rxAntenna());
        radio.setDriveOut(true);
        QTRY_VERIFY(radio.driveOut());
        QCOMPARE(sim->antenna(), QByteArray("211"));   // each set touched only its field

        radio.setAttenuator(true);
        QTRY_VERIFY(radio.attenuator());
        QVERIFY(sim->attenuator());

        radio.setPreamp(true);
        QTRY_VERIFY(radio.preamp());
        QVERIFY(sim->preamp());

        radio.setTuner(true);
        QTRY_VERIFY(radio.tuner());
        QCOMPARE(sim->tuner(), QByteArray("010"));   // in line, not tuning
        radio.setTuner(false);
        QTRY_VERIFY(!radio.tuner());
    }

    void bandSelection()
    {
        Radio radio;
        connectSim(radio);
        radio.selectBand(2);
        QTRY_COMPARE(radio.frequency(Vfo::A), 7'074'000);
        QCOMPARE(bandForFrequency(radio.frequency(Vfo::A)), 2);
        radio.selectBand(4);   // back to 20 m: its memory kept 14.195
        QTRY_COMPARE(radio.frequency(Vfo::A), 14'195'000);
    }

    void speechProcessor()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QVERIFY(!radio.processor());
        QCOMPARE(radio.processorInput(), std::optional(50));

        radio.setProcessor(true);
        QTRY_VERIFY(radio.processor());
        radio.setProcessorLevels(30, 70);
        QTRY_COMPARE(sim->processorInput(), 30);
        QCOMPARE(sim->processorOutput(), 70);
        radio.setProcessor(false);
        QTRY_VERIFY(!sim->processor());
    }

    void beatCancel()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.beatCancel(), BeatCancel::Off);
        radio.setBeatCancel(BeatCancel::BC2);
        QTRY_COMPARE(radio.beatCancel(), BeatCancel::BC2);
        QCOMPARE(sim->beatCancel(), 2);

        // Not available in CW: nothing is sent, so the radio can't reject it.
        radio.setMode(Mode::CW);
        QTRY_COMPARE(radio.mode(), Mode::CW);
        QSignalSpy messages(&radio, &Radio::message);
        radio.setBeatCancel(BeatCancel::Off);
        settle(radio);
        QVERIFY(!sim->received().contains("BC0"));
        QCOMPARE(messages.count(), 0);
    }

    void busyFollowsSquelch()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.setMeterPolling(true);
        sim->setSMeter(5);
        sim->setSquelchLevel(0);
        QTRY_VERIFY(radio.busy());
        sim->setSquelchLevel(200);
        QTRY_VERIFY(!radio.busy());
    }

    void keyerSettings()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.keyerSpeed(), std::optional(20));
        QCOMPARE(radio.breakInDelay(), std::optional(300));
        QCOMPARE(radio.cwPitch(), std::optional(6));

        radio.setKeyerSpeed(28);
        radio.setBreakInDelay(0);
        radio.setCwPitch(8);
        QTRY_COMPARE(sim->keyerSpeed(), 28);
        QTRY_COMPARE(sim->breakInDelay(), 0);
        QTRY_COMPARE(sim->menu(40), 8);
        QVERIFY(sim->received().contains("EX040000008"));   // two-digit value
    }

    void cwTuneNeedsCwMode()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        radio.setCwTune(true);   // USB: not sent
        settle(radio);
        QVERIFY(!sim->received().contains("CA1"));

        radio.setMode(Mode::CW);
        QTRY_COMPARE(radio.mode(), Mode::CW);
        radio.setCwTune(true);
        QTRY_VERIFY(radio.cwTune());
        QTRY_VERIFY(!radio.cwTune());   // finishes on its own
    }

    void keyboardCw()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QSignalSpy messages(&radio, &Radio::message);
        radio.sendCw(QStringLiteral("cq"));   // USB: refused
        QCOMPARE(messages.count(), 1);
        QCOMPARE(radio.cwQueued(), 0);

        radio.setMode(Mode::CW);
        QTRY_COMPARE(radio.mode(), Mode::CW);
        radio.sendCw(QStringLiteral("cq cq cq de kc2rgw kc2rgw kc2rgw k"));
        radio.sendCw(QStringLiteral("pse k"));
        QTRY_COMPARE(radio.cwQueued(), 0);
        QTRY_COMPARE(sim->keyedCw(), QByteArray(" CQ CQ CQ DE KC2RGW KC2RGW KC2RGW K PSE K"));
    }

    void morseDecoder()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QVERIFY(!radio.morseDecoder());
        QCOMPARE(radio.decoderThreshold(), std::optional(10));
        radio.setMode(Mode::CW);
        QSignalSpy decoded(&radio, &Radio::morseDecoded);
        radio.setMorseDecoder(true);
        QTRY_VERIFY(radio.morseDecoder());
        QTRY_VERIFY(decoded.count() >= 3);
        QCOMPARE(decoded.at(0).at(0).toString() + decoded.at(1).at(0).toString() + decoded.at(2).at(0).toString(),
                 QStringLiteral("CQ "));
        radio.setDecoderThreshold(15);
        QTRY_COMPARE(sim->decoderThreshold(), 15);
        radio.setMorseDecoder(false);
        QTRY_VERIFY(!sim->morseDecoder());
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

    void voxDelayLockAndFineTuning()
    {
        Radio radio;
        connectSim(radio);
        QCOMPARE(radio.voxDelay(), std::optional(300));
        QVERIFY(!radio.locked() && !radio.muted() && !radio.fineTuning());

        radio.setVoxDelay(500);   // rounded down to 150 ms steps
        QTRY_COMPARE(radio.voxDelay(), std::optional(450));
        QCOMPARE(cmd::voxDelay(5000), QByteArray("VD3000;"));

        QSignalSpy locked(&radio, &Radio::lockChanged);
        radio.setLock(true);
        QTRY_VERIFY(radio.locked());
        QCOMPARE(locked.count(), 1);
        radio.setFineTuning(true);
        QTRY_VERIFY(radio.fineTuning());
    }

    void muteAlsoSilencesUsbOutput()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        const int usbOut = menuNumbersFor(kModelTs590SG).usbAudioOut;
        QCOMPARE(radio.usbAudioOut(), std::optional(5));

        QSignalSpy levels(&radio, &Radio::usbAudioOutChanged);
        radio.setMute(true);
        QTRY_VERIFY(radio.muted());
        settle(radio);
        QCOMPARE(sim->menu(usbOut), 0);
        QCOMPARE(radio.usbAudioOut(), std::optional(0));
        QCOMPARE(levels.count(), 1);   // the USB out slider follows

        radio.setMute(false);
        QTRY_VERIFY(!radio.muted());
        settle(radio);
        QCOMPARE(sim->menu(usbOut), 5);
        QCOMPARE(radio.usbAudioOut(), std::optional(5));

        // A level set while muted is kept on unmute.
        radio.setMute(true);
        settle(radio);
        radio.setUsbAudioOut(3);
        radio.setMute(false);
        settle(radio);
        QCOMPARE(sim->menu(usbOut), 3);
    }

    void readsMenuBankAtConnect()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        QCOMPARE(radio.menuBank(), std::optional(MenuBank::A));

        // Switching sets changes every menu setting, so the cached ones follow.
        const int usbOut = menuNumbersFor(kModelTs590SG).usbAudioOut;
        sim->setMenu(MenuBank::B, usbOut, 8);
        QSignalSpy bankChanged(&radio, &Radio::menuBankChanged);
        radio.selectMenuBank(MenuBank::B);
        QCOMPARE(bankChanged.count(), 1);
        QTRY_COMPARE(sim->menuBank(), MenuBank::B);
        QTRY_COMPARE(radio.usbAudioOut(), std::optional(8));
    }

    void readsAndWritesTheOtherMenuBank()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        const int usbOut = menuNumbersFor(kModelTs590SG).usbAudioOut;
        sim->setMenu(MenuBank::B, usbOut, 9);

        QSignalSpy read(&radio, &Radio::menuItemRead);
        radio.readMenuItems(MenuBank::B, {usbOut});
        settle(radio);
        QCOMPARE(read.count(), 1);
        QCOMPARE(read[0][0].value<MenuBank>(), MenuBank::B);
        QCOMPARE(read[0][1].toInt(), usbOut);
        QCOMPARE(read[0][2].toByteArray(), QByteArray("9"));
        // Read in passing: menu A stays selected and its value stays cached.
        QCOMPARE(sim->menuBank(), MenuBank::A);
        QCOMPARE(radio.usbAudioOut(), std::optional(5));

        radio.writeMenuItem(MenuBank::B, usbOut, "3");
        settle(radio);
        QCOMPARE(sim->menu(MenuBank::B, usbOut), 3);
        QCOMPARE(sim->menu(MenuBank::A, usbOut), 5);
        QCOMPARE(sim->menuBank(), MenuBank::A);
        QCOMPARE(radio.usbAudioOut(), std::optional(5));

        // A write to the set in use updates the cached setting too.
        radio.writeMenuItem(MenuBank::A, usbOut, "2");
        QTRY_COMPARE(radio.usbAudioOut(), std::optional(2));
    }

    void menuTablesMatchKnownMenus()
    {
        for (int model : {kModelTs590S, kModelTs590SG}) {
            const MenuNumbers menus = menuNumbersFor(model);
            QVERIFY(findMenuItem(model, menus.usbAudioIn)->name.startsWith(QStringLiteral("USB audio input")));
            QVERIFY(findMenuItem(model, menus.powerFine)->name == QStringLiteral("Power fine"));
            QVERIFY(findMenuItem(model, menus.cwPitch)->name.startsWith(QStringLiteral("Sidetone")));
            QCOMPARE(findMenuItem(model, menus.cwPitch)->choices.size(), 15);   // 300..1000 Hz
            QVERIFY(findMenuItem(model, menus.txFilter[3])->name.startsWith(QStringLiteral("SSB-DATA TX filter high")));
            // Numbered 000.. without gaps.
            const QList<MenuItem> &items = menuItemsFor(model);
            for (int i = 0; i < items.size(); ++i)
                QCOMPARE(items[i].number, i);
        }
        QCOMPARE(menuItemsFor(kModelTs590S).size(), 88);
        QCOMPARE(menuItemsFor(kModelTs590SG).size(), 100);
        QVERIFY(findMenuItem(kModelTs590SG, 68)->locked);   // USB port speed

        const MenuItem *weight = findMenuItem(kModelTs590SG, 42);
        QCOMPARE(menuValueText(*weight, "00"), QStringLiteral("AUTO"));
        QCOMPARE(menuValueText(*weight, "16"), QStringLiteral("4.0"));
        QCOMPARE(menuValueText(*weight, "99"), QStringLiteral("99"));
        QCOMPARE(menuValueText(*findMenuItem(kModelTs590SG, 87), "255"), QStringLiteral("OFF"));
        QCOMPARE(menuIndexValue(6, 2), QByteArray("06"));
        QCOMPARE(menuValueText(*findMenuItem(kModelTs590SG, 16), "2"), QStringLiteral("1"));   // kHz, on hardware
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

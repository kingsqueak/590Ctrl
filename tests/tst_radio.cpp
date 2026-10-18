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

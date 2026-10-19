#include "Radio.h"
#include "RadioAudio.h"
#include "SimulatorTransport.h"
#include "TciServer.h"

#include <QSignalSpy>
#include <QTest>
#include <QWebSocket>
#include <QtEndian>

using namespace ts590;

// A TCI client that records every command it receives.
class Client : public QObject
{
public:
    explicit Client(quint16 port)
    {
        connect(&socket, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
            for (const QString &part : text.split(QLatin1Char(';'), Qt::SkipEmptyParts))
                received.append(part.toLower());
        });
        connect(&socket, &QWebSocket::binaryMessageReceived, this,
                [this](const QByteArray &data) { binary.append(data); });
        socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port)));
    }

    bool waitReady()
    {
        return QTest::qWaitFor([this] { return received.contains(QStringLiteral("ready")); });
    }
    void send(const QString &text) { socket.sendTextMessage(text); }
    bool has(const QString &command) const { return received.contains(command); }
    bool waitFor(const QString &command)
    {
        return QTest::qWaitFor([&] { return has(command); });
    }

    QWebSocket socket;
    QStringList received;
    QList<QByteArray> binary;
};

quint32 headerWord(const QByteArray &block, int index)
{
    return qFromLittleEndian<quint32>(block.constData() + index * 4);
}

QByteArray txAudioBlock(int rate, int frames)
{
    QByteArray block(64 + frames * 2 * 4, '\0');
    const quint32 words[] = {0, quint32(rate), 3, 0, 0, quint32(frames * 2), 2, 2};
    for (int i = 0; i < 8; ++i)
        qToLittleEndian(words[i], block.data() + i * 4);
    return block;
}

class TestTciServer : public QObject
{
    Q_OBJECT

private:
    SimulatorTransport *connectSim(Radio &radio)
    {
        auto transport = std::make_unique<SimulatorTransport>();
        SimulatorTransport *sim = transport.get();
        radio.connectTransport(std::move(transport));
        radio.setMeterPolling(false);
        const bool synced = QTest::qWaitFor([&] {
            return radio.state() == Radio::State::Connected && radio.link()->pendingCount() == 0;
        });
        if (!synced)
            qWarning("radio did not finish its initial sync");
        return sim;
    }

private slots:
    void modulationNames()
    {
        QCOMPARE(tci::modulationName(Mode::USB, true), QByteArray("DIGU"));
        QCOMPARE(tci::modulationName(Mode::LSB, false), QByteArray("LSB"));
        QCOMPARE(tci::modulationName(Mode::FM, false), QByteArray("NFM"));
        QCOMPARE(tci::modulationFromName("digl"), std::optional(std::pair{Mode::LSB, true}));
        QCOMPARE(tci::modulationFromName("CWL"), std::optional(std::pair{Mode::CWR, false}));
        QVERIFY(!tci::modulationFromName("WFM"));
        QCOMPARE(tci::sMeterDbm(kSMeterS9Dots), -73.0);
    }

    void greetsWithInitAndState()
    {
        Radio radio;
        connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        Client client(server.port());
        QVERIFY(client.waitReady());
        QVERIFY(client.has(QStringLiteral("protocol:590ctrl,1.9")));
        QVERIFY(client.has(QStringLiteral("device:ts-590sg")));
        QVERIFY(client.has(QStringLiteral("vfo:0,0,14195000")));
        QVERIFY(client.has(QStringLiteral("vfo:0,1,7074000")));
        QVERIFY(client.has(QStringLiteral("modulation:0,usb")));
        QVERIFY(client.has(QStringLiteral("trx:0,false")));
        QVERIFY(client.has(QStringLiteral("start")));
        // READY comes after the state.
        QVERIFY(client.received.indexOf(QStringLiteral("ready"))
                > client.received.indexOf(QStringLiteral("vfo:0,0,14195000")));
        QCOMPARE(server.clientCount(), 1);
        QCOMPARE(server.address(), QHostAddress(QHostAddress::LocalHost));
    }

    void sendsCommandNamesInLowerCase()
    {
        // As ExpertSDR3 does; some amplifiers and tuners match it exactly.
        Radio radio;
        connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        QStringList raw;
        connect(&server, &TciServer::traffic, this, [&](const QString &, bool incoming, const QString &text) {
            if (!incoming)
                raw.append(text);
        });
        Client client(server.port());
        QVERIFY(client.waitReady());
        QVERIFY(raw.contains(QStringLiteral("vfo:0,0,14195000;")));
        QVERIFY(raw.contains(QStringLiteral("ready;")));
        QVERIFY(raw.contains(QStringLiteral("start;")));
        QVERIFY(raw.contains(QStringLiteral("protocol:590Ctrl,1.9;")));   // values keep their case
    }

    void listensOnTheGivenAddress()
    {
        Radio radio;
        TciServer server(&radio);
        QVERIFY(server.listen(0, QHostAddress(QStringLiteral("0.0.0.0"))));
        QCOMPARE(server.address(), QHostAddress(QHostAddress::AnyIPv4));
        // An address this computer doesn't have can't be listened on.
        QVERIFY(!server.listen(0, QHostAddress(QStringLiteral("203.0.113.7"))));
        QVERIFY(!server.isListening());
    }

    void tunesAndSyncsOtherClients()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        Client a(server.port()), b(server.port());
        QVERIFY(a.waitReady() && b.waitReady());

        a.send(QStringLiteral("VFO:0,0,7074000;"));
        QTRY_COMPARE(sim->frequency(Vfo::A), 7'074'000);
        QVERIFY(b.waitFor(QStringLiteral("vfo:0,0,7074000")));

        // A change made in this app reaches the clients too, though the
        // radio doesn't report CAT-made changes back.
        radio.setFrequency(Vfo::A, 14'074'000);
        QVERIFY(b.waitFor(QStringLiteral("vfo:0,0,14074000")));
        radio.setTxPower(35);
        QVERIFY(b.waitFor(QStringLiteral("drive:0,35")));
        radio.setFrequency(Vfo::B, 10'136'000);
        radio.selectVfo(Vfo::B);
        QVERIFY(a.waitFor(QStringLiteral("vfo:0,0,10136000")));
    }

    void setsModeWithData()
    {
        Radio radio;
        connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        Client client(server.port());
        QVERIFY(client.waitReady());

        radio.setMode(Mode::CW);
        QTRY_COMPARE(radio.mode(), Mode::CW);
        client.send(QStringLiteral("modulation:0,DIGU;"));
        QTRY_VERIFY(radio.mode() == Mode::USB && radio.dataMode());
        QVERIFY(client.waitFor(QStringLiteral("modulation:0,digu")));
    }

    void keysDataSendAndReleasesOnDisconnect()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        auto client = std::make_unique<Client>(server.port());
        QVERIFY(client->waitReady());

        client->send(QStringLiteral("trx:0,true,tci;"));
        QTRY_VERIFY(radio.transmitting());
        QVERIFY(sim->received().contains("TX1"));
        QVERIFY(client->waitFor(QStringLiteral("trx:0,true")));

        // The client goes away without unkeying.
        client.reset();
        QTRY_VERIFY(!radio.transmitting());
        QTRY_COMPARE(server.clientCount(), 0);
    }

    void tuneUsesTuneCarrier()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        Client client(server.port());
        QVERIFY(client.waitReady());

        client.send(QStringLiteral("tune:0,true;"));
        QVERIFY(client.waitFor(QStringLiteral("tune:0,true")));
        QVERIFY(sim->received().contains("TX2"));
        client.send(QStringLiteral("tune:0,false;"));
        QVERIFY(client.waitFor(QStringLiteral("tune:0,false")));
        QTRY_VERIFY(!radio.transmitting());
    }

    void setsDriveAndFilter()
    {
        Radio radio;
        SimulatorTransport *sim = connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        Client client(server.port());
        QVERIFY(client.waitReady());

        client.send(QStringLiteral("drive:0,40;"));
        QTRY_COMPARE(sim->txPower(), 40);
        QVERIFY(client.waitFor(QStringLiteral("drive:0,40")));

        // USB, high/low cut: 300..2600 Hz -> SL and SH nearest those.
        client.send(QStringLiteral("rx_filter_band:0,300,2600;"));
        QVERIFY(client.waitFor(QStringLiteral("rx_filter_band:0,300,2600")));
    }

    void answersReadsAndAudioFormat()
    {
        Radio radio;
        connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        Client client(server.port());
        QVERIFY(client.waitReady());
        client.received.clear();

        client.send(QStringLiteral("vfo:0,0;audio_samplerate:24000;audio_stream_sample_type:int16;"));
        QVERIFY(client.waitFor(QStringLiteral("vfo:0,0,14195000")));
        QVERIFY(client.waitFor(QStringLiteral("audio_samplerate:24000")));
        QVERIFY(client.waitFor(QStringLiteral("audio_stream_sample_type:int16")));
    }

    void ignoresStopFromClient()
    {
        Radio radio;
        connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        Client client(server.port());
        QVERIFY(client.waitReady());
        client.received.clear();
        client.send(QStringLiteral("stop;"));
        QVERIFY(client.waitFor(QStringLiteral("start")));
        QCOMPARE(radio.state(), Radio::State::Connected);
    }

    void reportsSignalLevel()
    {
        Radio radio;
        connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        Client client(server.port());
        QVERIFY(client.waitReady());
        client.send(QStringLiteral("rx_sensors_enable:true,30;"));
        QTest::qWait(50);
        radio.setMeterPolling(true);
        QVERIFY(QTest::qWaitFor([&] {
            return std::any_of(client.received.cbegin(), client.received.cend(),
                               [](const QString &c) { return c.startsWith(QStringLiteral("rx_sensors:0,")); });
        }));
    }

    void streamsReceiveAudioInTheClientsFormat()
    {
        Radio radio;
        connectSim(radio);
        RadioAudio audio;   // no devices: start fails, captured is driven by hand
        TciServer server(&radio);
        server.setAudio(&audio);
        QSignalSpy errors(&server, &TciServer::audioError);
        QVERIFY(server.listen(0));
        Client client(server.port());
        QVERIFY(client.waitReady());
        client.send(QStringLiteral("audio_samplerate:24000;audio_stream_sample_type:int16;"
                                   "audio_stream_channels:1;audio_stream_samples:512;audio_start:0;"));
        QVERIFY(client.waitFor(QStringLiteral("audio_stream_samples:512")));
        QTRY_COMPARE(errors.count(), 1);   // reported once

        // 2048 codec frames at 48 kHz -> 1024 at 24 kHz -> two 512 packets.
        QList<float> frames(2048 * 2, 0.25f);
        emit audio.captured(frames);
        QTRY_COMPARE(client.binary.size(), 2);
        const QByteArray &block = client.binary[1];
        QCOMPARE(headerWord(block, 1), 24000u);   // rate
        QCOMPARE(headerWord(block, 2), 0u);       // int16
        QCOMPARE(headerWord(block, 5), 512u);     // samples, all channels
        QCOMPARE(headerWord(block, 6), 1u);       // RX audio
        QCOMPARE(headerWord(block, 7), 1u);       // mono
        QCOMPARE(headerWord(block, 8), 1u);       // second packet
        QCOMPARE(block.size(), 64 + 512 * 2);
        // DC passes the filter: the last sample is 0.25 full scale.
        QCOMPARE(qFromLittleEndian<qint16>(block.constData() + block.size() - 2), qint16(8192));

        client.send(QStringLiteral("audio_stop:0;"));
        QTest::qWait(100);
        emit audio.captured(frames);
        QTest::qWait(100);
        QCOMPARE(client.binary.size(), 2);
    }

    void pacesTxChronoAndStopsWithTx()
    {
        Radio radio;
        connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        Client client(server.port());
        QVERIFY(client.waitReady());
        // Answer every chrono with the samples it asks for.
        int chronos = 0;
        connect(&client.socket, &QWebSocket::binaryMessageReceived, &client, [&](const QByteArray &data) {
            if (headerWord(data, 6) != 3)
                return;
            ++chronos;
            QCOMPARE(headerWord(data, 5), 2048u * 2);
            QCOMPARE(headerWord(data, 7), 2u);
            client.socket.sendBinaryMessage(txAudioBlock(48000, 2048));
        });
        client.send(QStringLiteral("audio_start:0;trx:0,true,tci;"));
        QVERIFY(client.waitFor(QStringLiteral("trx:0,true")));
        QTest::qWait(1000);
        // 100 ms of buffering plus a block up front, then ~23.4 a second.
        QVERIFY2(chronos >= 18 && chronos <= 32, qPrintable(QString::number(chronos)));

        // The greeting had TRX false too; wait for the new one.
        client.received.clear();
        client.send(QStringLiteral("trx:0,false;"));
        QVERIFY(client.waitFor(QStringLiteral("trx:0,false")));
        const int atStop = chronos;
        QTest::qWait(300);
        QCOMPARE(chronos, atStop);
    }

    void reportsStopWhenRadioDisconnects()
    {
        Radio radio;
        connectSim(radio);
        TciServer server(&radio);
        QVERIFY(server.listen(0));
        Client client(server.port());
        QVERIFY(client.waitReady());
        radio.disconnectFromRadio();
        QVERIFY(client.waitFor(QStringLiteral("stop")));
        QVERIFY(client.waitFor(QStringLiteral("tx_enable:0,false")));
    }
};

QTEST_MAIN(TestTciServer)
#include "tst_tciserver.moc"

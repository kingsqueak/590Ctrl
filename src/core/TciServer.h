#pragma once

#include "CatCodec.h"
#include "Resampler.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QTimer>

#include <deque>
#include <optional>
#include <utility>

class QWebSocket;
class QWebSocketServer;

namespace ts590 {

class Radio;
class RadioAudio;

namespace tci {

constexpr quint16 kDefaultPort = 40001;

// TCI modulation names. USB/LSB with DATA on are DIGU/DIGL; AM and FM with
// DATA report as plain AM/NFM.
QByteArray modulationName(Mode mode, bool data);
// Case-insensitive; accepts the usual aliases (CWU, CWL, FM, ...).
std::optional<std::pair<Mode, bool>> modulationFromName(QByteArrayView name);
// TCI signal level for an S-meter reading: S9 (15 dots) = -73 dBm, 6 dB per
// S unit below and 4 dB per dot above, which works out to 4 dB per dot.
double sMeterDbm(int dots);

}   // namespace tci

// A TCI (Transceiver Control Interface) server for the radio, so programs
// such as fldigi can share it while this app holds the serial port. Clients
// connect over a WebSocket; their commands go through Radio, and changes
// from any source are broadcast to every client.
//
// The TS-590SG is one transceiver (0) with two channels: channel 0 is the
// receive VFO and channel 1 the other one. There's no panorama, so IF is
// always 0 and DDS follows the receive VFO.
//
// With a RadioAudio set, the radio's USB codec is served as the receiver
// audio (and Line Out) stream, and a client that keys with TRX:0,true,tci
// gets TX_CHRONO requests and its TX_AUDIO goes to the codec. The requests
// are paced by what the codec has played, so the codec's clock rules.
class TciServer : public QObject
{
    Q_OBJECT

public:
    explicit TciServer(Radio *radio, QObject *parent = nullptr);
    ~TciServer() override;

    // Not owned. The server starts it while a client wants audio.
    void setAudio(RadioAudio *audio);

    bool listen(quint16 port, const QHostAddress &address = QHostAddress::LocalHost);
    void close();
    bool isListening() const;
    quint16 port() const;
    QHostAddress address() const;
    QString errorString() const;
    int clientCount() const { return int(m_clients.size()); }

signals:
    void clientsChanged(int count);
    // Command text in either direction, for the monitor. peer is "address:port".
    void traffic(const QString &peer, bool incoming, const QString &text);
    // Clients connecting and leaving, and handshakes that fail.
    void event(const QString &text);
    void audioError(const QString &message);

private:
    struct Client {
        bool rxSensors = false;
        int rxSensorsMs = 200;
        QElapsedTimer rxSensorsSent;
        bool txSensors = false;
        int txSensorsMs = 200;
        QElapsedTimer txSensorsSent;
        bool audio = false;
        bool lineOut = false;
        int sampleRate = 48000;
        QByteArray sampleType = "float32";
        int channels = 2;
        int samples = 2048;
        int txBufferingMs = 100;
        // Receive: codec stereo down to sampleRate, then whole packets.
        Resampler rxResampler;
        QList<float> rxPending;     // interleaved, `channels` wide
        QList<float> linePending;   // interleaved stereo
        quint32 rxCount = 0;
        quint32 lineCount = 0;
        // Transmit: mono from sampleRate up to the codec's rate.
        Resampler txResampler;
    };

    void onNewConnection();
    void onDisconnected(QWebSocket *socket);
    void onText(QWebSocket *socket, const QString &text);
    void handleCommand(QWebSocket *socket, Client &client, const QByteArray &name, const QList<QByteArray> &args);
    void sendTo(QWebSocket *socket, const QByteArray &command);
    void broadcast(const QByteArray &key, const QByteArray &command);
    void refreshState();
    QMap<QByteArray, QByteArray> currentState() const;
    void sendSensors();
    void releaseTransmit();
    void setSampleRate(Client &client, int rate);
    void updateAudio();
    void onCaptured(const QList<float> &frames);
    void onBinary(QWebSocket *socket, const QByteArray &data);
    void startTxStream(QWebSocket *socket);
    void stopTxStream();
    void requestTxAudio();

    static QString peerName(const QWebSocket *socket);
    Vfo vfoForChannel(int channel) const;
    std::optional<std::pair<int, int>> filterBand() const;
    void setFilterBand(int low, int high);

    Radio *m_radio;
    QWebSocketServer *m_server;
    QHash<QWebSocket *, Client> m_clients;
    // Last message sent for each state key (e.g. "VFO:0,0"), so a change is
    // broadcast once and a new client can be given the whole state.
    QMap<QByteArray, QByteArray> m_sent;
    QWebSocket *m_keyedBy = nullptr;   // client that put the radio in TX
    bool m_tuning = false;             // TX came from TUNE
    QTimer m_refreshSoon;              // after commands go to the radio
    QTimer m_sensorTimer;              // sensor notifications repeat even when the reading doesn't change

    RadioAudio *m_audio = nullptr;
    bool m_audioFailed = false;          // reported once until audio works again
    QWebSocket *m_txClient = nullptr;    // its TX_AUDIO feeds the codec
    QTimer m_txTimer;
    QElapsedTimer m_clock;
    quint32 m_chronoCount = 0;
    // Codec frames asked for and not yet answered, with when they were asked.
    std::deque<std::pair<qint64, qint64>> m_requested;
    qint64 m_silentUntilMs = 0;          // without a codec: when the audio "played" so far ends
};

}   // namespace ts590

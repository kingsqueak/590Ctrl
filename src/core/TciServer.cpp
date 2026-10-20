#include "TciServer.h"

#include "Radio.h"
#include "RadioAudio.h"

#include <QtEndian>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ts590 {

namespace {

constexpr int kSensorsMinMs = 30;
constexpr int kSensorsMaxMs = 1000;
// Audio offsets the IF can cover; the 590 has no panorama, so just the
// receive passband.
constexpr int kIfLimitHz = 24'000;

// Binary stream blocks.
constexpr int kHeaderBytes = 64;
enum StreamType : quint32 { RxAudioStream = 1, TxAudioStream = 2, TxChrono = 3, LineOutStream = 4 };
constexpr int kTxTickMs = 5;
// A TX_CHRONO nobody answers stops counting as audio on the way after this.
constexpr qint64 kChronoAnswerMs = 300;
constexpr int kTxBufferingMinMs = 50;
constexpr int kTxBufferingMaxMs = 500;

int sampleTypeCode(const QByteArray &type)
{
    return type == "int16" ? 0 : type == "int24" ? 1 : type == "int32" ? 2 : 3;
}

int sampleBytes(int code) { return code == 0 ? 2 : code == 1 ? 3 : 4; }

QByteArray header(quint32 rate, quint32 format, quint32 length, quint32 type, quint32 channels, quint32 count)
{
    quint32 words[16] = {0, rate, format, 0, 0, length, type, channels, count};
    QByteArray out(kHeaderBytes, '\0');
    for (int i = 0; i < 16; ++i)
        qToLittleEndian(words[i], out.data() + i * 4);
    return out;
}

void appendSamples(QByteArray &out, const float *samples, qsizetype count, int code)
{
    const int bytes = sampleBytes(code);
    const qsizetype start = out.size();
    out.resize(start + count * bytes);
    char *p = out.data() + start;
    for (qsizetype i = 0; i < count; ++i, p += bytes) {
        const float v = std::clamp(samples[i], -1.f, 1.f);
        switch (code) {
        case 0: qToLittleEndian(qint16(std::lround(v * 32767.0)), p); break;
        case 1: {
            const qint32 x = qint32(std::lround(v * 8388607.0));
            p[0] = char(x & 0xff);
            p[1] = char((x >> 8) & 0xff);
            p[2] = char((x >> 16) & 0xff);
            break;
        }
        case 2: qToLittleEndian(qint32(std::llround(v * 2147483647.0)), p); break;
        default: qToLittleEndian(v, p); break;
        }
    }
}

float sampleAt(const char *p, int code)
{
    switch (code) {
    case 0: return qFromLittleEndian<qint16>(p) / 32768.f;
    case 1: {
        qint32 x = quint8(p[0]) | (quint8(p[1]) << 8) | (quint8(p[2]) << 16);
        if (x & 0x800000)
            x |= ~0xffffff;
        return x / 8388608.f;
    }
    case 2: return float(qFromLittleEndian<qint32>(p) / 2147483648.0);
    default: return qFromLittleEndian<float>(p);
    }
}

QByteArray boolText(bool on) { return on ? "true" : "false"; }

std::optional<bool> parseBool(const QByteArray &text)
{
    const QByteArray t = text.toLower();
    if (t == "true" || t == "1")
        return true;
    if (t == "false" || t == "0")
        return false;
    return std::nullopt;
}

std::optional<qint64> parseInt(const QByteArray &text)
{
    bool ok = false;
    const qint64 v = text.toLongLong(&ok);
    if (ok)
        return v;
    // Some clients send frequencies as "7074000.0".
    const double d = text.toDouble(&ok);
    return ok && std::isfinite(d) ? std::optional<qint64>(std::llround(d)) : std::nullopt;
}

// Names go out in lower case, as ExpertSDR3 sends them. The spec says case
// doesn't matter, but some clients (amplifiers, tuners) match it exactly.
QByteArray command(QByteArrayView name, std::initializer_list<QByteArray> args = {})
{
    QByteArray out = name.toByteArray().toLower();
    if (args.size() > 0) {
        out += ':';
        out += QList<QByteArray>(args).join(',');
    }
    return out + ';';
}

QByteArray num(qint64 v) { return QByteArray::number(v); }

// Index of the value in `values` nearest to hz.
int nearestIndex(const QList<int> &values, int hz)
{
    int best = 0;
    for (int i = 1; i < values.size(); ++i) {
        if (std::abs(values[i] - hz) < std::abs(values[best] - hz))
            best = i;
    }
    return best;
}

}   // namespace

namespace tci {

// Lower case, as ExpertSDR3 sends them. WSJT-X compares modes case-sensitively
// and drops the connection with "TCI failed set mode" if it gets "DIGU".
QByteArray modulationName(Mode mode, bool data)
{
    switch (mode) {
    case Mode::LSB: return data ? "digl" : "lsb";
    case Mode::USB: return data ? "digu" : "usb";
    case Mode::CW: return "cw";
    case Mode::CWR: return "cwr";
    case Mode::FM: return "nfm";
    case Mode::AM: return "am";
    case Mode::FSK: return "fsk";
    case Mode::FSKR: return "fskr";
    case Mode::None: break;
    }
    return {};
}

std::optional<std::pair<Mode, bool>> modulationFromName(QByteArrayView name)
{
    const QByteArray n = name.toByteArray().toUpper();
    if (n == "LSB")
        return std::pair{Mode::LSB, false};
    if (n == "USB")
        return std::pair{Mode::USB, false};
    if (n == "DIGL")
        return std::pair{Mode::LSB, true};
    if (n == "DIGU")
        return std::pair{Mode::USB, true};
    if (n == "CW" || n == "CWU")
        return std::pair{Mode::CW, false};
    if (n == "CWR" || n == "CWL")
        return std::pair{Mode::CWR, false};
    if (n == "NFM" || n == "FM")
        return std::pair{Mode::FM, false};
    if (n == "AM" || n == "SAM" || n == "DSB")
        return std::pair{Mode::AM, false};
    if (n == "FSK" || n == "RTTY")
        return std::pair{Mode::FSK, false};
    if (n == "FSKR")
        return std::pair{Mode::FSKR, false};
    return std::nullopt;
}

double sMeterDbm(int dots) { return -133.0 + 4.0 * dots; }

}   // namespace tci

TciServer::TciServer(Radio *radio, QObject *parent)
    : QObject(parent)
    , m_radio(radio)
    , m_server(new QWebSocketServer(QStringLiteral("590Ctrl"), QWebSocketServer::NonSecureMode, this))
{
    connect(m_server, &QWebSocketServer::newConnection, this, &TciServer::onNewConnection);
    // A client that reaches the port but fails the WebSocket handshake never
    // becomes a connection; say so, or it just looks ignored.
    connect(m_server, &QWebSocketServer::serverError, this, [this](QWebSocketProtocol::CloseCode code) {
        emit event(tr("WebSocket handshake failed (code %1): %2").arg(int(code)).arg(m_server->errorString()));
    });
    connect(m_server, &QWebSocketServer::acceptError, this, [this](QAbstractSocket::SocketError) {
        emit event(tr("Couldn't accept a connection: %1").arg(m_server->errorString()));
    });

    // Any change in the radio: recompute and broadcast what differs.
    connect(m_radio, &Radio::stateChanged, this, &TciServer::refreshState);
    // Setters (frequency, power, ...) update Radio's cache without a signal,
    // and the radio doesn't report CAT-made changes back, so a command going
    // out is the cue. Coalesced: a burst of wheel tuning is one refresh.
    m_refreshSoon.setSingleShot(true);
    m_refreshSoon.setInterval(0);
    connect(&m_refreshSoon, &QTimer::timeout, this, &TciServer::refreshState);
    connect(m_radio->link(), &CatLink::frameSent, &m_refreshSoon, qOverload<>(&QTimer::start));
    connect(m_radio, &Radio::frequencyChanged, this, &TciServer::refreshState);
    connect(m_radio, &Radio::vfoSelectionChanged, this, &TciServer::refreshState);
    connect(m_radio, &Radio::modeChanged, this, &TciServer::refreshState);
    connect(m_radio, &Radio::ritXitChanged, this, &TciServer::refreshState);
    connect(m_radio, &Radio::txPowerChanged, this, &TciServer::refreshState);
    connect(m_radio, &Radio::tunePowerChanged, this, &TciServer::refreshState);
    connect(m_radio, &Radio::filterChanged, this, &TciServer::refreshState);
    connect(m_radio, &Radio::keyerChanged, this, &TciServer::refreshState);
    connect(m_radio, &Radio::transmitChanged, this, [this](bool transmitting) {
        if (!transmitting) {
            m_keyedBy = nullptr;
            m_tuning = false;
            stopTxStream();
        }
        refreshState();
    });
    connect(m_radio, &Radio::sMeterChanged, this, &TciServer::sendSensors);
    connect(m_radio, &Radio::txMetersChanged, this, &TciServer::sendSensors);
    m_sensorTimer.setInterval(kSensorsMinMs);
    connect(&m_sensorTimer, &QTimer::timeout, this, &TciServer::sendSensors);
    m_txTimer.setInterval(kTxTickMs);
    m_txTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_txTimer, &QTimer::timeout, this, &TciServer::requestTxAudio);
    m_clock.start();
}

void TciServer::setAudio(RadioAudio *audio)
{
    if (m_audio) {
        m_audio->disconnect(this);
        m_audio->stop();
    }
    m_audio = audio;
    if (m_audio) {
        connect(m_audio, &RadioAudio::captured, this, &TciServer::onCaptured);
        connect(m_audio, &RadioAudio::failed, this, [this](const QString &message) {
            m_audioFailed = true;
            emit audioError(message);
        });
    }
    updateAudio();
}

TciServer::~TciServer()
{
    close();
}

bool TciServer::listen(quint16 port, const QHostAddress &address)
{
    close();
    return m_server->listen(address, port);
}

void TciServer::close()
{
    releaseTransmit();
    stopTxStream();
    const auto sockets = m_clients.keys();
    m_clients.clear();
    for (QWebSocket *socket : sockets) {
        socket->disconnect(this);
        socket->close();
        socket->deleteLater();
    }
    m_sensorTimer.stop();
    if (m_server->isListening())
        m_server->close();
    updateAudio();
    if (!sockets.isEmpty())
        emit clientsChanged(0);
}

bool TciServer::isListening() const { return m_server->isListening(); }

quint16 TciServer::port() const { return m_server->serverPort(); }

QHostAddress TciServer::address() const { return m_server->serverAddress(); }

QString TciServer::errorString() const { return m_server->errorString(); }

void TciServer::onNewConnection()
{
    while (QWebSocket *socket = m_server->nextPendingConnection()) {
        // Bring the others up to date first, so m_sent is the whole state.
        refreshState();
        m_clients.insert(socket, Client{});
        emit event(tr("%1 connected").arg(peerName(socket)));
        setSampleRate(m_clients[socket], RadioAudio::kSampleRate);
        connect(socket, &QWebSocket::textMessageReceived, this,
                [this, socket](const QString &text) { onText(socket, text); });
        connect(socket, &QWebSocket::binaryMessageReceived, this,
                [this, socket](const QByteArray &data) { onBinary(socket, data); });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] { onDisconnected(socket); });

        // Initialization commands, the current state, then READY.
        const bool sg = m_radio->modelId() != kModelTs590S;
        sendTo(socket, command("PROTOCOL", {"590Ctrl", "1.9"}));
        sendTo(socket, command("DEVICE", {sg ? "TS-590SG" : "TS-590S"}));
        sendTo(socket, command("RECEIVE_ONLY", {"false"}));
        sendTo(socket, command("TRX_COUNT", {"1"}));
        sendTo(socket, command("CHANNELS_COUNT", {"2"}));
        sendTo(socket, command("VFO_LIMITS", {num(kMinFrequencyHz), num(kMaxFrequencyHz)}));
        sendTo(socket, command("IF_LIMITS", {num(-kIfLimitHz), num(kIfLimitHz)}));
        sendTo(socket, command("MODULATIONS_LIST", {"lsb", "usb", "digl", "digu", "cw", "cwr", "am", "nfm",
                                                    "fsk", "fskr"}));
        const Client &client = m_clients[socket];
        sendTo(socket, command("AUDIO_SAMPLERATE", {num(client.sampleRate)}));
        sendTo(socket, command("AUDIO_STREAM_SAMPLE_TYPE", {client.sampleType}));
        sendTo(socket, command("AUDIO_STREAM_CHANNELS", {num(client.channels)}));
        sendTo(socket, command("AUDIO_STREAM_SAMPLES", {num(client.samples)}));
        for (const QByteArray &message : std::as_const(m_sent))
            sendTo(socket, message);
        sendTo(socket, "ready;");
        emit clientsChanged(clientCount());
    }
}

void TciServer::onDisconnected(QWebSocket *socket)
{
    if (!m_clients.remove(socket))
        return;
    emit event(tr("%1 disconnected").arg(peerName(socket)));
    if (std::none_of(m_clients.cbegin(), m_clients.cend(), [](const Client &c) { return c.rxSensors || c.txSensors; }))
        m_sensorTimer.stop();
    // A client that dies mid-transmission mustn't leave the radio keyed.
    if (m_keyedBy == socket)
        releaseTransmit();
    if (m_txClient == socket)
        stopTxStream();
    updateAudio();
    socket->deleteLater();
    emit clientsChanged(clientCount());
}

void TciServer::releaseTransmit()
{
    if (!m_keyedBy)
        return;
    m_keyedBy = nullptr;
    m_tuning = false;
    m_radio->setTransmit(false);
}

void TciServer::onText(QWebSocket *socket, const QString &text)
{
    emit traffic(peerName(socket), true, text);
    const QByteArray bytes = text.toUtf8();
    for (const QByteArray &part : bytes.split(';')) {
        const QByteArray one = part.trimmed();
        if (one.isEmpty())
            continue;
        const qsizetype colon = one.indexOf(':');
        const QByteArray name = (colon < 0 ? one : one.left(colon)).trimmed().toLower();
        QList<QByteArray> args;
        if (colon >= 0) {
            for (const QByteArray &arg : one.mid(colon + 1).split(','))
                args.append(arg.trimmed());
        }
        auto it = m_clients.find(socket);
        if (it == m_clients.end())
            return;
        handleCommand(socket, *it, name, args);
    }
    // Some setters (frequency, power) update Radio's cache without a signal.
    refreshState();
}

void TciServer::handleCommand(QWebSocket *socket, Client &client, const QByteArray &name,
                              const QList<QByteArray> &args)
{
    const bool connected = m_radio->state() == Radio::State::Connected;
    // Replies a read with the broadcast state for `key`.
    auto replyState = [&](const QByteArray &key) {
        const QByteArray message = currentState().value(key);
        if (!message.isEmpty())
            sendTo(socket, message);
    };
    // Most commands start with the transceiver number; only 0 exists.
    const bool trx0 = !args.isEmpty() && args[0] == "0";
    const QByteArray upper = name.toUpper();

    if (name == "vfo" || name == "dds" || name == "if") {
        if (!trx0)
            return;
        const bool dds = name == "dds";
        const int channel = dds ? 0 : int(args.value(1).toInt());
        const qsizetype valueArg = dds ? 1 : 2;
        const QByteArray key = dds ? QByteArray("DDS:0") : upper + ":0," + num(channel);
        if (args.size() > valueArg && name != "if" && connected) {
            if (const auto hz = parseInt(args[valueArg]); hz && *hz > 0)
                m_radio->setFrequency(vfoForChannel(channel), *hz);
            return;
        }
        replyState(key);
    } else if (name == "modulation") {
        if (!trx0)
            return;
        if (args.size() >= 2 && connected) {
            if (const auto mode = tci::modulationFromName(args[1]))
                m_radio->setMode(mode->first, mode->second);
            return;   // the radio's answer is broadcast
        }
        replyState("MODULATION:0");
    } else if (name == "trx") {
        if (!trx0)
            return;
        const auto on = parseBool(args.value(1));
        if (on && connected) {
            if (*on) {
                // "tci" asks for the computer's audio: DATA SEND, which takes
                // the USB (or ACC2) input. Anything else is the mic. With no
                // source, a client streaming TCI audio gets TCI too: WSJT-X
                // only adds "tci" when talking to ExpertSDR3.
                const QByteArray source = args.value(2).toLower();
                if (source == "tci" || (source.isEmpty() && client.audio)) {
                    m_radio->setDataTransmit(true);
                    startTxStream(socket);
                } else {
                    m_radio->setTransmit(true);
                }
                m_keyedBy = socket;
                m_tuning = false;
            } else {
                m_radio->setTransmit(false);
                stopTxStream();
            }
            return;
        }
        replyState("TRX:0");
    } else if (name == "tune") {
        if (!trx0)
            return;
        const auto on = parseBool(args.value(1));
        if (on && connected) {
            m_radio->setTuneCarrier(*on);
            if (*on) {
                m_keyedBy = socket;
                m_tuning = true;
            }
            return;
        }
        replyState("TUNE:0");
    } else if (name == "drive" || name == "tune_drive") {
        if (!trx0)
            return;
        if (args.size() >= 2 && connected) {
            if (const auto level = parseInt(args[1])) {
                const int watts = std::clamp(int(*level), kTxPowerMin, kTxPowerMax);
                if (name == "drive")
                    m_radio->setTxPower(std::min(watts, txPowerMax(m_radio->mode())));
                else
                    m_radio->setTunePower(watts);
            }
            return;
        }
        replyState(upper + ":0");
    } else if (name == "split_enable" || name == "rit_enable" || name == "xit_enable") {
        if (!trx0)
            return;
        const auto on = parseBool(args.value(1));
        if (on && connected) {
            if (name == "split_enable")
                m_radio->setSplit(*on);
            else if (name == "rit_enable")
                m_radio->setRit(*on);
            else
                m_radio->setXit(*on);
            return;
        }
        replyState(upper + ":0");
    } else if (name == "rit_offset" || name == "xit_offset") {
        if (!trx0)
            return;
        if (args.size() >= 2 && connected) {
            // RIT and XIT share one offset on this radio.
            if (const auto hz = parseInt(args[1]))
                m_radio->setRitXitOffset(int(std::clamp<qint64>(*hz, -kRitXitMaxHz, kRitXitMaxHz)));
            return;
        }
        replyState(upper + ":0");
    } else if (name == "rx_filter_band") {
        if (!trx0)
            return;
        if (args.size() >= 3 && connected) {
            const auto low = parseInt(args[1]), high = parseInt(args[2]);
            if (low && high)
                setFilterBand(int(*low), int(*high));
            return;
        }
        replyState("RX_FILTER_BAND:0");
    } else if (name == "cw_macros_speed" || name == "cw_keyer_speed") {
        if (!args.isEmpty() && connected) {
            if (const auto wpm = parseInt(args[0]))
                m_radio->setKeyerSpeed(std::clamp(int(*wpm), kKeyerSpeedMin, kKeyerSpeedMax));
            return;
        }
        replyState(upper);
    } else if (name == "cw_macros") {
        // The text may contain commas; put it back together.
        if (trx0 && args.size() >= 2 && connected)
            m_radio->sendCw(QString::fromUtf8(args.mid(1).join(',')));
    } else if (name == "cw_macros_stop") {
        if (connected)
            m_radio->stopCw();
    } else if (name == "rx_sensors_enable" || name == "tx_sensors_enable") {
        const auto on = parseBool(args.value(0));
        if (!on)
            return;
        const int ms = std::clamp(int(args.value(1, "200").toInt()), kSensorsMinMs, kSensorsMaxMs);
        if (name == "rx_sensors_enable") {
            client.rxSensors = *on;
            client.rxSensorsMs = ms;
            client.rxSensorsSent.invalidate();
        } else {
            client.txSensors = *on;
            client.txSensorsMs = ms;
            client.txSensorsSent.invalidate();
        }
        const bool any = std::any_of(m_clients.cbegin(), m_clients.cend(),
                                     [](const Client &c) { return c.rxSensors || c.txSensors; });
        if (any != m_sensorTimer.isActive())
            any ? m_sensorTimer.start() : m_sensorTimer.stop();
    } else if (name == "tx_enable" || name == "start" || name == "stop") {
        // Owned by the radio; a client can't power it or inhibit TX.
        replyState(name == "tx_enable" ? QByteArray("TX_ENABLE:0") : QByteArray("START"));
    } else if (name == "audio_start" || name == "audio_stop") {
        if (trx0) {
            client.audio = name == "audio_start";
            client.rxPending.clear();
            updateAudio();
            // ExpertSDR3 confirms; WSJT-X gives up with "TCI Audio could
            // not be switched on" if no confirmation comes within 500 ms.
            sendTo(socket, command(name, {"0"}));
        }
    } else if (name == "line_out_start" || name == "line_out_stop") {
        if (trx0) {
            client.lineOut = name == "line_out_start";
            client.linePending.clear();
            updateAudio();
            sendTo(socket, command(name, {"0"}));
        }
    } else if (name == "tx_stream_audio_buffering") {
        if (const int ms = args.value(0).toInt(); ms > 0)
            client.txBufferingMs = std::clamp(ms, kTxBufferingMinMs, kTxBufferingMaxMs);
        sendTo(socket, command("TX_STREAM_AUDIO_BUFFERING", {num(client.txBufferingMs)}));
    } else if (name == "audio_samplerate") {
        if (const int rate = args.value(0).toInt(); rate == 8000 || rate == 12000 || rate == 24000 || rate == 48000)
            setSampleRate(client, rate);
        sendTo(socket, command("AUDIO_SAMPLERATE", {num(client.sampleRate)}));
    } else if (name == "audio_stream_sample_type") {
        if (const QByteArray type = args.value(0).toLower();
            type == "int16" || type == "int24" || type == "int32" || type == "float32")
            client.sampleType = type;
        sendTo(socket, command("AUDIO_STREAM_SAMPLE_TYPE", {client.sampleType}));
    } else if (name == "audio_stream_channels") {
        if (const int channels = args.value(0).toInt(); channels == 1 || channels == 2) {
            client.channels = channels;
            client.rxPending.clear();
        }
        sendTo(socket, command("AUDIO_STREAM_CHANNELS", {num(client.channels)}));
    } else if (name == "audio_stream_samples") {
        if (const int samples = args.value(0).toInt(); samples >= 100 && samples <= 2048)
            client.samples = samples;
        sendTo(socket, command("AUDIO_STREAM_SAMPLES", {num(client.samples)}));
    } else if (name == "modulations_list" || name == "device" || name == "protocol") {
        // Some clients ask again for these; the answers are in the greeting.
        if (name == "modulations_list")
            sendTo(socket, command("MODULATIONS_LIST", {"lsb", "usb", "digl", "digu", "cw", "cwr", "am", "nfm",
                                                        "fsk", "fskr"}));
        else if (name == "device")
            sendTo(socket, command("DEVICE", {m_radio->modelId() == kModelTs590S ? "TS-590S" : "TS-590SG"}));
        else
            sendTo(socket, command("PROTOCOL", {"590Ctrl", "1.9"}));
    }
    // Anything else is ignored, as the protocol says.
}

void TciServer::sendTo(QWebSocket *socket, const QByteArray &message)
{
    socket->sendTextMessage(QString::fromLatin1(message));
    emit traffic(peerName(socket), false, QString::fromLatin1(message));
}

void TciServer::broadcast(const QByteArray &key, const QByteArray &message)
{
    if (m_sent.value(key) == message)
        return;
    m_sent.insert(key, message);
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it)
        sendTo(it.key(), message);
}

void TciServer::refreshState()
{
    const auto state = currentState();
    for (auto it = state.cbegin(); it != state.cend(); ++it)
        broadcast(it.key(), it.value());
}

QMap<QByteArray, QByteArray> TciServer::currentState() const
{
    QMap<QByteArray, QByteArray> s;
    const bool connected = m_radio->state() == Radio::State::Connected;
    s["START"] = connected ? "start;" : "stop;";
    s["TX_ENABLE:0"] = command("TX_ENABLE", {"0", boolText(connected)});
    if (!connected)
        return s;

    const qint64 rx = m_radio->frequency(vfoForChannel(0));
    const qint64 other = m_radio->frequency(vfoForChannel(1));
    s["DDS:0"] = command("DDS", {"0", num(rx)});
    s["IF:0,0"] = command("IF", {"0", "0", "0"});
    s["IF:0,1"] = command("IF", {"0", "1", "0"});
    s["VFO:0,0"] = command("VFO", {"0", "0", num(rx)});
    s["VFO:0,1"] = command("VFO", {"0", "1", num(other)});
    qint64 tx = m_radio->frequency(m_radio->txVfo() == Vfo::Memory ? Vfo::A : m_radio->txVfo());
    if (m_radio->xit())
        tx += m_radio->ritXitOffset();
    s["TX_FREQUENCY"] = command("TX_FREQUENCY", {num(tx)});
    if (const QByteArray mode = tci::modulationName(m_radio->mode(), m_radio->dataMode()); !mode.isEmpty())
        s["MODULATION:0"] = command("MODULATION", {"0", mode});
    if (const auto band = filterBand())
        s["RX_FILTER_BAND:0"] = command("RX_FILTER_BAND", {"0", num(band->first), num(band->second)});
    const bool transmitting = m_radio->transmitting();
    s["TRX:0"] = command("TRX", {"0", boolText(transmitting && !m_tuning)});
    s["TUNE:0"] = command("TUNE", {"0", boolText(transmitting && m_tuning)});
    if (const auto watts = m_radio->txPower())
        s["DRIVE:0"] = command("DRIVE", {"0", num(*watts)});
    if (const auto watts = m_radio->tunePower())
        s["TUNE_DRIVE:0"] = command("TUNE_DRIVE", {"0", num(*watts)});
    s["SPLIT_ENABLE:0"] = command("SPLIT_ENABLE", {"0", boolText(m_radio->split())});
    s["RIT_ENABLE:0"] = command("RIT_ENABLE", {"0", boolText(m_radio->rit())});
    s["XIT_ENABLE:0"] = command("XIT_ENABLE", {"0", boolText(m_radio->xit())});
    s["RIT_OFFSET:0"] = command("RIT_OFFSET", {"0", num(m_radio->ritXitOffset())});
    s["XIT_OFFSET:0"] = command("XIT_OFFSET", {"0", num(m_radio->ritXitOffset())});
    if (const auto wpm = m_radio->keyerSpeed()) {
        s["CW_MACROS_SPEED"] = command("CW_MACROS_SPEED", {num(*wpm)});
        s["CW_KEYER_SPEED"] = command("CW_KEYER_SPEED", {num(*wpm)});
    }
    return s;
}

void TciServer::sendSensors()
{
    if (m_radio->state() != Radio::State::Connected)
        return;
    const bool transmitting = m_radio->transmitting();
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        Client &client = *it;
        if (!transmitting && client.rxSensors
            && (!client.rxSensorsSent.isValid() || client.rxSensorsSent.elapsed() >= client.rxSensorsMs)) {
            client.rxSensorsSent.start();
            const QByteArray dbm = QByteArray::number(tci::sMeterDbm(m_radio->sMeter()), 'f', 1);
            sendTo(it.key(), command("RX_SENSORS", {"0", dbm}));
            sendTo(it.key(), command("RX_CHANNEL_SENSORS", {"0", "0", dbm}));
        }
        if (transmitting && client.txSensors
            && (!client.txSensorsSent.isValid() || client.txSensorsSent.elapsed() >= client.txSensorsMs)) {
            client.txSensorsSent.start();
            // On transmit the S-meter reads power.
            const QByteArray watts = num(txPowerWatts(m_radio->sMeter()));
            const auto swr = swrForDots(m_radio->txMeter(TxMeter::Swr));
            sendTo(it.key(), command("TX_SENSORS", {"0", "0", watts, watts,
                                                    QByteArray::number(swr.value_or(3.0), 'f', 1)}));
        }
    }
}

// --------------------------------------------------------------------- audio

void TciServer::setSampleRate(Client &client, int rate)
{
    client.sampleRate = rate;
    const int factor = RadioAudio::kSampleRate / rate;
    client.rxResampler = Resampler(factor, Resampler::Direction::Down, RadioAudio::kChannels);
    client.txResampler = Resampler(factor, Resampler::Direction::Up, 1);
    client.rxPending.clear();
    client.linePending.clear();
}

// Runs the codec while anyone listens or transmits through it.
void TciServer::updateAudio()
{
    if (!m_audio)
        return;
    const bool wanted = m_txClient || std::any_of(m_clients.cbegin(), m_clients.cend(),
                                                  [](const Client &c) { return c.audio || c.lineOut; });
    if (wanted && !m_audio->isRunning()) {
        if (m_audio->start()) {
            m_audioFailed = false;
        } else if (!m_audioFailed) {
            m_audioFailed = true;
            emit audioError(m_audio->errorString());
        }
    } else if (!wanted && m_audio->isRunning()) {
        m_audio->stop();
    }
}

void TciServer::onCaptured(const QList<float> &frames)
{
    constexpr int stereo = RadioAudio::kChannels;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        Client &client = *it;
        if (!client.audio && !client.lineOut)
            continue;
        const QList<float> audio = client.rxResampler.process(frames.constData(), frames.size() / stereo);
        if (client.audio) {
            if (client.channels == 2) {
                client.rxPending.append(audio);
            } else {
                for (qsizetype i = 0; i + 1 < audio.size(); i += 2)
                    client.rxPending.append(0.5f * (audio[i] + audio[i + 1]));
            }
            const qsizetype block = qsizetype(client.samples) * client.channels;
            const int code = sampleTypeCode(client.sampleType);
            while (client.rxPending.size() >= block) {
                QByteArray packet = header(client.sampleRate, code, quint32(block), RxAudioStream, client.channels,
                                           client.rxCount++);
                appendSamples(packet, client.rxPending.constData(), block, code);
                client.rxPending.remove(0, block);
                it.key()->sendBinaryMessage(packet);
            }
        }
        if (client.lineOut) {
            // Line Out is fixed: float32, two channels.
            client.linePending.append(audio);
            const qsizetype block = qsizetype(client.samples) * stereo;
            while (client.linePending.size() >= block) {
                QByteArray packet = header(client.sampleRate, 3, quint32(block), LineOutStream, stereo,
                                           client.lineCount++);
                appendSamples(packet, client.linePending.constData(), block, 3);
                client.linePending.remove(0, block);
                it.key()->sendBinaryMessage(packet);
            }
        }
    }
}

void TciServer::startTxStream(QWebSocket *socket)
{
    auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;
    stopTxStream();
    m_txClient = socket;
    it->txResampler.reset();
    m_chronoCount = 0;
    m_requested.clear();
    m_silentUntilMs = m_clock.elapsed();
    updateAudio();
    if (m_audio)
        m_audio->clearPlayback();
    requestTxAudio();
    m_txTimer.start();
}

void TciServer::stopTxStream()
{
    if (!m_txClient)
        return;
    m_txClient = nullptr;
    m_txTimer.stop();
    m_requested.clear();
    m_chronoCount = 0;
    if (m_audio)
        m_audio->clearPlayback();
    updateAudio();
}

// Keeps the client's buffering worth of audio queued or asked for. What
// counts as played comes from the codec, so the client follows its clock;
// with no codec, real time stands in so the client still isn't stalled.
void TciServer::requestTxAudio()
{
    auto it = m_clients.find(m_txClient);
    if (it == m_clients.end())
        return;
    const Client &client = *it;
    const qint64 now = m_clock.elapsed();
    while (!m_requested.empty() && now - m_requested.front().second > kChronoAnswerMs)
        m_requested.pop_front();

    const qint64 perMs = RadioAudio::kSampleRate / 1000;
    const qint64 blockFrames = qint64(client.samples) * (RadioAudio::kSampleRate / client.sampleRate);
    const qint64 target = client.txBufferingMs * perMs + blockFrames;
    qint64 queued = m_audio && m_audio->isRunning() ? m_audio->queuedFrames()
                                                    : std::max<qint64>(m_silentUntilMs - now, 0) * perMs;
    for (const auto &request : m_requested)
        queued += request.first;
    while (queued < target) {
        const quint32 length = quint32(client.samples * client.channels);
        m_txClient->sendBinaryMessage(header(client.sampleRate, sampleTypeCode(client.sampleType), length, TxChrono,
                                             client.channels, m_chronoCount++));
        m_requested.emplace_back(blockFrames, now);
        queued += blockFrames;
    }
}

void TciServer::onBinary(QWebSocket *socket, const QByteArray &data)
{
    if (socket != m_txClient || data.size() < kHeaderBytes)
        return;
    const char *p = data.constData();
    const auto word = [p](int i) { return qFromLittleEndian<quint32>(p + i * 4); };
    if (word(6) != TxAudioStream)
        return;
    Client &client = m_clients[socket];
    const int code = word(2) <= 3 ? int(word(2)) : 3;
    const int channels = word(7) == 1 ? 1 : 2;
    const int bytes = sampleBytes(code);
    const qsizetype available = (data.size() - kHeaderBytes) / bytes;
    const qsizetype count = std::min<qsizetype>(word(5) ? qsizetype(word(5)) : available, available);
    const qsizetype frames = count / channels;
    if (!m_requested.empty())
        m_requested.pop_front();
    if (frames <= 0)
        return;

    QList<float> mono(frames);
    const char *s = p + kHeaderBytes;
    for (qsizetype f = 0; f < frames; ++f) {
        float sum = 0.f;
        for (int c = 0; c < channels; ++c, s += bytes)
            sum += sampleAt(s, code);
        mono[f] = sum / channels;
    }
    const QList<float> audio = client.txResampler.process(mono.constData(), frames);
    if (m_audio && m_audio->isRunning()) {
        m_audio->play(audio.constData(), audio.size());
    } else {
        const qint64 now = m_clock.elapsed();
        m_silentUntilMs = std::max(m_silentUntilMs, now) + audio.size() / (RadioAudio::kSampleRate / 1000);
    }
}

QString TciServer::peerName(const QWebSocket *socket)
{
    QHostAddress address = socket->peerAddress();
    // IPv4 clients of a dual-stack socket show up as ::ffff:a.b.c.d.
    bool ok = false;
    if (const quint32 v4 = address.toIPv4Address(&ok); ok)
        address = QHostAddress(v4);
    return QStringLiteral("%1:%2").arg(address.toString()).arg(socket->peerPort());
}

Vfo TciServer::vfoForChannel(int channel) const
{
    const Vfo rx = m_radio->rxVfo() == Vfo::B ? Vfo::B : Vfo::A;
    if (channel == 0)
        return rx;
    return rx == Vfo::A ? Vfo::B : Vfo::A;
}

// The passband relative to the carrier, negative below it (LSB). AM and FM
// report the symmetric band their high cut gives.
std::optional<std::pair<int, int>> TciServer::filterBand() const
{
    const FilterLayout layout = m_radio->filterLayout();
    auto hz = [&](const FilterControl &control) -> std::optional<int> {
        const auto raw = m_radio->filterValue(control.command);
        if (!raw || control.valuesHz.isEmpty())
            return std::nullopt;
        return control.valuesHz.value(filterIndexForRaw(control, *raw));
    };
    const Mode mode = m_radio->mode();
    switch (mode) {
    case Mode::LSB:
    case Mode::USB: {
        const auto first = hz(layout.first), second = hz(layout.second);
        if (!first || !second)
            return std::nullopt;
        int low = *second, high = *first;   // SL low cut, SH high cut
        if (layout.first.label == QLatin1String("Shift")) {
            // SH is the center (shift), SL the width.
            low = *first - *second / 2;
            high = *first + *second / 2;
        }
        if (mode == Mode::LSB)
            return std::pair{-high, -low};
        return std::pair{low, high};
    }
    case Mode::AM:
    case Mode::FM: {
        const auto high = hz(layout.first);
        if (!high)
            return std::nullopt;
        return std::pair{-*high, *high};
    }
    case Mode::CW:
    case Mode::CWR:
    case Mode::FSK:
    case Mode::FSKR: {
        const auto width = hz(layout.second);
        if (!width)
            return std::nullopt;
        return std::pair{-*width / 2, *width / 2};
    }
    case Mode::None: break;
    }
    return std::nullopt;
}

void TciServer::setFilterBand(int low, int high)
{
    const FilterLayout layout = m_radio->filterLayout();
    auto set = [&](const FilterControl &control, int hz) {
        if (control.command == FilterCommand::None || control.valuesHz.isEmpty())
            return;
        m_radio->setFilterValue(control.command, filterRawForIndex(control, nearestIndex(control.valuesHz, hz)));
    };
    // Below the carrier comes in negative; the radio wants audio offsets.
    const int a = std::min(std::abs(low), std::abs(high));
    const int b = std::max(std::abs(low), std::abs(high));
    switch (m_radio->mode()) {
    case Mode::LSB:
    case Mode::USB:
        if (layout.first.label == QLatin1String("Shift")) {
            set(layout.first, (a + b) / 2);
            set(layout.second, b - a);
        } else {
            set(layout.first, b);
            set(layout.second, a);
        }
        break;
    case Mode::AM:
    case Mode::FM:
        set(layout.first, b);
        break;
    case Mode::CW:
    case Mode::CWR:
    case Mode::FSK:
    case Mode::FSKR:
        set(layout.second, std::abs(high - low));
        break;
    case Mode::None: break;
    }
}

}   // namespace ts590

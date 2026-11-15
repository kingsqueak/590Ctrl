#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QList>
#include <QObject>
#include <QTimer>

#include <deque>

class QAudioSink;
class QAudioSource;
class QIODevice;

namespace ts590 {

// The radio's USB audio codec: receive audio in, transmit audio out, both
// at 48 kHz (the codec's native rate, so nothing is resampled on its side).
class RadioAudio : public QObject
{
    Q_OBJECT

public:
    static constexpr int kSampleRate = 48'000;
    static constexpr int kChannels = 2;

    explicit RadioAudio(QObject *parent = nullptr);
    ~RadioAudio() override;

    // Whether a device description is the radio's codec. macOS shows the bare
    // "USB Audio CODEC"; Windows wraps it ("Microphone (2- USB Audio CODEC)")
    // and Linux names the chip ("PCM2903B Audio CODEC Analog Stereo").
    static bool isCodecName(const QString &description);
    // Which of these device descriptions is the radio, or -1. A codec name
    // wins; failing that, a TS-590S on Windows is a plain "USB Audio Device"
    // ("Speakers (2- USB Audio Device)"), taken only if it's the only one.
    static int pickCodec(const QStringList &descriptions);
    // The device with this id, or with an empty id the radio's codec.
    // Null if there's no such device.
    static QAudioDevice findInput(const QByteArray &id = {});
    static QAudioDevice findOutput(const QByteArray &id = {});

    void setDevices(const QAudioDevice &input, const QAudioDevice &output);
    QAudioDevice input() const { return m_inputDevice; }
    QAudioDevice output() const { return m_outputDevice; }

    // The codec has several users (the TCI server, the DeepCW decoder): it
    // runs while any of them holds it. False if it couldn't start; see
    // errorString(). A failure while running drops every hold.
    bool hold(const QObject *user);
    void drop(const QObject *user);
    bool isHeldBy(const QObject *user) const { return m_users.contains(user); }
    bool isRunning() const { return m_source != nullptr; }
    QString errorString() const { return m_error; }

    // Transmit audio: mono at 48 kHz, played on both channels.
    void play(const float *samples, qsizetype count);
    void clearPlayback();
    // Frames not yet played: waiting here plus in the device's buffer.
    qsizetype queuedFrames() const;

signals:
    // Receive audio, interleaved stereo at 48 kHz.
    void captured(const QList<float> &frames);
    void failed(const QString &message);

private:
    bool start();
    void stop();
    void fail(const QString &message);
    void readInput();
    void pumpOutput();

    QAudioDevice m_inputDevice;
    QAudioDevice m_outputDevice;
    QAudioFormat m_inputFormat;
    QAudioFormat m_outputFormat;
    QAudioSource *m_source = nullptr;
    QAudioSink *m_sink = nullptr;
    QIODevice *m_in = nullptr;
    QIODevice *m_out = nullptr;
    std::deque<float> m_playQueue;   // mono
    QTimer m_pump;
    QString m_error;
    QList<const QObject *> m_users;
};

}   // namespace ts590

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

    // The device with this id, or with an empty id the radio's codec
    // ("USB Audio CODEC"). Null if there's no such device.
    static QAudioDevice findInput(const QByteArray &id = {});
    static QAudioDevice findOutput(const QByteArray &id = {});

    void setDevices(const QAudioDevice &input, const QAudioDevice &output);
    QAudioDevice input() const { return m_inputDevice; }
    QAudioDevice output() const { return m_outputDevice; }

    bool start();
    void stop();
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
};

}   // namespace ts590

#include "RadioAudio.h"

#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>

#include <algorithm>
#include <cstring>

namespace ts590 {

namespace {

const QString kCodecName = QStringLiteral("USB Audio CODEC");
constexpr int kPumpMs = 5;
// Device buffers: short for receive latency, a little longer on transmit
// so a busy UI thread doesn't starve the radio.
constexpr int kInputBufferMs = 20;
constexpr int kOutputBufferMs = 60;

QAudioDevice findDevice(const QList<QAudioDevice> &devices, const QByteArray &id)
{
    for (const QAudioDevice &device : devices) {
        if (id.isEmpty() ? device.description() == kCodecName : device.id() == id)
            return device;
    }
    return {};
}

// 48 kHz stereo 16-bit if the device takes it, as the 590's codec does;
// float otherwise.
QAudioFormat formatFor(const QAudioDevice &device)
{
    QAudioFormat format;
    format.setSampleRate(RadioAudio::kSampleRate);
    format.setChannelCount(RadioAudio::kChannels);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format))
        format.setSampleFormat(QAudioFormat::Float);
    return format;
}

int bufferBytes(const QAudioFormat &format, int ms)
{
    return format.bytesForDuration(qint64(ms) * 1000);
}

}   // namespace

RadioAudio::RadioAudio(QObject *parent)
    : QObject(parent)
{
    m_pump.setInterval(kPumpMs);
    m_pump.setTimerType(Qt::PreciseTimer);
    connect(&m_pump, &QTimer::timeout, this, &RadioAudio::pumpOutput);
}

RadioAudio::~RadioAudio()
{
    stop();
}

QAudioDevice RadioAudio::findInput(const QByteArray &id) { return findDevice(QMediaDevices::audioInputs(), id); }

QAudioDevice RadioAudio::findOutput(const QByteArray &id) { return findDevice(QMediaDevices::audioOutputs(), id); }

void RadioAudio::setDevices(const QAudioDevice &input, const QAudioDevice &output)
{
    const bool running = isRunning();
    stop();
    m_inputDevice = input;
    m_outputDevice = output;
    if (running)
        start();
}

bool RadioAudio::start()
{
    if (isRunning())
        return true;
    if (m_inputDevice.isNull() || m_outputDevice.isNull()) {
        m_error = tr("The radio's USB audio device (%1) wasn't found.").arg(kCodecName);
        return false;
    }
    m_inputFormat = formatFor(m_inputDevice);
    m_outputFormat = formatFor(m_outputDevice);

    m_source = new QAudioSource(m_inputDevice, m_inputFormat, this);
    m_source->setBufferSize(bufferBytes(m_inputFormat, kInputBufferMs));
    m_in = m_source->start();
    m_sink = new QAudioSink(m_outputDevice, m_outputFormat, this);
    m_sink->setBufferSize(bufferBytes(m_outputFormat, kOutputBufferMs));
    m_out = m_sink->start();
    if (!m_in || !m_out || m_source->error() != QtAudio::NoError || m_sink->error() != QtAudio::NoError) {
        m_error = tr("Couldn't open the radio's USB audio (%1 / %2).")
                      .arg(m_inputDevice.description(), m_outputDevice.description());
        stop();
        return false;
    }
    connect(m_in, &QIODevice::readyRead, this, &RadioAudio::readInput);
    // A device unplugged while running reports through the state change.
    connect(m_source, &QAudioSource::stateChanged, this, [this](QtAudio::State) {
        if (m_source && m_source->error() == QtAudio::FatalError) {
            m_error = tr("The radio's USB audio input stopped.");
            stop();
            emit failed(m_error);
        }
    });
    connect(m_sink, &QAudioSink::stateChanged, this, [this](QtAudio::State) {
        if (m_sink && m_sink->error() == QtAudio::FatalError) {
            m_error = tr("The radio's USB audio output stopped.");
            stop();
            emit failed(m_error);
        }
    });
    m_pump.start();
    m_error.clear();
    return true;
}

void RadioAudio::stop()
{
    m_pump.stop();
    m_playQueue.clear();
    if (m_source) {
        m_source->disconnect(this);
        m_source->stop();
        m_source->deleteLater();
    }
    if (m_sink) {
        m_sink->disconnect(this);
        m_sink->stop();
        m_sink->deleteLater();
    }
    m_source = nullptr;
    m_sink = nullptr;
    m_in = nullptr;
    m_out = nullptr;
}

void RadioAudio::readInput()
{
    if (!m_in)
        return;
    const QByteArray bytes = m_in->readAll();
    const int channels = m_inputFormat.channelCount();
    const int sampleBytes = m_inputFormat.bytesPerSample();
    const qsizetype frames = bytes.size() / (sampleBytes * channels);
    if (frames <= 0)
        return;
    QList<float> out(frames * kChannels);
    const char *p = bytes.constData();
    for (qsizetype f = 0; f < frames; ++f) {
        for (int c = 0; c < kChannels; ++c) {
            // A mono device feeds both channels.
            const char *s = p + (f * channels + std::min(c, channels - 1)) * sampleBytes;
            out[f * kChannels + c] = m_inputFormat.normalizedSampleValue(s);
        }
    }
    emit captured(out);
}

void RadioAudio::play(const float *samples, qsizetype count)
{
    if (!isRunning())
        return;
    m_playQueue.insert(m_playQueue.end(), samples, samples + count);
    pumpOutput();
}

void RadioAudio::clearPlayback()
{
    m_playQueue.clear();
}

qsizetype RadioAudio::queuedFrames() const
{
    qsizetype frames = qsizetype(m_playQueue.size());
    if (m_sink) {
        const qsizetype pending = m_sink->bufferSize() - m_sink->bytesFree();
        frames += std::max<qsizetype>(pending, 0) / m_outputFormat.bytesPerFrame();
    }
    return frames;
}

void RadioAudio::pumpOutput()
{
    if (!m_out || m_playQueue.empty())
        return;
    const int channels = m_outputFormat.channelCount();
    const int frameBytes = m_outputFormat.bytesPerFrame();
    const qsizetype frames = std::min<qsizetype>(m_sink->bytesFree() / frameBytes, qsizetype(m_playQueue.size()));
    if (frames <= 0)
        return;
    QByteArray bytes(frames * frameBytes, Qt::Uninitialized);
    char *p = bytes.data();
    const bool int16 = m_outputFormat.sampleFormat() == QAudioFormat::Int16;
    for (qsizetype f = 0; f < frames; ++f) {
        const float v = std::clamp(m_playQueue.front(), -1.f, 1.f);
        m_playQueue.pop_front();
        for (int c = 0; c < channels; ++c) {
            if (int16) {
                const qint16 s = qint16(std::lround(v * 32767.f));
                std::memcpy(p, &s, sizeof s);
                p += sizeof s;
            } else {
                std::memcpy(p, &v, sizeof v);
                p += sizeof v;
            }
        }
    }
    m_out->write(bytes);
}

}   // namespace ts590

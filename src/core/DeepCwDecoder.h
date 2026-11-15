#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace ts590 {

// Streaming CW decoder using the DeepCW neural network (e04, MIT; see
// resources/deepcw/README.md). A port of deepcw-engine's
// examples/python/decode_morse.py by way of SimpleHPSDR's DeepCwDecoder.cs:
// 48 kHz receive audio in, decoded text out about 2-4 s later. Every second
// it runs the model over up to 12 s of audio and stitches the overlapping
// windows together, committing text once it is 2 s behind the newest audio.
//
// The model runs in ONNX Runtime, loaded at run time from beside the app, so
// the app starts (without DeepCW) where there's no ONNX Runtime, such as on
// Intel Macs. Decoding runs on its own thread; the signals are sent from it,
// so connect them queued (the default for a receiver on another thread).
class DeepCwDecoder : public QObject
{
    Q_OBJECT

public:
    // False when the app was built without ONNX Runtime's headers: there's
    // no DeepCW to offer.
    static bool isBuilt();
    // The model's audio band: the CW pitch has to lie inside it.
    static constexpr int kMinPitchHz = 400;
    static constexpr int kMaxPitchHz = 1200;

    explicit DeepCwDecoder(QObject *parent = nullptr);
    ~DeepCwDecoder() override;

    // Loads the model and starts decoding. Call once, after connecting the
    // signals: failed() may be sent from inside it.
    void start();

    // Receive audio: mono at 48 kHz. From the thread that owns the decoder.
    void push(const float *samples, qsizetype count);

signals:
    void ready();                            // the model is loaded
    void decoded(const QString &text);       // committed text, leading space included
    void failed(const QString &message);     // it can't run, or stopped; nothing more follows

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}   // namespace ts590

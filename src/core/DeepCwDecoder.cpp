#include "DeepCwDecoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QStringList>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef HAVE_ONNXRUNTIME
// The library is loaded at run time: nothing links against it.
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#endif

namespace ts590 {

namespace {

// From the reference decoder.
constexpr double kNoiseLevelMinRelativeToPeak = 1e-3;
constexpr double kNoiseLevelMin = 1e-5;
constexpr double kMaxSegmentSeconds = 12.0;
constexpr double kMinPendingSeconds = 2.0;
constexpr double kStepSeconds = 1.0;
constexpr double kStitchLeftContextSeconds = 4.0;
constexpr double kStitchFreezeGuardSeconds = 2.0;
constexpr double kStitchLeftRampSeconds = 2.0;
constexpr double kStitchRightRampSeconds = 1.0;
constexpr int kStitchDilationFrames = 1;
constexpr int kWordBoundaryBlankHops = 134;
constexpr float kMinViewWeight = 1e-3f;
constexpr float kUncoveredLogProb = -30.0f;

constexpr int kInputRate = 48'000;
// More than this waiting means the worker can't keep up: drop the oldest.
constexpr int kMaxPendingSeconds = 30;

const QString kModelFile = QStringLiteral("model.onnx");
const QString kMetaFile = QStringLiteral("model.onnx.json");

// ---- model metadata ----

struct Meta
{
    QStringList chars;
    int blankIndex = 0;
    int sampleRate = 0;
    int fftLength = 0;
    int hopLength = 0;
    int outputTimeStride = 1;
    double minHz = 0;
    double maxHz = 0;
    int bins = 0;
    double noiseQuantile = 0.9;
    int numClasses = 0;
    QByteArray inputName;
    QByteArray outputName;

    // An empty string if it loaded, or what's wrong with it.
    QString load(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QCoreApplication::translate("DeepCwDecoder", "can't read %1").arg(path);
        const QJsonObject r = QJsonDocument::fromJson(file.readAll()).object();
        const QString norm = r.value(QLatin1String("normalization")).toString();
        if (!norm.startsWith(QLatin1String("noise_q")) || !norm.endsWith(QLatin1String("_log1p")))
            return QCoreApplication::translate("DeepCwDecoder", "unsupported normalization %1").arg(norm);
        const auto array = r.value(QLatin1String("chars")).toArray();
        for (const auto &c : array)
            chars.append(c.toString());
        blankIndex = r.value(QLatin1String("blank_index")).toInt(-1);
        sampleRate = r.value(QLatin1String("sample_rate")).toInt();
        fftLength = r.value(QLatin1String("fft_length")).toInt();
        hopLength = r.value(QLatin1String("hop_length")).toInt();
        outputTimeStride = r.value(QLatin1String("output_time_stride")).toInt(1);
        minHz = r.value(QLatin1String("spectrogram_min_freq_hz")).toDouble();
        maxHz = r.value(QLatin1String("spectrogram_max_freq_hz")).toDouble();
        bins = r.value(QLatin1String("spectrogram_frequency_bins")).toInt();
        noiseQuantile = r.value(QLatin1String("noise_level_quantile")).toDouble(0.9);
        numClasses = r.value(QLatin1String("num_classes")).toInt();
        inputName = r.value(QLatin1String("onnx_input_name")).toString(QStringLiteral("spectrogram")).toUtf8();
        outputName = r.value(QLatin1String("onnx_output_name")).toString(QStringLiteral("log_probs")).toUtf8();
        if (chars.isEmpty() || blankIndex < 0 || sampleRate <= 0 || fftLength <= 0 || hopLength <= 0
            || outputTimeStride <= 0 || bins <= 0 || numClasses <= blankIndex)
            return QCoreApplication::translate("DeepCwDecoder", "%1 is incomplete").arg(path);
        if (kInputRate % sampleRate != 0)
            return QCoreApplication::translate("DeepCwDecoder", "model rate %1 Hz doesn't divide 48 kHz")
                .arg(sampleRate);
        const double binHz = double(sampleRate) / fftLength;
        const int start = int(std::ceil(minHz / binHz));
        const int stop = int(std::floor(maxHz / binHz)) + 1;
        if (stop - start != bins)
            return QCoreApplication::translate("DeepCwDecoder", "model expects %1 bins, range gives %2")
                .arg(bins).arg(stop - start);
        return {};
    }
};

// ---- 48 kHz to model rate ----

// Windowed-sinc low-pass and keep every Nth sample. 48 kHz to 3.2 kHz: flat
// to about 1.2 kHz, the top of the model's band, and anything that could
// fold back below 1.2 kHz is well down. (The reference's linear resampler
// aliases noise into the band.)
class Decimator
{
public:
    explicit Decimator(int factor)
        : m_factor(factor)
    {
        constexpr int n = 481;
        const double cutoff = 1450.0 / kInputRate;   // cycles per sample
        m_taps.resize(n);
        double sum = 0;
        for (int i = 0; i < n; ++i) {
            const double m = i - (n - 1) / 2.0;
            const double sinc = m == 0 ? 2 * cutoff : std::sin(2 * std::numbers::pi * cutoff * m) / (std::numbers::pi * m);
            const double w = 0.42 - 0.5 * std::cos(2 * std::numbers::pi * i / (n - 1))
                             + 0.08 * std::cos(4 * std::numbers::pi * i / (n - 1));
            m_taps[i] = float(sinc * w);
            sum += m_taps[i];
        }
        for (float &tap : m_taps)
            tap /= float(sum);
        m_history.assign(n, 0.0f);
    }

    bool push(float x, float *y)
    {
        m_history[m_pos] = x;
        m_pos = (m_pos + 1) % m_history.size();
        if (++m_phase < m_factor)
            return false;
        m_phase = 0;
        double acc = 0;
        size_t p = m_pos;   // oldest sample
        for (const float tap : m_taps) {
            acc += tap * m_history[p];
            if (++p == m_history.size())
                p = 0;
        }
        *y = float(acc);
        return true;
    }

private:
    int m_factor;
    std::vector<float> m_taps;
    std::vector<float> m_history;
    size_t m_pos = 0;
    int m_phase = 0;
};

// ---- spectrogram ----

class Spectrogram
{
public:
    explicit Spectrogram(const Meta &m)
        : m_fft(m.fftLength), m_hop(m.hopLength), m_bins(m.bins), m_quantile(m.noiseQuantile)
    {
        const double binHz = double(m.sampleRate) / m_fft;
        const int start = int(std::ceil(m.minHz / binHz));
        m_cos.resize(size_t(m_bins) * m_fft);
        m_sin.resize(size_t(m_bins) * m_fft);
        for (int b = 0; b < m_bins; ++b) {
            for (int n = 0; n < m_fft; ++n) {
                const double w = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * n / m_fft);   // np.hanning(N+1)[:-1]
                const double a = -2 * std::numbers::pi * (start + b) * n / m_fft;
                m_cos[size_t(b) * m_fft + n] = float(w * std::cos(a));
                m_sin[size_t(b) * m_fft + n] = float(w * std::sin(a));
            }
        }
        m_rayleigh = std::sqrt(-2.0 * std::log(1.0 - m_quantile));
    }

    // Normalised log-magnitude spectrogram, [frames, bins] row-major.
    // startSample is the absolute model-rate index of audio[0].
    std::vector<float> compute(const std::vector<float> &audio, int64_t startSample, int *frameCount)
    {
        const int pad = m_fft / 2;
        const int n = int(audio.size());
        // numpy "reflect" padding: mirror without repeating the edge sample.
        std::vector<float> x(size_t(n) + 2 * pad);
        for (int i = 0; i < pad; ++i) {
            x[i] = audio[std::min(pad - i, n - 1)];
            x[size_t(pad) + n + i] = audio[std::max(n - 2 - i, 0)];
        }
        std::copy(audio.begin(), audio.end(), x.begin() + pad);
        const int frames = 1 + (int(x.size()) - m_fft) / m_hop;
        std::vector<float> spec(size_t(frames) * m_bins);
        // Magnitudes of frames lying wholly inside the audio are kept, by the
        // absolute sample their FFT starts at: windows overlap by up to 11 s,
        // so otherwise each frame would be transformed about 12 times. Only
        // the edge frames, which see the reflect padding, depend on the window.
        m_frameCache.erase(m_frameCache.begin(), m_frameCache.lower_bound(startSample - pad));
        for (int f = 0; f < frames; ++f) {
            const int at = f * m_hop - pad;   // frame start within audio
            const bool interior = at >= 0 && at + m_fft <= n;
            const int64_t key = startSample + at;
            float *row = spec.data() + size_t(f) * m_bins;
            if (interior) {
                if (const auto hit = m_frameCache.find(key); hit != m_frameCache.end()) {
                    std::copy(hit->second.begin(), hit->second.end(), row);
                    continue;
                }
            }
            const float *frame = x.data() + size_t(f) * m_hop;
            for (int b = 0; b < m_bins; ++b) {
                const float *c = m_cos.data() + size_t(b) * m_fft;
                const float *s = m_sin.data() + size_t(b) * m_fft;
                float re = 0, im = 0;
                for (int i = 0; i < m_fft; ++i) {
                    re += frame[i] * c[i];
                    im += frame[i] * s[i];
                }
                row[b] = std::sqrt(re * re + im * im);
            }
            if (interior)
                m_frameCache.emplace(key, std::vector<float>(row, row + m_bins));
        }
        std::vector<float> sorted = spec;
        std::sort(sorted.begin(), sorted.end());
        const double noise = std::max({sorted[size_t(std::floor(m_quantile * (sorted.size() - 1)))] / m_rayleigh,
                                       sorted.back() * kNoiseLevelMinRelativeToPeak, kNoiseLevelMin});
        const float inv = float(1.0 / noise);
        for (float &v : spec)
            v = std::log(1 + v * inv);
        *frameCount = frames;
        return spec;
    }

private:
    int m_fft, m_hop, m_bins;
    std::vector<float> m_cos, m_sin;   // [bin, n], Hann window folded in
    double m_quantile;
    double m_rayleigh = 1;
    std::map<int64_t, std::vector<float>> m_frameCache;
};

// ---- stitching overlapping windows and CTC decoding ----

struct Span
{
    QString ch;
    int64_t startFrame;
    int64_t endFrame;
};

struct LaneText
{
    QString text;
    bool prependSpace = false;
};

bool isBlankChar(const QString &ch)
{
    return ch.trimmed().isEmpty();
}

double smoothStep(double v)
{
    const double t = std::clamp(v, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void logSoftmax(const float *v, float *o, int n)
{
    float peak = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < n; ++i)
        peak = std::max(peak, v[i]);
    double sum = 0;
    for (int i = 0; i < n; ++i)
        sum += std::exp(v[i] - peak);
    const float lse = peak + float(std::log(sum));
    for (int i = 0; i < n; ++i)
        o[i] = v[i] - lse;
}

class Stitcher
{
public:
    explicit Stitcher(const Meta &m)
        : m_chars(m.chars), m_blank(m.blankIndex), m_space(int(m.chars.indexOf(QStringLiteral(" ")))),
          m_classes(m.numClasses), m_hopLength(m.hopLength * m.outputTimeStride)
    {
        m_framesPerSecond = double(m.sampleRate) / m_hopLength;
        m_maxSegmentSamples = int(kMaxSegmentSeconds * m.sampleRate);
        m_leftContextFrames = int(std::round(kStitchLeftContextSeconds * m.sampleRate / m_hopLength));
        m_freezeGuardFrames = int(std::round(kStitchFreezeGuardSeconds * m.sampleRate / m_hopLength));
        m_wordBoundaryFrames = int(std::ceil(double(kWordBoundaryBlankHops) / m.outputTimeStride));
    }

    int hopLength() const { return m_hopLength; }
    int64_t contextStartFrame() const { return std::max<int64_t>(0, m_frozenEnd - m_leftContextFrames); }

    // Audio before this frame was dropped: start afresh from there.
    void skipTo(int64_t frame)
    {
        if (frame <= m_committedEnd)
            return;
        m_rowBase = m_committedEnd = m_frozenEnd = m_coveredEnd = frame;
        m_rows = 0;
        m_previous = Previous::None;
        m_blankFrames = 0;
    }

    int64_t windowStartFrame(int64_t endSample) const
    {
        return std::max(contextStartFrame(),
                        int64_t(std::ceil((endSample - m_maxSegmentSamples) / double(m_hopLength))));
    }

    void addWindow(int64_t startFrame, const float *logits, int frameCount)
    {
        if (frameCount <= 0)
            return;
        const int64_t endFrame = startFrame + frameCount;
        ensureRows(endFrame);
        const std::vector<float> logProbs = dilatedLogProbs(logits, frameCount);
        for (int64_t f = std::max<int64_t>(0, m_frozenEnd - startFrame); f < frameCount; ++f) {
            const double left = startFrame == 0 ? std::numeric_limits<double>::infinity() : f / m_framesPerSecond;
            const double right = (frameCount - 1 - f) / m_framesPerSecond;
            const float w = float(std::max(double(kMinViewWeight), smoothStep(left / kStitchLeftRampSeconds)
                                                                       * smoothStep(right / kStitchRightRampSeconds)));
            const int64_t row = startFrame + f - m_rowBase;
            for (int c = 0; c < m_classes; ++c)
                m_sums[size_t(row) * m_classes + c] += w * logProbs[size_t(f) * m_classes + c];
            m_weights[size_t(row)] += w;
        }
        m_coveredEnd = std::max(m_coveredEnd, endFrame);
        m_frozenEnd = std::max(m_frozenEnd, endFrame - m_freezeGuardFrames);
    }

    // The text that is now final, if any.
    bool take(LaneText *committed)
    {
        const int64_t startFrame = m_committedEnd;
        if (m_coveredEnd - startFrame <= 0)
            return false;
        const bool startsWord = !m_hasCommittedText || m_previous == Previous::Space;
        const int frameCount = int(m_coveredEnd - startFrame);
        std::vector<int> argmax;
        std::vector<Span> spans = decodeCtcGreedy(readScores(startFrame, m_coveredEnd), frameCount, &argmax);
        for (Span &s : spans) {
            s.startFrame += startFrame;
            s.endFrame += startFrame;
        }

        size_t committedCount = 0;
        for (size_t i = 0; i < spans.size(); ++i) {
            if (spans[i].endFrame + 1 >= m_frozenEnd)
                break;
            if (!isBlankChar(spans[i].ch))
                committedCount = i + 1;
        }

        bool any = false;
        if (committedCount > 0) {
            *committed = toLaneText(spans, committedCount, startsWord);
            any = true;
            m_hasCommittedText = true;
            m_committedEnd = spans[committedCount - 1].endFrame + 1;
            m_previous = Previous::Char;
            m_blankFrames = 0;
        }

        // Release the silence up to the next character still pending.
        int64_t releaseEnd = m_frozenEnd;
        for (size_t i = committedCount; i < spans.size(); ++i) {
            if (!isBlankChar(spans[i].ch)) {
                releaseEnd = std::min(releaseEnd, spans[i].startFrame);
                break;
            }
        }
        releaseEnd -= 1;
        Previous previous = m_previous;
        int64_t blankFrames = m_blankFrames;
        for (int64_t frame = m_committedEnd; frame < releaseEnd; ++frame) {
            if (argmax[size_t(frame - startFrame)] == m_space) {
                previous = Previous::Space;
                blankFrames = 0;
            } else {
                ++blankFrames;
            }
        }
        if (releaseEnd > m_committedEnd) {
            m_previous = previous;
            m_blankFrames = blankFrames;
            m_committedEnd = releaseEnd;
        }
        dropRowsBefore(m_committedEnd);
        return any;
    }

private:
    enum class Previous { None, Char, Space };

    std::vector<Span> decodeCtcGreedy(const std::vector<float> &scores, int frameCount, std::vector<int> *argmaxOut) const
    {
        std::vector<int> argmax(size_t(frameCount), 0);
        for (int f = 0; f < frameCount; ++f) {
            int best = 0;
            float bestV = -std::numeric_limits<float>::infinity();
            for (int c = 0; c < m_classes; ++c) {
                const float v = scores[size_t(f) * m_classes + c];
                if (v > bestV) {
                    bestV = v;
                    best = c;
                }
            }
            argmax[size_t(f)] = best;
        }
        // A long enough silence between two characters is a word space, even
        // where the model didn't say so.
        std::vector<int> indices = argmax;
        if (m_space >= 0) {
            int previous = m_previous == Previous::Char ? -2 : m_previous == Previous::Space ? m_space : -1;
            int frame = 0;
            while (frame < frameCount) {
                if (indices[size_t(frame)] != m_blank) {
                    previous = indices[size_t(frame)];
                    ++frame;
                    continue;
                }
                const int runStart = frame;
                while (frame < frameCount && indices[size_t(frame)] == m_blank)
                    ++frame;
                const int following = frame < frameCount ? indices[size_t(frame)] : -1;
                const int64_t runLength = frame - runStart + (runStart == 0 ? m_blankFrames : 0);
                if (runLength >= m_wordBoundaryFrames && previous != -1 && previous != m_space && following >= 0
                    && following != m_space) {
                    for (int i = runStart; i < frame; ++i)
                        indices[size_t(i)] = m_space;
                }
            }
        }
        std::vector<Span> spans;
        int previousIndex = -1;
        bool active = false;
        for (int f = 0; f < frameCount; ++f) {
            const int index = indices[size_t(f)];
            if (index == m_blank) {
                previousIndex = -1;
                active = false;
                continue;
            }
            if (index == previousIndex) {
                if (active)
                    spans.back().endFrame = f;
                continue;
            }
            previousIndex = index;
            if (index >= m_chars.size()) {
                active = false;
                continue;
            }
            spans.push_back(Span{m_chars[index], f, f});
            active = true;
        }
        *argmaxOut = std::move(argmax);
        return spans;
    }

    static LaneText toLaneText(const std::vector<Span> &spans, size_t count, bool prependSpace)
    {
        LaneText lane;
        lane.prependSpace = prependSpace;
        bool pending = false;
        for (size_t i = 0; i < count; ++i) {
            if (isBlankChar(spans[i].ch)) {
                if (lane.text.isEmpty())
                    lane.prependSpace = true;
                else
                    pending = true;
                continue;
            }
            if (pending) {
                lane.text.append(QLatin1Char(' '));
                pending = false;
            }
            lane.text.append(spans[i].ch);
        }
        return lane;
    }

    std::vector<float> dilatedLogProbs(const float *logits, int frames) const
    {
        const size_t size = size_t(frames) * m_classes;
        std::vector<float> lp(size), dilated(size), out(size);
        for (int f = 0; f < frames; ++f)
            logSoftmax(logits + size_t(f) * m_classes, lp.data() + size_t(f) * m_classes, m_classes);
        for (int f = 0; f < frames; ++f) {
            for (int c = 0; c < m_classes; ++c) {
                float m = -std::numeric_limits<float>::infinity();
                if (c == m_blank) {
                    m = lp[size_t(f) * m_classes + c];
                } else {
                    for (int o = -kStitchDilationFrames; o <= kStitchDilationFrames; ++o) {
                        const int g = f + o;
                        if (g >= 0 && g < frames)
                            m = std::max(m, lp[size_t(g) * m_classes + c]);
                    }
                }
                dilated[size_t(f) * m_classes + c] = m;
            }
        }
        for (int f = 0; f < frames; ++f)
            logSoftmax(dilated.data() + size_t(f) * m_classes, out.data() + size_t(f) * m_classes, m_classes);
        return out;
    }

    void ensureRows(int64_t endFrame)
    {
        const int64_t need = endFrame - m_rowBase;
        if (need <= m_rows)
            return;
        m_weights.resize(size_t(need), 0.0f);
        m_sums.resize(size_t(need) * m_classes, 0.0f);
        m_rows = need;
    }

    void dropRowsBefore(int64_t frame)
    {
        const int64_t drop = std::min(frame - m_rowBase, m_rows);
        if (drop <= 0)
            return;
        m_weights.erase(m_weights.begin(), m_weights.begin() + drop);
        m_sums.erase(m_sums.begin(), m_sums.begin() + drop * m_classes);
        m_rows -= drop;
        m_rowBase += drop;
    }

    std::vector<float> readScores(int64_t startFrame, int64_t endFrame) const
    {
        const int n = int(endFrame - startFrame);
        std::vector<float> scores(size_t(n) * m_classes);
        for (int f = 0; f < n; ++f) {
            const int64_t row = startFrame - m_rowBase + f;
            const float w = row >= 0 && row < m_rows ? m_weights[size_t(row)] : 0;
            for (int c = 0; c < m_classes; ++c) {
                scores[size_t(f) * m_classes + c] = w > 0   ? m_sums[size_t(row) * m_classes + c] / w
                                                    : c == m_blank ? 0.0f
                                                                   : kUncoveredLogProb;
            }
        }
        return scores;
    }

    QStringList m_chars;
    int m_blank, m_space, m_classes, m_hopLength;
    double m_framesPerSecond = 0;
    int m_maxSegmentSamples = 0, m_leftContextFrames = 0, m_freezeGuardFrames = 0, m_wordBoundaryFrames = 0;

    int64_t m_rowBase = 0;
    std::vector<float> m_sums;      // [rows, classes]
    std::vector<float> m_weights;   // [rows]
    int64_t m_rows = 0;
    int64_t m_coveredEnd = 0, m_frozenEnd = 0, m_committedEnd = 0;
    Previous m_previous = Previous::None;
    int64_t m_blankFrames = 0;
    bool m_hasCommittedText = false;
};

// ---- finding the model and ONNX Runtime ----

#ifdef HAVE_ONNXRUNTIME

// Where the app's files may be: in a Mac bundle, beside the program
// (Windows), or under the install prefix (Linux). CTRL590_DEEPCW_DIR, a
// folder holding the model and the library, comes first (the tests use it).
QStringList searchDirs(const QStringList &relative)
{
    QStringList dirs;
    if (const QString dir = qEnvironmentVariable("CTRL590_DEEPCW_DIR"); !dir.isEmpty())
        dirs.append(dir);
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString &path : relative)
        dirs.append(QDir::cleanPath(app + QLatin1Char('/') + path));
    return dirs;
}

QString findModelDir()
{
    const QStringList dirs = searchDirs({QStringLiteral("../Resources/DeepCw"), QStringLiteral("DeepCw"),
                                         QStringLiteral("../share/590ctrl/DeepCw")});
    for (const QString &dir : dirs) {
        if (QFileInfo::exists(dir + QLatin1Char('/') + kModelFile) && QFileInfo::exists(dir + QLatin1Char('/') + kMetaFile))
            return dir;
    }
    return {};
}

QString findLibrary()
{
    const QStringList dirs = searchDirs({QStringLiteral("../Frameworks"), QStringLiteral("."), QStringLiteral("../lib"),
                                         QStringLiteral("../lib/590ctrl")});
    const QStringList names = {QStringLiteral("libonnxruntime*.dylib"), QStringLiteral("libonnxruntime.so*"),
                               QStringLiteral("onnxruntime.dll")};
    for (const QString &dir : dirs) {
        const QStringList found = QDir(dir).entryList(names, QDir::Files, QDir::Name);
        if (!found.isEmpty())
            return dir + QLatin1Char('/') + found.first();
    }
    return QStringLiteral("onnxruntime");   // one installed on the system
}

// Loads ONNX Runtime once for the process; it stays loaded. Null, with the
// reason, where it can't run.
Ort::Env *runtime(QString *error)
{
    static std::mutex mutex;
    static Ort::Env *env = nullptr;
    static QString failure;
    const std::lock_guard lock(mutex);
    if (env || !failure.isEmpty()) {
        *error = failure;
        return env;
    }
    const QString unavailable =
        QCoreApplication::translate("DeepCwDecoder", "DeepCW isn't available on this computer (no ONNX Runtime for it).");
    static QLibrary library;
    library.setFileName(findLibrary());
    using GetApiBase = const OrtApiBase *(*)();
    const auto getApiBase = library.load() ? reinterpret_cast<GetApiBase>(library.resolve("OrtGetApiBase")) : nullptr;
    const OrtApi *api = getApiBase ? getApiBase()->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) {
        failure = unavailable;
        *error = failure;
        return nullptr;
    }
    try {
        Ort::InitApi(api);
        env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "590Ctrl");
    } catch (const Ort::Exception &e) {
        failure = QCoreApplication::translate("DeepCwDecoder", "DeepCW couldn't load: %1").arg(QString::fromUtf8(e.what()));
    }
    *error = failure;
    return env;
}

#endif   // HAVE_ONNXRUNTIME

}   // namespace

struct DeepCwDecoder::Private
{
    DeepCwDecoder *q = nullptr;
    Meta meta;
    QString modelPath;
    std::unique_ptr<Decimator> decimator;
    std::unique_ptr<Spectrogram> spectrogram;
    std::unique_ptr<Stitcher> stitcher;

    std::mutex mutex;                    // guards the fields down to skipToFrame
    std::condition_variable wake;
    std::vector<float> audio;            // model-rate audio, absolute index audioBase + i
    int64_t audioBase = 0;
    int64_t nextEndSample = 0;
    int64_t skipToFrame = 0;             // audio was dropped: restart the stitcher here
    bool stop = false;

    std::atomic<bool> dead{true};        // not started, or stopped: drop audio rather than pile it up
    bool hasTranscript = false;
    std::thread worker;

#ifdef HAVE_ONNXRUNTIME
    std::unique_ptr<Ort::Session> session;

    bool load(QString *error)
    {
        Ort::Env *env = runtime(error);
        if (!env)
            return false;
        try {
            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(2);
            options.SetInterOpNumThreads(1);
#ifdef Q_OS_WIN
            const std::wstring path = QDir::toNativeSeparators(modelPath).toStdWString();
#else
            const std::string path = QFile::encodeName(modelPath).toStdString();
#endif
            session = std::make_unique<Ort::Session>(*env, path.c_str(), options);
        } catch (const Ort::Exception &e) {
            *error = QCoreApplication::translate("DeepCwDecoder", "DeepCW couldn't load: %1")
                         .arg(QString::fromUtf8(e.what()));
            return false;
        }
        return true;
    }

    QString runWindow(const std::vector<float> &window, int64_t startFrame)
    {
        if (int(window.size()) < meta.fftLength)
            return {};
        int frames = 0;
        std::vector<float> spec = spectrogram->compute(window, startFrame * stitcher->hopLength(), &frames);
        const std::array<int64_t, 4> shape = {1, 1, frames, meta.bins};
        const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const Ort::Value input =
            Ort::Value::CreateTensor<float>(memory, spec.data(), spec.size(), shape.data(), shape.size());
        const char *inputNames[] = {meta.inputName.constData()};
        const char *outputNames[] = {meta.outputName.constData()};
        const std::vector<Ort::Value> results =
            session->Run(Ort::RunOptions{nullptr}, inputNames, &input, 1, outputNames, 1);
        const std::vector<int64_t> dims = results.front().GetTensorTypeAndShapeInfo().GetShape();   // [1, t, c]
        if (dims.size() != 3 || dims[2] != meta.numClasses)
            throw std::runtime_error("unexpected model output");
        stitcher->addWindow(startFrame, results.front().GetTensorData<float>(), int(dims[1]));
        LaneText lane;
        if (!stitcher->take(&lane) || lane.text.isEmpty())
            return {};
        const QString text = (lane.prependSpace && hasTranscript ? QStringLiteral(" ") : QString()) + lane.text;
        hasTranscript = true;
        return text;
    }

    void run()
    {
        QString error;
        if (!load(&error)) {
            dead = true;
            emit q->failed(error);
            return;
        }
        emit q->ready();
        const int stepSamples = int(kStepSeconds * meta.sampleRate);
        try {
            for (;;) {
                std::vector<float> window;
                int64_t startFrame = 0;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [this] { return stop || audioBase + int64_t(audio.size()) >= nextEndSample; });
                    if (stop)
                        return;
                    if (skipToFrame > 0) {
                        stitcher->skipTo(skipToFrame);
                        skipToFrame = 0;
                    }
                    const int hop = stitcher->hopLength();
                    // Never before the oldest audio kept.
                    startFrame = std::max(stitcher->windowStartFrame(nextEndSample), (audioBase + hop - 1) / hop);
                    const int64_t from = startFrame * hop - audioBase;
                    const int64_t to = nextEndSample - audioBase;
                    if (to > from)
                        window.assign(audio.begin() + from, audio.begin() + to);
                }
                const QString text = runWindow(window, startFrame);
                {
                    const std::lock_guard lock(mutex);
                    nextEndSample += stepSamples;
                    // Keep only what a later window can still start from.
                    const int64_t keepFrom = stitcher->contextStartFrame() * stitcher->hopLength();
                    const int64_t drop = std::clamp<int64_t>(keepFrom - audioBase, 0, int64_t(audio.size()));
                    audio.erase(audio.begin(), audio.begin() + drop);
                    audioBase += drop;
                }
                if (!text.isEmpty())
                    emit q->decoded(text);
            }
        } catch (const std::exception &e) {
            dead = true;
            emit q->failed(QCoreApplication::translate("DeepCwDecoder", "DeepCW stopped: %1")
                               .arg(QString::fromUtf8(e.what())));
        }
    }
#endif   // HAVE_ONNXRUNTIME
};

bool DeepCwDecoder::isBuilt()
{
#ifdef HAVE_ONNXRUNTIME
    return true;
#else
    return false;
#endif
}

DeepCwDecoder::DeepCwDecoder(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->q = this;
}

DeepCwDecoder::~DeepCwDecoder()
{
    d->dead = true;
    {
        const std::lock_guard lock(d->mutex);
        d->stop = true;
    }
    d->wake.notify_all();
    if (d->worker.joinable())
        d->worker.join();
}

void DeepCwDecoder::start()
{
#ifdef HAVE_ONNXRUNTIME
    if (d->worker.joinable())
        return;
    const QString dir = findModelDir();
    if (dir.isEmpty()) {
        emit failed(tr("DeepCW's model files are missing from the app."));
        return;
    }
    if (const QString problem = d->meta.load(dir + QLatin1Char('/') + kMetaFile); !problem.isEmpty()) {
        emit failed(tr("DeepCW couldn't load: %1").arg(problem));
        return;
    }
    d->modelPath = dir + QLatin1Char('/') + kModelFile;
    d->decimator = std::make_unique<Decimator>(kInputRate / d->meta.sampleRate);
    d->spectrogram = std::make_unique<Spectrogram>(d->meta);
    d->stitcher = std::make_unique<Stitcher>(d->meta);
    d->nextEndSample = int64_t(kMinPendingSeconds * d->meta.sampleRate);
    d->dead = false;
    d->worker = std::thread([this] { d->run(); });
#else
    emit failed(tr("This build of 590Ctrl has no DeepCW."));
#endif
}

void DeepCwDecoder::push(const float *samples, qsizetype count)
{
    if (d->dead)
        return;
    {
        const std::lock_guard lock(d->mutex);
        for (qsizetype i = 0; i < count; ++i) {
            float y;
            if (d->decimator->push(samples[i], &y))
                d->audio.push_back(y);
        }
        const int rate = d->meta.sampleRate;
        const size_t max = size_t(kMaxSegmentSeconds + kMaxPendingSeconds) * rate;
        if (d->audio.size() > max) {
            // Behind by more than half a minute: skip ahead to the newest audio.
            const int64_t drop = int64_t(d->audio.size()) - int64_t(kMaxSegmentSeconds * rate);
            d->audio.erase(d->audio.begin(), d->audio.begin() + drop);
            d->audioBase += drop;
            d->nextEndSample = std::max(d->nextEndSample, d->audioBase + int64_t(kMinPendingSeconds * rate));
            const int hop = d->meta.hopLength * d->meta.outputTimeStride;
            d->skipToFrame = (d->audioBase + hop - 1) / hop;   // the worker applies it
        }
    }
    d->wake.notify_one();
}

}   // namespace ts590

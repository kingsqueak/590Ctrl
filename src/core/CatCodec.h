#pragma once

// Encoders and decoders for the TS-590S/SG PC control (CAT) command set.
// Everything here is pure: no I/O, no state. Frames passed to the parsers are
// the radio's answers with the trailing ';' already removed.
//
// Reference: "TS-590S/SG PC CONTROL COMMAND Reference Guide" B5A-0316-20/01.

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

#include <array>
#include <optional>

namespace ts590 {

// Model numbers returned by "ID;".
constexpr int kModelTs590S = 21;
constexpr int kModelTs590SG = 23;

// SG receive coverage; the radio rejects frequencies outside it.
constexpr qint64 kMinFrequencyHz = 30'000;
constexpr qint64 kMaxFrequencyHz = 60'000'000;

// Operating mode as used by MD, IF, etc. The numbering has gaps on purpose.
enum class Mode : int {
    None = 0,
    LSB = 1,
    USB = 2,
    CW = 3,
    FM = 4,
    AM = 5,
    FSK = 6,
    CWR = 7,
    FSKR = 9,
};

enum class Vfo : int { A = 0, B = 1, Memory = 2 };

enum class FilterSlot : int { A = 1, B = 2 };

// MF: the radio keeps two complete menu sets and EX reads and writes the
// one selected.
enum class MenuBank : int { A = 0, B = 1 };

QString modeName(Mode mode);
std::optional<Mode> modeFromChar(char c);
bool modeSupportsData(Mode mode);   // DATA is valid in LSB/USB/FM/AM only

// The S and SG number their EX menus differently. -1: menu doesn't exist.
struct MenuNumbers {
    int ssbFilterMode = -1;       // SSB HI/LO vs WIDTH/SHIFT
    int ssbDataFilterMode = -1;   // same for SSB-DATA
    int usbAudioIn = -1;          // USB audio input level, 0..9
    int usbAudioOut = -1;         // USB audio output level, 0..9
    int powerFine = -1;           // OFF: power in 5 W steps, ON: 1 W steps
    // Transmit filter menus, indexed by TxFilter.
    std::array<int, 4> txFilter = {-1, -1, -1, -1};
    int cwPitch = -1;             // CW pitch / sidetone, 0..14 (two-digit value)
};
MenuNumbers menuNumbersFor(int modelId);
constexpr int kUsbAudioLevelMax = 9;

// RIT/XIT offset range (RU/RD) and TX power limits (PC/TP).
constexpr int kRitXitMaxHz = 9999;
constexpr int kTxPowerMin = 5;
constexpr int kTxPowerMax = 100;
constexpr int kTxPowerMaxAm = 25;
int txPowerMax(Mode mode);

// Carrier level (CG) and mic gain (MG, SSB/AM; FM uses a menu): 0..100.
// TX monitor (ML): 0 = off, else the level, up to txMonitorMax.
constexpr int kCarrierLevelMax = 100;
constexpr int kMicGainMax = 100;
int txMonitorMax(int modelId);   // 20 on the SG, 9 on the S
constexpr int kVoxGainMax = 9;   // VG
constexpr int kVoxDelayMaxMs = 3000;   // VD, in steps of kVoxDelayStepMs
constexpr int kVoxDelayStepMs = 150;

// S-meter (SM): 0..30 display dots. Matches the front-panel scale: S1, S3,
// S5, S7, S9 every 3 dots (S9 = 15), then +20, +40, +60 dB every 5 dots.
constexpr int kSMeterMaxDots = 30;
constexpr int kSMeterS9Dots = 15;
constexpr int sMeterDotsForS(int sUnits) { return (sUnits + 1) * 3 / 2; }   // S1..S9, odd units exact
constexpr int sMeterDotsForDbOverS9(int db) { return kSMeterS9Dots + db / 4; }
QString sMeterText(int dots);   // "S0".."S9", "S9+4".."S9+60"

// Where an RF gain setting (0..255) sits on the S-meter scale, in dots.
// Assumed linear (255 = full gain = 0 dots, 0 = full scale); not calibrated
// against the radio's display yet.
double rfGainMeterDots(int level);

// Transmit meters (RM). Reading "RM;" answers all three at once, 0..30 dots each.
enum class TxMeter : int { Swr = 1, Comp = 2, Alc = 3 };

// Meter calibration read off a TS-590SG's display; linear between points.
// Power (SM while transmitting): 5 = 10 W, 10 = 25 W, 15 = 50 W, 30 = 100 W.
int txPowerWatts(int dots);
// SWR: 5 = 1.5, 10 = 2.0, 15 = 3.0. Uncalibrated above 15 dots: nullopt.
std::optional<double> swrForDots(int dots);
QString swrText(int dots);   // "1.0".."3.0", ">3"

// Notch filter (NT); the manual notch position (BP) has no documented Hz scale.
enum class NotchMode : int { Off = 0, Auto = 1, Manual = 2 };
constexpr int kNotchPositionMax = 127;
// The radio rejects NT/BP in FM (checked on a TS-590SG); CW etc. are fine.
bool notchSupported(Mode mode);

// Noise blanker (NB) and its level (NL, 1..10). The level applies to whichever
// single blanker is on; the radio rejects NL when NB is off or both are on.
enum class NoiseBlanker : int { Off = 0, NB1 = 1, NB2 = 2, Both = 3 };
constexpr int kNbLevelMin = 1;
constexpr int kNbLevelMax = 10;
bool noiseBlankerSupported(Mode mode);   // not in FM
bool nbLevelAdjustable(NoiseBlanker nb);
QString noiseBlankerName(NoiseBlanker nb);

// Noise reduction (NR) and its level (RL). NR1: strength 1..10.
// NR2: SPAC follow speed 0..9 = 2..20 ms. RL is rejected while NR is off.
enum class NoiseReduction : int { Off = 0, NR1 = 1, NR2 = 2 };
bool nr2Supported(Mode mode);            // not in FM
std::pair<int, int> nrLevelRange(NoiseReduction nr);
QString noiseReductionName(NoiseReduction nr);

// Bands (BD/BU): 0 = 1.8 MHz ... 9 = 50 MHz, 10 = GENE. Selecting the band
// you're already on steps through that band's memories.
constexpr int kBandCount = 11;
constexpr int kGeneralBand = 10;
QString bandLabel(int band);                     // "1.8", ..., "50", "GENE"
QString bandWavelength(int band);                // "160 m", ..., "" for GENE
// The amateur band containing hz, or kGeneralBand outside them all.
int bandForFrequency(qint64 hz);

// Speech processor (PR on/off) and its levels (PL: input and output, 0..100).
constexpr int kProcessorLevelMax = 100;

// AGC (GC) and its time constant (GT, 1..20). Slow and fast keep separate
// time constants; GT applies to whichever is on and is rejected while AGC is
// off. Neither works in FM.
enum class Agc : int { Off = 0, Slow = 1, Fast = 2 };
constexpr int kAgcTimeMin = 1;
constexpr int kAgcTimeMax = 20;
bool agcSupported(Mode mode);
QString agcName(Agc agc);

// Beat cancel (BC). A TS-590SG rejects BC, even BC0, in CW and FSK.
enum class BeatCancel : int { Off = 0, BC1 = 1, BC2 = 2 };
QString beatCancelName(BeatCancel bc);   // "BC", "BC1", "BC2"
bool beatCancelSupported(Mode mode);

// CW keyer speed (KS, WPM) and break-in delay (SD, ms in 50 ms steps;
// 0 = full break-in).
constexpr int kKeyerSpeedMin = 4;
constexpr int kKeyerSpeedMax = 60;
constexpr int kBreakInDelayMax = 1000;
constexpr int kBreakInDelayStep = 50;
// CW pitch / sidetone, an EX menu index: 0 = 300 Hz ... 14 = 1000 Hz.
constexpr int kCwPitchMinHz = 300;
constexpr int kCwPitchMaxHz = 1000;
constexpr int kCwPitchStepHz = 50;
constexpr int cwPitchHz(int index) { return kCwPitchMinHz + index * kCwPitchStepHz; }
constexpr int kCwPitchIndexMax = (kCwPitchMaxHz - kCwPitchMinHz) / kCwPitchStepHz;
bool isCwMode(Mode mode);   // CW or CW-R

// Morse decoder (CD0/CD1/CD2, TS-590SG only): threshold 1..30.
constexpr int kDecoderThresholdMin = 1;
constexpr int kDecoderThresholdMax = 30;

// Keyboard CW (KY): the radio takes 24 characters per command.
constexpr int kCwChunkLength = 24;
// Upper-cases the text and drops anything KY can't send (';' included).
QByteArray cwText(QStringView text);
// Splits cwText output into KY-sized pieces, breaking between words where it
// can. The radio doesn't key the padding spaces at the end of a command, so
// a word gap that falls on a boundary starts the next piece instead.
QList<QByteArray> splitCwText(const QByteArray &text);

// Transmit audio filters, set through EX menus as an index into a table.
enum class TxFilter : int { SsbLowCut = 0, SsbHighCut = 1, SsbDataLowCut = 2, SsbDataHighCut = 3 };
constexpr int kTxFilterCount = 4;
// Hz for each menu index (SSB entries also apply to AM).
const QList<int> &txFilterValuesHz(TxFilter filter);

// ---------------------------------------------------------------- Equalizer

constexpr int kEqBandCount = 18;   // 0 Hz .. 5100 Hz in 300 Hz steps
constexpr int kEqRawMin = 0;       // +6 dB
constexpr int kEqRawFlat = 6;      //  0 dB
constexpr int kEqRawMax = 30;      // -24 dB
constexpr int kEqDbMax = 6;
constexpr int kEqDbMin = -24;

// Raw band levels exactly as UR/UT carry them (0..30, lower = louder).
using EqLevels = std::array<int, kEqBandCount>;

enum class EqType : int { Tx = 0, Rx = 1 };

enum class EqCurve : int {
    Off = 0,
    HB1 = 1,
    HB2 = 2,
    FP = 3,
    BB1 = 4,
    BB2 = 5,
    Flat = 6,   // "C" (conventional) on TX, "FLAT" on RX
    User = 7,
};

// P2 of the EQ command: which mode family a curve selection belongs to.
enum class EqMode : int {
    Ssb = 0,
    SsbData = 1,
    Cw = 2,
    Fm = 3,
    FmData = 4,
    Am = 5,
    AmData = 6,
    Fsk = 7,
};

constexpr int eqBandFrequencyHz(int band) { return band * 300; }
constexpr int eqRawToDb(int raw) { return kEqRawFlat - raw; }
int eqDbToRaw(int db);   // clamps to the valid range
EqLevels flatEqLevels();
QString eqCurveName(EqType type, EqCurve curve);
EqMode eqModeFor(Mode mode, bool data);
QString eqModeName(EqMode mode);
// TX EQ in CW and FSK is fixed OFF; the radio rejects changes.
bool eqCurveSelectable(EqType type, EqMode mode);

// ------------------------------------------------------------------ Filters

// Which CAT command carries a given receive filter parameter.
enum class FilterCommand { None, SH, SL, FW, IS };

struct FilterControl {
    FilterCommand command = FilterCommand::None;
    QString label;
    QList<int> valuesHz;   // SH/SL: indexed by the raw value. FW/IS: allowed Hz values.
};

// The two filter knobs for the current mode: SH or IS on the first, SL or FW
// on the second. Their meaning depends on mode and on menus EX028/EX029.
struct FilterLayout {
    FilterControl first;
    FilterControl second;
};

// ssbWidthShift: EX028 (SSB) or EX029 (SSB-DATA) is set to WIDTH/SHIFT.
FilterLayout filterLayoutFor(Mode mode, bool data, bool ssbWidthShift);
// Converts between a control's raw CAT value and an index into valuesHz.
int filterIndexForRaw(const FilterControl &control, int raw);
int filterRawForIndex(const FilterControl &control, int index);

// ----------------------------------------------------------------- Encoders

namespace cmd {

QByteArray read(QByteArrayView name);            // e.g. "FA" -> "FA;"
QByteArray autoInfo(int level);                  // AI
QByteArray frequency(Vfo vfo, qint64 hz);        // FA / FB
QByteArray rxVfo(Vfo vfo);                       // FR: selects RX VFO, simplex
QByteArray txVfo(Vfo vfo);                       // FT: selects TX VFO, split if != RX
QByteArray copyVfo();                            // VV: A=B
QByteArray rit(bool on);                         // RT
QByteArray xit(bool on);                         // XT
// RC then RU/RD: sets the shared RIT/XIT offset to an absolute value. Only
// valid while RIT or XIT is on (RC is rejected otherwise).
QByteArray ritXitOffset(int hz);
QByteArray txPower(int watts);                   // PC
QByteArray tunePower(int watts);                 // TP (TS-590SG only)
QByteArray carrierLevel(int level);              // CG
QByteArray micGain(int level);                   // MG
QByteArray txMonitor(int level);                 // ML, 0 = off
QByteArray voxGain(int level);                   // VG
QByteArray voxDelay(int ms);                     // VD, rounded down to 150 ms steps
QByteArray lock(bool on);                        // LK
QByteArray mute(bool on);                        // MU: received audio only
QByteArray fineTuning(bool on);                  // FS
// AN sets one field at a time; the others are sent as 9 ("no change").
QByteArray antenna(int number);                  // ANT1 / ANT2
QByteArray rxAntenna(bool on);                   // RX ANT in use
QByteArray driveOut(bool on);                    // DRV out (antenna out on an SG set up for it)
QByteArray attenuator(bool on);                  // RA
QByteArray preamp(bool on);                      // PA
// AC: the RX-AT field can't be changed by command, so it's echoed back as
// read; P3 = 0 so switching the tuner in or out never starts a tune.
QByteArray tuner(bool rxIn, bool txIn);
// Starting a tune must be sent exactly as "AC111;" per the reference guide.
QByteArray startTuning();
QByteArray stopTuning(bool rxIn);
QByteArray notch(NotchMode mode, bool wide);     // NT; `wide` only counts in Manual
QByteArray noiseBlanker(NoiseBlanker nb);        // NB
QByteArray band(int band);                       // BD
QByteArray processor(bool on);                   // PR
QByteArray processorLevels(int input, int output);   // PL
QByteArray agc(Agc agc);                         // GC
// GC3: the only way to turn AGC back on from off (the radio rejects GC1/GC2
// then); it returns to whichever of slow/fast was active before.
QByteArray agcResume();
QByteArray agcTimeConstant(int value);           // GT
QByteArray beatCancel(BeatCancel bc);            // BC
QByteArray keyerSpeed(int wpm);                  // KS
QByteArray breakInDelay(int ms);                 // SD, rounded down to 50 ms
QByteArray cwTune(bool on);                      // CA
QByteArray sendCw(QByteArrayView chunk);         // KY, one splitCwText piece
QByteArray stopCw();                             // KY0: abandons what's buffered
QByteArray morseDecoder(bool on);                // CD0
QByteArray decoderThreshold(int level);          // CD1
QByteArray nbLevel(int level);                   // NL
QByteArray noiseReduction(NoiseReduction nr);    // NR
QByteArray nrLevel(NoiseReduction nr, int level);   // RL, clamped for nr
QByteArray notchPosition(int position);          // BP, 0..127
QByteArray mode(Mode mode);                      // MD
QByteArray dataMode(bool on);                    // DA
QByteArray afGain(int level);                    // AG (0..255)
QByteArray squelch(int level);                   // SQ (0..255)
QByteArray rfGain(int level);                    // RG (0..255)
QByteArray filterSlot(FilterSlot slot);          // FL
QByteArray filterValue(FilterCommand command, int raw);  // SH / SL / FW / IS
QByteArray readFilterValue(FilterCommand command);
QByteArray readMenu(int number);                 // EX
QByteArray setMenu(int number, QByteArrayView value);
QByteArray menuBank(MenuBank bank);              // MF
QByteArray readEqCurve(EqType type, EqMode mode);
QByteArray eqCurve(EqType type, EqMode mode, EqCurve curve);
QByteArray readEqLevels(EqType type);            // UR / UT
QByteArray eqLevels(EqType type, const EqLevels &levels);

}   // namespace cmd

// ------------------------------------------------------------------ Parsers

// Decimal digits only (no sign, no spaces); nullopt otherwise.
std::optional<qint64> parseNumber(QByteArrayView digits);

// Decoded "IF" answer.
struct StatusInfo {
    qint64 frequencyHz = 0;
    int ritXitOffsetHz = 0;
    bool rit = false;
    bool xit = false;
    int memoryChannel = 0;
    bool transmitting = false;
    Mode mode = Mode::None;
    Vfo function = Vfo::A;
    bool scanning = false;
    bool split = false;
    int toneMode = 0;
    int toneNumber = 0;
};
std::optional<StatusInfo> parseStatus(QByteArrayView frame);

struct EqCurveAnswer {
    EqType type;
    EqMode mode;
    EqCurve curve;
};
std::optional<EqCurveAnswer> parseEqCurve(QByteArrayView frame);

// Accepts "UR..." or "UT..." frames; the type is taken from the frame.
std::optional<std::pair<EqType, EqLevels>> parseEqLevels(QByteArrayView frame);

struct MenuAnswer {
    int number;
    QByteArray value;
};
std::optional<MenuAnswer> parseMenu(QByteArrayView frame);
std::optional<MenuBank> parseMenuBank(QByteArrayView frame);   // "MF0" / "MF1"

}   // namespace ts590

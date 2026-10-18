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
};
MenuNumbers menuNumbersFor(int modelId);
constexpr int kUsbAudioLevelMax = 9;

// RIT/XIT offset range (RU/RD) and TX power limits (PC/TP).
constexpr int kRitXitMaxHz = 9999;
constexpr int kTxPowerMin = 5;
constexpr int kTxPowerMax = 100;
constexpr int kTxPowerMaxAm = 25;
int txPowerMax(Mode mode);

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

}   // namespace ts590

#include "CatCodec.h"

#include <algorithm>

namespace ts590 {

namespace {

QByteArray digits(qint64 value, int width)
{
    return QByteArray::number(value).rightJustified(width, '0');
}

int clampLevel(int level) { return std::clamp(level, 0, 255); }

QByteArray eqLevelsCommandName(EqType type) { return type == EqType::Rx ? "UR" : "UT"; }

const QList<int> kSsbHighCutHz = {1000, 1200, 1400, 1600, 1800, 2000, 2200,
                                  2400, 2600, 2800, 3000, 3400, 4000, 5000};
const QList<int> kSsbLowCutHz = {0, 50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000};
const QList<int> kAmHighCutHz = {2500, 3000, 4000, 5000};
const QList<int> kAmLowCutHz = {0, 100, 200, 300};
const QList<int> kSsbWidthHz = {50, 80, 100, 150, 200, 250, 300,
                                400, 500, 600, 1000, 1500, 2000, 2500};
// TS-590SG table; the TS-590S lacks the 1750 Hz entry.
const QList<int> kSsbShiftHz = {1000, 1100, 1200, 1300, 1400, 1500, 1600,
                                1700, 1750, 1800, 1900, 2000, 2100, 2210};
const QList<int> kCwWidthHz = {50, 80, 100, 150, 200, 250, 300,
                               400, 500, 600, 1000, 1500, 2000, 2500};
const QList<int> kFskWidthHz = {250, 500, 1000, 1500};

QList<int> cwShiftHz()
{
    QList<int> values;
    for (int hz = 300; hz <= 1000; hz += 50)
        values.append(hz);
    return values;
}

}   // namespace

QString modeName(Mode mode)
{
    switch (mode) {
    case Mode::LSB: return QStringLiteral("LSB");
    case Mode::USB: return QStringLiteral("USB");
    case Mode::CW: return QStringLiteral("CW");
    case Mode::FM: return QStringLiteral("FM");
    case Mode::AM: return QStringLiteral("AM");
    case Mode::FSK: return QStringLiteral("FSK");
    case Mode::CWR: return QStringLiteral("CW-R");
    case Mode::FSKR: return QStringLiteral("FSK-R");
    case Mode::None: break;
    }
    return QStringLiteral("—");
}

std::optional<Mode> modeFromChar(char c)
{
    switch (c) {
    case '1': return Mode::LSB;
    case '2': return Mode::USB;
    case '3': return Mode::CW;
    case '4': return Mode::FM;
    case '5': return Mode::AM;
    case '6': return Mode::FSK;
    case '7': return Mode::CWR;
    case '9': return Mode::FSKR;
    default: return std::nullopt;
    }
}

bool modeSupportsData(Mode mode)
{
    return mode == Mode::LSB || mode == Mode::USB || mode == Mode::FM || mode == Mode::AM;
}

MenuNumbers menuNumbersFor(int modelId)
{
    if (modelId == kModelTs590S) {
        // The S has no HI/LO vs WIDTH/SHIFT menus (SSB is always HI/LO).
        return {-1, -1, 64, 65};
    }
    return {28, 29, 71, 72};
}

// ---------------------------------------------------------------- Equalizer

int eqDbToRaw(int db)
{
    return std::clamp(kEqRawFlat - db, kEqRawMin, kEqRawMax);
}

EqLevels flatEqLevels()
{
    EqLevels levels;
    levels.fill(kEqRawFlat);
    return levels;
}

QString eqCurveName(EqType type, EqCurve curve)
{
    switch (curve) {
    case EqCurve::Off: return QStringLiteral("OFF");
    case EqCurve::HB1: return QStringLiteral("HB1 (High Boost 1)");
    case EqCurve::HB2: return QStringLiteral("HB2 (High Boost 2)");
    case EqCurve::FP: return QStringLiteral("FP (Formant Pass)");
    case EqCurve::BB1: return QStringLiteral("BB1 (Bass Boost 1)");
    case EqCurve::BB2: return QStringLiteral("BB2 (Bass Boost 2)");
    case EqCurve::Flat:
        return type == EqType::Tx ? QStringLiteral("C (Conventional)") : QStringLiteral("FLAT");
    case EqCurve::User: return QStringLiteral("U (User)");
    }
    return {};
}

EqMode eqModeFor(Mode mode, bool data)
{
    switch (mode) {
    case Mode::CW:
    case Mode::CWR: return EqMode::Cw;
    case Mode::FSK:
    case Mode::FSKR: return EqMode::Fsk;
    case Mode::FM: return data ? EqMode::FmData : EqMode::Fm;
    case Mode::AM: return data ? EqMode::AmData : EqMode::Am;
    case Mode::LSB:
    case Mode::USB:
    case Mode::None: break;
    }
    return data ? EqMode::SsbData : EqMode::Ssb;
}

QString eqModeName(EqMode mode)
{
    switch (mode) {
    case EqMode::Ssb: return QStringLiteral("SSB");
    case EqMode::SsbData: return QStringLiteral("SSB-DATA");
    case EqMode::Cw: return QStringLiteral("CW/CW-R");
    case EqMode::Fm: return QStringLiteral("FM");
    case EqMode::FmData: return QStringLiteral("FM-DATA");
    case EqMode::Am: return QStringLiteral("AM");
    case EqMode::AmData: return QStringLiteral("AM-DATA");
    case EqMode::Fsk: return QStringLiteral("FSK/FSK-R");
    }
    return {};
}

bool eqCurveSelectable(EqType type, EqMode mode)
{
    return type == EqType::Rx || (mode != EqMode::Cw && mode != EqMode::Fsk);
}

// ------------------------------------------------------------------ Filters

FilterLayout filterLayoutFor(Mode mode, bool data, bool ssbWidthShift)
{
    switch (mode) {
    case Mode::CW:
    case Mode::CWR:
        return {{FilterCommand::IS, QStringLiteral("Shift"), cwShiftHz()},
                {FilterCommand::FW, QStringLiteral("Width"), kCwWidthHz}};
    case Mode::FSK:
    case Mode::FSKR:
        return {{}, {FilterCommand::FW, QStringLiteral("Width"), kFskWidthHz}};
    case Mode::AM:
        return {{FilterCommand::SH, QStringLiteral("High cut"), kAmHighCutHz},
                {FilterCommand::SL, QStringLiteral("Low cut"), kAmLowCutHz}};
    case Mode::FM:
        return {{FilterCommand::SH, QStringLiteral("High cut"), kSsbHighCutHz},
                {FilterCommand::SL, QStringLiteral("Low cut"), kSsbLowCutHz}};
    case Mode::LSB:
    case Mode::USB:
        if (ssbWidthShift) {
            return {{FilterCommand::SH, QStringLiteral("Shift"), kSsbShiftHz},
                    {FilterCommand::SL, QStringLiteral("Width"), kSsbWidthHz}};
        }
        return {{FilterCommand::SH, QStringLiteral("High cut"), kSsbHighCutHz},
                {FilterCommand::SL, QStringLiteral("Low cut"), kSsbLowCutHz}};
    case Mode::None: break;
    }
    Q_UNUSED(data);
    return {};
}

int filterIndexForRaw(const FilterControl &control, int raw)
{
    if (control.valuesHz.isEmpty())
        return -1;
    if (control.command == FilterCommand::SH || control.command == FilterCommand::SL)
        return std::clamp(raw, 0, int(control.valuesHz.size()) - 1);
    // FW/IS carry Hz; the radio rounds unlisted values down, so do the same.
    int index = 0;
    for (int i = 0; i < control.valuesHz.size(); ++i) {
        if (control.valuesHz[i] <= raw)
            index = i;
    }
    return index;
}

int filterRawForIndex(const FilterControl &control, int index)
{
    if (control.command == FilterCommand::SH || control.command == FilterCommand::SL)
        return index;
    return control.valuesHz.value(index);
}

// ----------------------------------------------------------------- Encoders

namespace cmd {

QByteArray read(QByteArrayView name) { return name.toByteArray() + ';'; }

QByteArray autoInfo(int level) { return "AI" + QByteArray::number(level) + ';'; }

QByteArray frequency(Vfo vfo, qint64 hz)
{
    hz = std::clamp(hz, kMinFrequencyHz, kMaxFrequencyHz);
    return (vfo == Vfo::B ? "FB" : "FA") + digits(hz, 11) + ';';
}

QByteArray rxVfo(Vfo vfo) { return "FR" + QByteArray::number(int(vfo)) + ';'; }

QByteArray txVfo(Vfo vfo) { return "FT" + QByteArray::number(int(vfo)) + ';'; }

QByteArray copyVfo() { return "VV;"; }

QByteArray mode(Mode mode) { return "MD" + QByteArray::number(int(mode)) + ';'; }

QByteArray dataMode(bool on) { return on ? "DA1;" : "DA0;"; }

QByteArray afGain(int level) { return "AG0" + digits(clampLevel(level), 3) + ';'; }

QByteArray squelch(int level) { return "SQ0" + digits(clampLevel(level), 3) + ';'; }

QByteArray rfGain(int level) { return "RG" + digits(clampLevel(level), 3) + ';'; }

QByteArray filterSlot(FilterSlot slot) { return "FL" + QByteArray::number(int(slot)) + ';'; }

QByteArray filterValue(FilterCommand command, int raw)
{
    switch (command) {
    case FilterCommand::SH: return "SH" + digits(raw, 2) + ';';
    case FilterCommand::SL: return "SL" + digits(raw, 2) + ';';
    case FilterCommand::FW: return "FW" + digits(raw, 4) + ';';
    case FilterCommand::IS: return "IS " + digits(raw, 4) + ';';   // P1 is always a space
    case FilterCommand::None: break;
    }
    return {};
}

QByteArray readFilterValue(FilterCommand command)
{
    switch (command) {
    case FilterCommand::SH: return "SH;";
    case FilterCommand::SL: return "SL;";
    case FilterCommand::FW: return "FW;";
    case FilterCommand::IS: return "IS;";
    case FilterCommand::None: break;
    }
    return {};
}

// EX P1(3) P2(2, "00") P3(1, "0") P4(1, "0") [P5]
QByteArray readMenu(int number) { return "EX" + digits(number, 3) + "0000;"; }

QByteArray setMenu(int number, QByteArrayView value)
{
    return "EX" + digits(number, 3) + "0000" + value.toByteArray() + ';';
}

QByteArray readEqCurve(EqType type, EqMode mode)
{
    return "EQ" + QByteArray::number(int(type)) + QByteArray::number(int(mode)) + ';';
}

QByteArray eqCurve(EqType type, EqMode mode, EqCurve curve)
{
    return "EQ" + QByteArray::number(int(type)) + QByteArray::number(int(mode))
           + QByteArray::number(int(curve)) + ';';
}

QByteArray readEqLevels(EqType type) { return eqLevelsCommandName(type) + ';'; }

QByteArray eqLevels(EqType type, const EqLevels &levels)
{
    QByteArray out = eqLevelsCommandName(type);
    for (int raw : levels)
        out += digits(std::clamp(raw, kEqRawMin, kEqRawMax), 2);
    return out + ';';
}

}   // namespace cmd

// ------------------------------------------------------------------ Parsers

std::optional<qint64> parseNumber(QByteArrayView text)
{
    if (text.isEmpty() || text.size() > 18)
        return std::nullopt;
    qint64 value = 0;
    for (char c : text) {
        if (c < '0' || c > '9')
            return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

// IF answer, 0-based offsets within the frame (37 chars without ';'):
//  2..12 frequency   13..17 spaces   18..22 RIT/XIT offset (sign may be ' ')
//  23 RIT  24 XIT  25..27 memory ch  28 TX  29 mode  30 function  31 scan
//  32 split  33 tone mode  34..35 tone number  36 always 0
std::optional<StatusInfo> parseStatus(QByteArrayView frame)
{
    if (frame.size() != 37 || !frame.startsWith("IF"))
        return std::nullopt;
    const auto freq = parseNumber(frame.sliced(2, 11));
    const auto offset = parseNumber(frame.sliced(19, 4));
    const auto channel = parseNumber(frame.sliced(25, 3));
    const auto tone = parseNumber(frame.sliced(34, 2));
    if (!freq || !offset || !channel || !tone)
        return std::nullopt;

    StatusInfo info;
    info.frequencyHz = *freq;
    info.ritXitOffsetHz = int(frame[18] == '-' ? -*offset : *offset);
    info.rit = frame[23] == '1';
    info.xit = frame[24] == '1';
    info.memoryChannel = int(*channel);
    info.transmitting = frame[28] == '1';
    info.mode = modeFromChar(frame[29]).value_or(Mode::None);
    info.function = frame[30] == '1' ? Vfo::B : frame[30] == '2' ? Vfo::Memory : Vfo::A;
    info.scanning = frame[31] != '0';
    info.split = frame[32] == '1';
    info.toneMode = frame[33] - '0';
    info.toneNumber = int(*tone);
    return info;
}

std::optional<EqCurveAnswer> parseEqCurve(QByteArrayView frame)
{
    if (frame.size() != 5 || !frame.startsWith("EQ"))
        return std::nullopt;
    const int type = frame[2] - '0';
    const int mode = frame[3] - '0';
    const int curve = frame[4] - '0';
    if (type < 0 || type > 1 || mode < 0 || mode > 7 || curve < 0 || curve > 7)
        return std::nullopt;
    return EqCurveAnswer{EqType(type), EqMode(mode), EqCurve(curve)};
}

std::optional<std::pair<EqType, EqLevels>> parseEqLevels(QByteArrayView frame)
{
    if (frame.size() != 2 + 2 * kEqBandCount)
        return std::nullopt;
    EqType type;
    if (frame.startsWith("UR"))
        type = EqType::Rx;
    else if (frame.startsWith("UT"))
        type = EqType::Tx;
    else
        return std::nullopt;

    EqLevels levels;
    for (int band = 0; band < kEqBandCount; ++band) {
        const auto raw = parseNumber(frame.sliced(2 + 2 * band, 2));
        if (!raw || *raw > kEqRawMax)
            return std::nullopt;
        levels[band] = int(*raw);
    }
    return std::pair{type, levels};
}

std::optional<MenuAnswer> parseMenu(QByteArrayView frame)
{
    // "EX" + 3-digit menu + "0000" + value
    if (frame.size() < 9 || !frame.startsWith("EX"))
        return std::nullopt;
    const auto number = parseNumber(frame.sliced(2, 3));
    if (!number)
        return std::nullopt;
    return MenuAnswer{int(*number), frame.sliced(9).toByteArray()};
}

}   // namespace ts590

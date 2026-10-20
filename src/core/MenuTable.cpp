#include "MenuTable.h"

#include "CatCodec.h"

#include <algorithm>

namespace ts590 {

namespace {

QStringList numbers(int first, int last, int step = 1)
{
    QStringList out;
    for (int v = first; v <= last; v += step)
        out << QString::number(v);
    return out;
}

QStringList offThen(const QStringList &rest) { return QStringList{QStringLiteral("OFF")} + rest; }

const QStringList kOffOn = {QStringLiteral("OFF"), QStringLiteral("ON")};
const QStringList kLevels = numbers(0, 9);
const QStringList kPortSpeeds = {"4800", "9600", "19200", "38400", "57600", "115200"};
const QStringList kLowCut = {"10", "100", "200", "300", "400", "500"};
const QStringList kHighCut = numbers(2500, 3000, 100);
const QStringList kAmFmSteps = {"5", "6.25", "10", "12.5", "15", "20", "25", "30", "50", "100"};
const QStringList kTxEq = {"OFF", "HB1", "HB2", "FP", "BB1", "BB2", "C", "U"};
const QStringList kRxEq = {"OFF", "HB1", "HB2", "FP", "BB1", "BB2", "FLAT", "U"};
const QStringList kPsqOutput = {"OFF", "BSY", "SQL", "SND", "BSY-SND", "SQL-SND"};

QStringList keyingWeights()
{
    QStringList out = {QStringLiteral("AUTO")};
    for (int tenths = 25; tenths <= 40; ++tenths)
        out << QString::number(tenths / 10.0, 'f', 1);
    return out;
}

MenuItem choice(int number, const char *name, const QStringList &choices)
{
    return {number, QString::fromUtf8(name), MenuItem::Kind::Choice, choices};
}

MenuItem locked(int number, const char *name, const QStringList &choices)
{
    MenuItem item = choice(number, name, choices);
    item.locked = true;
    return item;
}

MenuItem text(int number, const char *name, int maxLength)
{
    MenuItem item{number, QString::fromUtf8(name), MenuItem::Kind::Text};
    item.maxLength = maxLength;
    return item;
}

MenuItem function(int number, const char *name)
{
    return {number, QString::fromUtf8(name), MenuItem::Kind::Function};
}

QList<MenuItem> ts590sItems()
{
    return {
        choice(0, "Display brightness", offThen(numbers(1, 6))),
        choice(1, "Back light color", numbers(1, 2)),
        choice(2, "Panel key response for double function", numbers(1, 3)),
        choice(3, "Beep volume", offThen(numbers(1, 9))),
        choice(4, "Sidetone volume", offThen(numbers(1, 9))),
        choice(5, "Message playback volume", offThen(numbers(1, 9))),
        choice(6, "Voice guide volume", offThen(numbers(1, 7))),
        choice(7, "Voice guide speed", numbers(0, 4)),
        choice(8, "Voice guide language", {"EN", "JP"}),
        choice(9, "Auto announcement", kOffOn),
        choice(10, "MHz step (MHz)", {"0.1", "0.5", "1"}),
        choice(11, "Tuning control adjustment rate (Hz)", {"250", "500", "1000"}),
        choice(12, "MULTI/CH control rounding off", kOffOn),
        choice(13, "Dedicated step inside the AM broadcast band", kOffOn),
        choice(14, "MULTI/CH step for SSB/CW/FSK (kHz)", {"0.5", "1", "2.5", "5", "10"}),
        choice(15, "MULTI/CH step for AM (kHz)", kAmFmSteps),
        choice(16, "MULTI/CH step for FM (kHz)", kAmFmSteps),
        choice(17, "Maximum Quick Memory channels", {"3", "5", "10"}),
        choice(18, "Temporary change of standard memory frequency", kOffOn),
        choice(19, "Program Scan slow down", kOffOn),
        choice(20, "Program Scan slow down range (Hz)", numbers(100, 500, 100)),
        choice(21, "Program Scan hold", kOffOn),
        choice(22, "Scan resume method", {"TO", "CO"}),
        choice(23, "Auto mode change", kOffOn),
        choice(24, "AUTO NOTCH tracking speed", numbers(0, 4)),
        choice(25, "SSB/AM TX filter low cut (Hz)", kLowCut),
        choice(26, "SSB/AM TX filter high cut (Hz)", kHighCut),
        choice(27, "SSB-DATA TX filter low cut (Hz)", kLowCut),
        choice(28, "SSB-DATA TX filter high cut (Hz)", kHighCut),
        choice(29, "Speech processor effect", {"SOFT", "HARD"}),
        choice(30, "Transmit equalizer", kTxEq),
        choice(31, "Receive equalizer", kRxEq),
        choice(32, "Electronic keyer mode", {"A", "B"}),
        choice(33, "Insert keying", kOffOn),
        choice(34, "Sidetone / pitch frequency (Hz)", numbers(300, 1000, 50)),
        choice(35, "CW rise time (ms)", {"1", "2", "4", "6"}),
        choice(36, "Keying weight ratio", keyingWeights()),
        choice(37, "Reverse keying auto weight ratio", kOffOn),
        choice(38, "Bug key function", kOffOn),
        choice(39, "Swap paddle dot and dash", kOffOn),
        choice(40, "Mic paddle function", {"PF", "PA"}),
        choice(41, "Auto CW TX in SSB mode", kOffOn),
        choice(42, "Frequency correction for SSB to CW change", kOffOn),
        choice(43, "No break-in while setting keying speed", kOffOn),
        choice(44, "FSK shift (Hz)", {"170", "200", "425", "850"}),
        choice(45, "FSK keying polarity reverse", kOffOn),
        choice(46, "FSK tone frequency (Hz)", {"1275", "2125"}),
        choice(47, "Mic gain for FM", numbers(1, 3)),
        choice(48, "Power fine", kOffOn),
        choice(49, "Time-out timer (min)", offThen({"3", "5", "10", "20", "30"})),
        choice(50, "Transverter function and power down", offThen(numbers(1, 2))),
        choice(51, "TX hold when AT completes tuning", kOffOn),
        choice(52, "AT operation when receiving", kOffOn),
        choice(53, "HF linear amplifier control", offThen(numbers(1, 3))),
        choice(54, "50 MHz linear amplifier control", offThen(numbers(1, 3))),
        choice(55, "Constant recording", kOffOn),
        choice(56, "Voice/message playback repeat", kOffOn),
        choice(57, "Playback repeat interval (s)", numbers(0, 60)),
        choice(58, "Split transfer function", kOffOn),
        choice(59, "Write split transfer data to the VFO", kOffOn),
        choice(60, "Transmit inhibit", kOffOn),
        locked(61, "COM port speed (bps)", kPortSpeeds),
        locked(62, "USB port speed (bps)", kPortSpeeds),
        choice(63, "DATA modulation line", {"ACC2", "USB"}),
        choice(64, "USB audio input level", kLevels),
        choice(65, "USB audio output level", kLevels),
        choice(66, "ACC2 AF input level", kLevels),
        choice(67, "ACC2 AF output level", kLevels),
        choice(68, "Beep mixed into external AF output", kOffOn),
        choice(69, "DATA VOX", kOffOn),
        choice(70, "DATA VOX delay", numbers(0, 100, 5)),
        choice(71, "DATA VOX gain for USB audio input", kLevels),
        choice(72, "DATA VOX gain for ACC2 input", kLevels),
        choice(73, "PKS polarity reverse", kOffOn),
        choice(74, "Busy transmit inhibit", kOffOn),
        choice(75, "CTCSS mute operation", numbers(1, 2)),
        choice(76, "PSQ control signal logic", {"LO", "OPEN"}),
        choice(77, "PSQ output condition", kPsqOutput),
        choice(78, "Auto power off (min)", offThen({"60", "120", "180"})),
        function(79, "Panel PF A function"),
        function(80, "Panel PF B function"),
        function(81, "Mic PF 1 function"),
        function(82, "Mic PF 2 function"),
        function(83, "Mic PF 3 function"),
        function(84, "Mic PF 4 function"),
        function(85, "Mic PF (DWN) function"),
        function(86, "Mic PF (UP) function"),
        text(87, "Power-on message", 8),
    };
}

QList<MenuItem> ts590sgItems()
{
    MenuItem firmware{0, QStringLiteral("Firmware version"), MenuItem::Kind::ReadOnly};
    // The reference guide lists 0.5 twice for menus 016/017; the radio has it
    // once (fw 1.07 answers 2 for 1 kHz).
    const QStringList ssbCwSteps = offThen({"0.5", "1", "2.5", "5", "10"});
    const QStringList filterControl = {"1 (HI/LO)", "2 (WIDTH/SHIFT)"};
    return {
        firmware,
        text(1, "Power-on message", 8),
        choice(2, "Display brightness", offThen(numbers(1, 6))),
        choice(3, "Back light color", numbers(1, 10)),
        choice(4, "Panel key response for double function", numbers(1, 3)),
        choice(5, "Beep volume", offThen(numbers(1, 20))),
        choice(6, "Sidetone volume", offThen(numbers(1, 20))),
        choice(7, "Message playback volume", offThen(numbers(1, 20))),
        choice(8, "Voice guide volume", offThen(numbers(1, 20))),
        choice(9, "Voice guide speed", numbers(0, 4)),
        choice(10, "Voice guide language", {"EN", "JP"}),
        choice(11, "Auto announcement", offThen(numbers(1, 2))),
        choice(12, "MHz step (MHz)", {"0.1", "0.5", "1"}),
        choice(13, "Tuning control adjustment rate (Hz)", {"250", "500", "1000"}),
        choice(14, "MULTI/CH control rounding off", kOffOn),
        choice(15, "Dedicated step inside the AM broadcast band", kOffOn),
        choice(16, "MULTI/CH step for SSB (kHz)", ssbCwSteps),
        choice(17, "MULTI/CH step for CW/FSK (kHz)", ssbCwSteps),
        choice(18, "MULTI/CH step for AM (kHz)", offThen(kAmFmSteps)),
        choice(19, "MULTI/CH step for FM (kHz)", offThen(kAmFmSteps)),
        choice(20, "Shift RX frequency during split transmit", kOffOn),
        choice(21, "Maximum Quick Memory channels", {"3", "5", "10"}),
        choice(22, "Temporary change of standard/extension memory frequency", kOffOn),
        choice(23, "Program Scan slow down", kOffOn),
        choice(24, "Program Scan slow down range (Hz)", numbers(100, 500, 100)),
        choice(25, "Program Scan hold", kOffOn),
        choice(26, "Scan resume method", {"TO", "CO"}),
        choice(27, "Auto mode change", kOffOn),
        choice(28, "SSB filter control", filterControl),
        choice(29, "SSB-DATA filter control", filterControl),
        choice(30, "AUTO NOTCH tracking speed", numbers(0, 4)),
        choice(31, "SSB/AM TX filter low cut (Hz)", kLowCut),
        choice(32, "SSB/AM TX filter high cut (Hz)", kHighCut),
        choice(33, "SSB-DATA TX filter low cut (Hz)", kLowCut),
        choice(34, "SSB-DATA TX filter high cut (Hz)", kHighCut),
        choice(35, "Speech processor effect", {"SOFT", "HARD"}),
        choice(36, "Transmit equalizer", kTxEq),
        choice(37, "Receive equalizer", kRxEq),
        choice(38, "Electronic keyer mode", {"A", "B"}),
        choice(39, "Insert keying", kOffOn),
        choice(40, "Sidetone / pitch frequency (Hz)", numbers(300, 1000, 50)),
        choice(41, "CW rise time (ms)", {"1", "2", "4", "6"}),
        choice(42, "Keying weight ratio", keyingWeights()),
        choice(43, "Reverse keying auto weight ratio", kOffOn),
        choice(44, "Bug key function", kOffOn),
        choice(45, "Swap paddle dot and dash", kOffOn),
        choice(46, "Mic paddle function", {"PF", "PA"}),
        choice(47, "Auto CW TX in SSB mode", kOffOn),
        choice(48, "Frequency correction for SSB to CW change", kOffOn),
        choice(49, "No break-in while setting keying speed", kOffOn),
        choice(50, "FSK shift (Hz)", {"170", "200", "425", "850"}),
        choice(51, "FSK keying polarity reverse", kOffOn),
        choice(52, "FSK tone frequency (Hz)", {"1275", "2125"}),
        choice(53, "Mic gain for FM", numbers(1, 3)),
        choice(54, "Power fine", kOffOn),
        choice(55, "Time-out timer (min)", offThen({"3", "5", "10", "20", "30"})),
        choice(56, "Transverter function and power down", offThen(numbers(1, 2))),
        choice(57, "TX hold when AT completes tuning", kOffOn),
        choice(58, "AT operation when receiving", kOffOn),
        choice(59, "HF linear amplifier control", offThen(numbers(1, 5))),
        choice(60, "50 MHz linear amplifier control", offThen(numbers(1, 5))),
        choice(61, "Constant recording", kOffOn),
        choice(62, "Voice/message playback repeat", kOffOn),
        choice(63, "Playback repeat interval (s)", numbers(0, 60)),
        choice(64, "Split transfer function", {"OFF", "A-TR", "A-SUBR", "B"}),
        choice(65, "Write split transfer data to the VFO", kOffOn),
        choice(66, "Transmit inhibit", kOffOn),
        locked(67, "COM port speed (bps)", kPortSpeeds),
        locked(68, "USB port speed (bps)", kPortSpeeds),
        choice(69, "DATA modulation line", {"ACC2", "USB"}),
        choice(70, "Audio source of SEND/PTT for data modes", {"FRONT", "REAR"}),
        choice(71, "USB audio input level", kLevels),
        choice(72, "USB audio output level", kLevels),
        choice(73, "ACC2 AF input level", kLevels),
        choice(74, "ACC2 AF output level", kLevels),
        choice(75, "Beep mixed into external AF output", kOffOn),
        choice(76, "DATA VOX", kOffOn),
        choice(77, "DATA VOX delay", numbers(0, 100, 5)),
        choice(78, "DATA VOX gain for USB audio input", kLevels),
        choice(79, "DATA VOX gain for ACC2 input", kLevels),
        choice(80, "PKS polarity reverse", kOffOn),
        choice(81, "Busy transmit inhibit", kOffOn),
        choice(82, "CTCSS mute operation", numbers(1, 2)),
        choice(83, "PSQ control signal logic", {"LO", "OPEN"}),
        choice(84, "PSQ output condition", kPsqOutput),
        choice(85, "DRV connector output", {"DRO", "ANT"}),
        choice(86, "Auto power off (min)", offThen({"60", "120", "180"})),
        function(87, "Panel PF A function"),
        function(88, "Panel PF B function"),
        function(89, "RIT key function"),
        function(90, "XIT key function"),
        function(91, "CL key function"),
        function(92, "MULTI/CH key function (except CW)"),
        function(93, "MULTI/CH key function (CW)"),
        function(94, "Mic PF 1 function"),
        function(95, "Mic PF 2 function"),
        function(96, "Mic PF 3 function"),
        function(97, "Mic PF 4 function"),
        function(98, "Mic PF (DWN) function"),
        function(99, "Mic PF (UP) function"),
    };
}

}   // namespace

const QList<MenuItem> &menuItemsFor(int modelId)
{
    static const QList<MenuItem> s = ts590sItems();
    static const QList<MenuItem> sg = ts590sgItems();
    return modelId == kModelTs590S ? s : sg;
}

const MenuItem *findMenuItem(int modelId, int number)
{
    const QList<MenuItem> &items = menuItemsFor(modelId);
    const auto it = std::find_if(items.cbegin(), items.cend(), [number](const MenuItem &i) { return i.number == number; });
    return it == items.cend() ? nullptr : &*it;
}

int menuValueIndex(const MenuItem &item, const QByteArray &raw)
{
    if (item.kind != MenuItem::Kind::Choice && item.kind != MenuItem::Kind::Function)
        return -1;
    const auto value = parseNumber(raw);
    if (!value)
        return -1;
    const qint64 maximum = item.kind == MenuItem::Kind::Choice ? item.choices.size() - 1 : kMenuFunctionOff;
    return *value <= maximum ? int(*value) : -1;
}

QString menuValueText(const MenuItem &item, const QByteArray &raw)
{
    const int index = menuValueIndex(item, raw);
    if (item.kind == MenuItem::Kind::Choice && index >= 0)
        return item.choices[index];
    if (item.kind == MenuItem::Kind::Function && index >= 0)
        return index == kMenuFunctionOff ? QStringLiteral("OFF") : QString::number(index);
    return QString::fromLatin1(raw).trimmed();
}

QByteArray menuIndexValue(int index, int width)
{
    return QByteArray::number(index).rightJustified(width, '0');
}

int defaultMenuValueWidth(const MenuItem &item)
{
    if (item.kind == MenuItem::Kind::Function)
        return 3;
    return item.choices.size() > 10 ? 2 : 1;
}

}   // namespace ts590

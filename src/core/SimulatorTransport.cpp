#include "SimulatorTransport.h"

#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>

namespace ts590 {

namespace {

QByteArray digits(qint64 value, int width)
{
    return QByteArray::number(value).rightJustified(width, '0');
}

std::optional<int> number(QByteArrayView text)
{
    const auto value = parseNumber(text);
    return value ? std::optional<int>(int(*value)) : std::nullopt;
}

}   // namespace

SimulatorTransport::SimulatorTransport(QObject *parent)
    : CatTransport(parent)
{
    m_eqLevels[int(EqType::Rx)] = flatEqLevels();
    m_eqLevels[int(EqType::Tx)] = flatEqLevels();
    m_eqCurves[int(EqType::Rx)][int(EqMode::SsbData)] = EqCurve::User;
    m_eqCurves[int(EqType::Tx)][int(EqMode::SsbData)] = EqCurve::User;
    const MenuNumbers menus = menuNumbersFor(kModelTs590SG);
    m_menus[menus.usbAudioIn] = 4;
    m_menus[menus.usbAudioOut] = 5;
}

bool SimulatorTransport::open()
{
    m_open = true;
    return true;
}

void SimulatorTransport::close()
{
    m_open = false;
}

bool SimulatorTransport::isOpen() const
{
    return m_open;
}

QString SimulatorTransport::errorString() const
{
    return {};
}

QString SimulatorTransport::description() const
{
    return QStringLiteral("Simulated TS-590SG");
}

void SimulatorTransport::write(const QByteArray &data)
{
    if (!m_open)
        return;
    m_input += data;
    qsizetype end;
    while ((end = m_input.indexOf(';')) >= 0) {
        const QByteArray command = m_input.left(end).toUpper();
        m_input.remove(0, end + 1);
        m_received.append(command);
        if (!m_unresponsive)
            handle(command);
    }
}

void SimulatorTransport::reply(const QByteArray &answer)
{
    // Answer asynchronously, like a real serial link.
    QTimer::singleShot(1, this, [this, answer] {
        if (m_open)
            emit dataReceived(answer + ';');
    });
}

void SimulatorTransport::echo(const QByteArray &answer)
{
    if (m_autoInfo != 0)
        reply(answer);
}

FilterLayout SimulatorTransport::currentFilters() const
{
    const int menu = m_data ? 29 : 28;
    return filterLayoutFor(m_mode, m_data, m_menus.value(menu) == 1);
}

QByteArray SimulatorTransport::answerFor(QByteArrayView name) const
{
    if (name == "FA")
        return "FA" + digits(m_freqA, 11);
    if (name == "FB")
        return "FB" + digits(m_freqB, 11);
    if (name == "MD")
        return "MD" + QByteArray::number(int(m_mode));
    if (name == "DA")
        return m_data ? "DA1" : "DA0";
    if (name == "AG")
        return "AG0" + digits(m_afGain, 3);
    if (name == "SQ")
        return "SQ0" + digits(m_squelch, 3);
    if (name == "RG")
        return "RG" + digits(m_rfGain, 3);
    if (name == "FL")
        return "FL" + QByteArray::number(m_filterSlot);
    if (name == "SH")
        return "SH" + digits(m_sh, 2);
    if (name == "SL")
        return "SL" + digits(m_sl, 2);
    if (name == "FW")
        return "FW" + digits(m_fw, 4);
    if (name == "IS")
        return "IS " + digits(m_is, 4);
    if (name == "UR" || name == "UT") {
        QByteArray out = name.toByteArray();
        for (int raw : m_eqLevels[name == "UR" ? int(EqType::Rx) : int(EqType::Tx)])
            out += digits(raw, 2);
        return out;
    }
    return {};
}

void SimulatorTransport::handle(const QByteArray &command)
{
    const QByteArrayView name = QByteArrayView(command).first(std::min<qsizetype>(2, command.size()));
    const QByteArrayView args = QByteArrayView(command).sliced(name.size());
    const bool isRead = args.isEmpty();

    if (command.isEmpty()) {
        reply("?");
        return;
    }

    if (name == "ID" && isRead)
        return reply("ID023");
    if (name == "FV" && isRead)
        return reply("FV1.07");
    if (name == "AI") {
        if (isRead)
            return reply("AI" + QByteArray::number(m_autoInfo));
        if (const auto v = number(args); v && (*v == 0 || *v == 2 || *v == 4)) {
            m_autoInfo = *v;
            return;
        }
        return reply("?");
    }
    if (name == "SM" && args == "0") {
        m_sMeter = std::clamp(m_sMeter + QRandomGenerator::global()->bounded(-2, 3), 0, 30);
        return reply("SM0" + digits(m_sMeter, 4));
    }
    if (name == "FR" || name == "FT") {
        int &vfo = name == "FR" ? m_rxVfo : m_txVfo;
        if (isRead)
            return reply(name.toByteArray() + QByteArray::number(vfo));
        vfo = number(args).value_or(0);
        if (name == "FR")
            m_txVfo = vfo;
        return echo(name.toByteArray() + QByteArray::number(vfo));
    }
    if (name == "VV" && isRead) {
        // Copies the receive VFO onto the other one.
        if (m_rxVfo == 1)
            m_freqA = m_freqB;
        else
            m_freqB = m_freqA;
        echo(answerFor("FA"));
        return echo(answerFor("FB"));
    }
    if (name == "IF" && isRead) {
        const qint64 freq = m_rxVfo == 1 ? m_freqB : m_freqA;
        return reply("IF" + digits(freq, 11) + "      0000" + "00000" + "0"
                     + QByteArray::number(int(m_mode)) + QByteArray::number(m_rxVfo) + "000000");
    }
    if (isRead) {
        if (name == "FW" || name == "IS") {
            const FilterLayout layout = currentFilters();
            const FilterCommand needed = name == "FW" ? FilterCommand::FW : FilterCommand::IS;
            if (layout.first.command != needed && layout.second.command != needed)
                return reply("?");
        }
        const QByteArray answer = answerFor(name);
        return reply(answer.isEmpty() ? QByteArray("?") : answer);
    }

    // ---- set commands and parameterized reads
    if (name == "FA" || name == "FB") {
        const auto hz = parseNumber(args);
        if (args.size() != 11 || !hz || *hz < kMinFrequencyHz || *hz > kMaxFrequencyHz)
            return reply("?");
        (name == "FA" ? m_freqA : m_freqB) = *hz;
        return echo(answerFor(name));
    }
    if (name == "MD") {
        const auto mode = args.size() == 1 ? modeFromChar(args[0]) : std::nullopt;
        if (!mode)
            return reply("?");
        m_mode = *mode;
        if (!modeSupportsData(m_mode))
            m_data = false;
        return echo(answerFor("MD"));
    }
    if (name == "DA") {
        if (!modeSupportsData(m_mode))
            return reply("?");
        m_data = args == "1";
        return echo(answerFor("DA"));
    }
    if (name == "AG" || name == "SQ") {
        if (args.size() == 1)   // "AG0;" / "SQ0;" are reads
            return reply(answerFor(name));
        const auto v = args.size() == 4 ? number(args.sliced(1)) : std::nullopt;
        if (!v)
            return reply("?");
        (name == "AG" ? m_afGain : m_squelch) = std::min(*v, 255);
        return echo(answerFor(name));
    }
    if (name == "RG") {
        const auto v = number(args);
        if (!v)
            return reply("?");
        m_rfGain = std::min(*v, 255);
        return echo(answerFor("RG"));
    }
    if (name == "FL") {
        if (args != "1" && args != "2")
            return reply("?");
        m_filterSlot = args[0] - '0';
        return echo(answerFor("FL"));
    }
    if (name == "SH" || name == "SL" || name == "FW" || name == "IS") {
        const FilterLayout layout = currentFilters();
        const FilterCommand command = name == "SH"   ? FilterCommand::SH
                                      : name == "SL" ? FilterCommand::SL
                                      : name == "FW" ? FilterCommand::FW
                                                     : FilterCommand::IS;
        const FilterControl *control = layout.first.command == command    ? &layout.first
                                       : layout.second.command == command ? &layout.second
                                                                          : nullptr;
        const auto v = number(name == "IS" ? args.sliced(1) : args);
        if (!control || !v)
            return reply("?");
        int &target = name == "SH" ? m_sh : name == "SL" ? m_sl : name == "FW" ? m_fw : m_is;
        target = filterRawForIndex(*control, filterIndexForRaw(*control, *v));
        return echo(answerFor(name));
    }
    if (name == "EX") {
        const auto answer = parseMenu(command);
        if (!answer)
            return reply("?");
        if (answer->value.isEmpty())
            return reply(command + QByteArray::number(m_menus.value(answer->number)));
        m_menus[answer->number] = number(answer->value).value_or(0);
        return echo(command);
    }
    if (name == "EQ") {
        if (args.size() == 2) {
            if (args[0] < '0' || args[0] > '1' || args[1] < '0' || args[1] > '7')
                return reply("?");
            const auto curve = m_eqCurves[args[0] - '0'][args[1] - '0'];
            return reply(command + QByteArray::number(int(curve)));
        }
        const auto answer = parseEqCurve(command);
        if (!answer)
            return reply("?");
        if (!eqCurveSelectable(answer->type, answer->mode) && answer->curve != EqCurve::Off)
            return reply("?");
        m_eqCurves[int(answer->type)][int(answer->mode)] = answer->curve;
        return echo(command);
    }
    if (name == "UR" || name == "UT") {
        const auto answer = parseEqLevels(command);
        if (!answer || m_eqCurves[int(answer->first)][int(currentEqMode())] == EqCurve::Off)
            return reply("?");
        m_eqLevels[int(answer->first)] = answer->second;
        return echo(command);
    }
    reply("?");
}

}   // namespace ts590

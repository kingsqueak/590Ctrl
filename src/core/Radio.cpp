#include "Radio.h"

#include "CatTransport.h"

namespace ts590 {

namespace {

constexpr int kMeterIntervalMs = 200;
constexpr int kIdentifyAttempts = 3;
// Auto Information level used while connected: on, not persisted, so the
// radio drops back to its own setting if the app dies without cleaning up.
constexpr int kAutoInfoLevel = 2;
// Leading ';' clears any partial command left in the radio's input buffer.
// The radio answers it with "?;", which is expected and ignored.
const QByteArray kFlushCommand = ";";

std::optional<int> numberAt(const QByteArray &frame, qsizetype pos, qsizetype length = -1)
{
    const QByteArrayView view = length < 0 ? QByteArrayView(frame).sliced(pos)
                                           : QByteArrayView(frame).sliced(pos, length);
    const auto value = parseNumber(view);
    return value ? std::optional<int>(int(*value)) : std::nullopt;
}

}   // namespace

Radio::Radio(QObject *parent)
    : QObject(parent)
{
    connect(&m_link, &CatLink::frameReceived, this, &Radio::handleFrame);
    connect(&m_link, &CatLink::commandRejected, this, &Radio::handleRejected);
    connect(&m_link, &CatLink::queryTimedOut, this, &Radio::handleTimeout);
    m_meterTimer.setInterval(kMeterIntervalMs);
    connect(&m_meterTimer, &QTimer::timeout, this, &Radio::pollMeter);
}

Radio::~Radio()
{
    disconnectFromRadio();
}

void Radio::connectTransport(std::unique_ptr<CatTransport> transport)
{
    disconnectFromRadio();
    if (!transport->open()) {
        emit message(tr("Could not open %1: %2").arg(transport->description(), transport->errorString()));
        return;
    }
    connect(transport.get(), &CatTransport::failed, this, &Radio::handleTransportFailure);
    m_link.setTransport(std::move(transport));
    setState(State::Connecting);
    m_identifyAttempts = 0;
    m_link.send(kFlushCommand);
    m_link.query("ID;");
}

void Radio::disconnectFromRadio()
{
    if (m_state == State::Disconnected)
        return;
    if (CatTransport *transport = m_link.transport(); transport && transport->isOpen()) {
        m_link.clear();
        // Bypass the queue: put AI back the way we found it before closing.
        if (m_originalAutoInfo)
            transport->write(cmd::autoInfo(*m_originalAutoInfo));
        transport->flush();
    }
    teardown();
}

void Radio::teardown()
{
    m_meterTimer.stop();
    m_link.setTransport(nullptr);

    m_modelId = 0;
    m_firmware.clear();
    m_originalAutoInfo.reset();
    m_mode = Mode::None;
    m_data = false;
    m_transmitting = false;
    m_sh = m_sl = m_fw = m_is = std::nullopt;
    for (auto &perType : m_eqCurves) {
        for (auto &curve : perType)
            curve.reset();
    }
    m_eqLevels[0].reset();
    m_eqLevels[1].reset();
    setState(State::Disconnected);
}

void Radio::handleTransportFailure(const QString &text)
{
    emit message(tr("Lost connection to the radio: %1").arg(text));
    teardown();
}

void Radio::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void Radio::setMeterPolling(bool on)
{
    m_meterPolling = on;
}

// ------------------------------------------------------------------- queries

void Radio::requestFullState()
{
    m_link.query("FV;");
    m_link.query("AI;");
    m_link.send(cmd::autoInfo(kAutoInfoLevel));
    // Filter knob meaning depends on these, so read them before the mode.
    m_link.query(cmd::readMenu(28));
    m_link.query(cmd::readMenu(29));
    for (const char *read : {"FA;", "FB;", "FR;", "FT;", "IF;", "DA;", "AG0;", "SQ0;", "RG;"})
        m_link.query(read);
    // MD's answer sets the mode, which pulls everything mode-dependent.
    m_link.query("MD;");
}

void Radio::requestModeDependentState()
{
    m_link.query("FL;");
    requestFilterValues();
    refreshEq();
}

void Radio::requestFilterValues()
{
    const FilterLayout layout = filterLayout();
    for (const FilterControl *control : {&layout.first, &layout.second}) {
        if (control->command != FilterCommand::None)
            m_link.query(cmd::readFilterValue(control->command));
    }
}

void Radio::refreshEq()
{
    for (EqType type : {EqType::Tx, EqType::Rx}) {
        m_link.query(cmd::readEqCurve(type, eqMode()));
        m_link.query(cmd::readEqLevels(type));
    }
}

void Radio::refreshEqLevels(EqType type)
{
    m_link.query(cmd::readEqLevels(type));
}

void Radio::pollMeter()
{
    if (m_state == State::Connected && m_meterPolling && m_link.pendingCount() < 3)
        m_link.query("SM0;");
}

// ------------------------------------------------------------------- answers

void Radio::handleFrame(const QByteArray &frame)
{
    if (frame.size() < 2)
        return;
    const QByteArrayView name = QByteArrayView(frame).first(2);

    if (name == "ID") {
        m_modelId = numberAt(frame, 2).value_or(0);
        if (m_state != State::Connecting)
            return;
        if (m_modelId == kModelTs590S)
            emit message(tr("This is a TS-590S. 590Ctrl targets the TS-590SG; some filter tables differ."));
        else if (m_modelId != kModelTs590SG)
            emit message(tr("Unexpected radio ID %1; continuing anyway.").arg(m_modelId));
        setState(State::Connected);
        requestFullState();
        m_meterTimer.start();
    } else if (name == "FV") {
        m_firmware = QString::fromLatin1(frame.sliced(2));
        emit identified(m_modelId, m_firmware);
    } else if (name == "AI") {
        if (!m_originalAutoInfo)
            m_originalAutoInfo = numberAt(frame, 2);
    } else if (name == "FA" || name == "FB") {
        const Vfo vfo = name == "FA" ? Vfo::A : Vfo::B;
        if (const auto hz = parseNumber(QByteArrayView(frame).sliced(2)); hz && frame.size() == 13) {
            qint64 &field = vfo == Vfo::A ? m_freqA : m_freqB;
            if (field != *hz) {
                field = *hz;
                emit frequencyChanged(vfo, *hz);
            }
        }
    } else if ((name == "FR" || name == "FT") && frame.size() == 3) {
        const Vfo vfo = Vfo(std::clamp(frame[2] - '0', 0, 2));
        Vfo &field = name == "FR" ? m_rxVfo : m_txVfo;
        if (field != vfo) {
            field = vfo;
            emit vfoSelectionChanged(m_rxVfo, m_txVfo);
        }
    } else if (name == "MD" && frame.size() == 3) {
        if (const auto mode = modeFromChar(frame[2]))
            applyMode(*mode, std::nullopt);
    } else if (name == "DA" && frame.size() == 3) {
        applyMode(m_mode, frame[2] == '1');
    } else if (name == "IF") {
        if (const auto info = parseStatus(frame)) {
            update(m_transmitting, info->transmitting, &Radio::transmitChanged);
            if (info->mode != Mode::None)
                applyMode(info->mode, std::nullopt);
        }
    } else if (name == "AG" && frame.size() == 6) {
        if (const auto v = numberAt(frame, 3))
            update(m_afGain, *v, &Radio::afGainChanged);
    } else if (name == "SQ" && frame.size() == 6) {
        if (const auto v = numberAt(frame, 3))
            update(m_squelch, *v, &Radio::squelchChanged);
    } else if (name == "RG") {
        if (const auto v = numberAt(frame, 2))
            update(m_rfGain, *v, &Radio::rfGainChanged);
    } else if (name == "SM" && frame.size() == 7) {
        if (const auto v = numberAt(frame, 3))
            update(m_sMeter, *v, &Radio::sMeterChanged);
    } else if (name == "TX") {
        update(m_transmitting, true, &Radio::transmitChanged);
    } else if (name == "RX") {
        update(m_transmitting, false, &Radio::transmitChanged);
    } else if (name == "FL" && frame.size() == 3) {
        const FilterSlot slot = frame[2] == '2' ? FilterSlot::B : FilterSlot::A;
        if (slot != m_filterSlot) {
            m_filterSlot = slot;
            emit filterChanged();
            requestFilterValues();
        }
    } else if (name == "SH" || name == "SL" || name == "FW" || name == "IS") {
        // IS has a space (sign position) before its digits.
        const auto v = numberAt(frame, name == "IS" ? 3 : 2);
        if (!v)
            return;
        std::optional<int> &field = name == "SH"   ? m_sh
                                    : name == "SL" ? m_sl
                                    : name == "FW" ? m_fw
                                                   : m_is;
        if (field != v) {
            field = v;
            emit filterChanged();
        }
    } else if (name == "EX") {
        const auto menu = parseMenu(frame);
        if (!menu || (menu->number != 28 && menu->number != 29))
            return;
        // P5 = 0: "1 (HI/LO)", P5 = 1: "2 (WIDTH/SHIFT)".
        const bool widthShift = menu->value == "1";
        bool &field = menu->number == 28 ? m_ssbWidthShift : m_ssbDataWidthShift;
        if (field != widthShift) {
            field = widthShift;
            emit filterChanged();
            requestFilterValues();
        }
    } else if (name == "EQ") {
        if (const auto answer = parseEqCurve(frame)) {
            auto &field = m_eqCurves[int(answer->type)][int(answer->mode)];
            if (field != answer->curve) {
                field = answer->curve;
                if (answer->mode == eqMode())
                    emit eqCurveChanged(answer->type);
            }
        }
    } else if (name == "UR" || name == "UT") {
        if (const auto answer = parseEqLevels(frame)) {
            auto &field = m_eqLevels[int(answer->first)];
            if (field != answer->second) {
                field = answer->second;
                emit eqLevelsChanged(answer->first);
            }
        }
    }
}

void Radio::applyMode(Mode mode, std::optional<bool> data)
{
    const bool newData = modeSupportsData(mode) && data.value_or(m_data);
    if (mode == m_mode && newData == m_data)
        return;
    m_mode = mode;
    m_data = newData;
    emit modeChanged(m_mode, m_data);
    emit filterChanged();
    emit eqCurveChanged(EqType::Tx);
    emit eqCurveChanged(EqType::Rx);
    requestModeDependentState();
}

void Radio::handleRejected(const QByteArray &command, char code)
{
    if (command == kFlushCommand)
        return;
    if (m_state == State::Connecting && command == "ID;") {
        if (++m_identifyAttempts < kIdentifyAttempts) {
            m_link.query("ID;");
            return;
        }
    }
    if (code != '?') {
        emit message(tr("Serial communication error from radio (%1;) after %2")
                         .arg(QLatin1Char(code), QString::fromLatin1(command)));
        return;
    }
    if ((command.startsWith("UR") || command.startsWith("UT")) && command.size() > 3) {
        emit message(tr("The radio rejected the EQ levels. Select an EQ curve other than OFF first."));
        // Re-read so the UI snaps back to what the radio really has.
        m_link.query(command.left(2) + ';');
        return;
    }
    if (command.startsWith("EQ")) {
        emit message(tr("The radio rejected the EQ curve change (%1).").arg(QString::fromLatin1(command)));
        refreshEq();
        return;
    }
    if (!command.isEmpty())
        emit message(tr("Radio rejected %1").arg(QString::fromLatin1(command)));
}

void Radio::handleTimeout(const QByteArray &command)
{
    if (m_state == State::Connecting && command == "ID;") {
        const CatTransport *transport = m_link.transport();
        emit message(tr("No answer from the radio on %1. Check that it is powered on and that the "
                        "USB port speed (menu 068) matches.")
                         .arg(transport ? transport->description() : QString()));
        teardown();
        return;
    }
    if (command != "SM0;")
        emit message(tr("Radio did not answer %1").arg(QString::fromLatin1(command)));
}

// ------------------------------------------------------------------- setters

void Radio::setFrequency(Vfo vfo, qint64 hz)
{
    if (vfo == Vfo::Memory)
        return;
    hz = std::clamp(hz, kMinFrequencyHz, kMaxFrequencyHz);
    (vfo == Vfo::B ? m_freqB : m_freqA) = hz;
    m_link.send(cmd::frequency(vfo, hz), vfo == Vfo::B ? "FB" : "FA");
}

void Radio::setMode(Mode mode)
{
    m_link.send(cmd::mode(mode), "MD");
    m_link.query("MD;");
}

void Radio::setDataMode(bool on)
{
    if (!modeSupportsData(m_mode))
        return;
    m_link.send(cmd::dataMode(on), "DA");
    m_link.query("DA;");
}

void Radio::setAfGain(int level)
{
    m_afGain = std::clamp(level, 0, 255);
    m_link.send(cmd::afGain(m_afGain), "AG");
}

void Radio::setSquelch(int level)
{
    m_squelch = std::clamp(level, 0, 255);
    m_link.send(cmd::squelch(m_squelch), "SQ");
}

void Radio::setRfGain(int level)
{
    m_rfGain = std::clamp(level, 0, 255);
    m_link.send(cmd::rfGain(m_rfGain), "RG");
}

void Radio::setFilterSlot(FilterSlot slot)
{
    m_link.send(cmd::filterSlot(slot), "FL");
    m_link.query("FL;");
}

void Radio::setFilterValue(FilterCommand command, int raw)
{
    const QByteArray encoded = cmd::filterValue(command, raw);
    if (encoded.isEmpty())
        return;
    m_link.send(encoded, encoded.left(2));
    // Read back: the radio rounds FW/IS to the nearest supported value.
    m_link.query(cmd::readFilterValue(command));
}

void Radio::setEqCurve(EqType type, EqCurve curve)
{
    const EqMode mode = eqMode();
    if (!eqCurveSelectable(type, mode)) {
        emit message(tr("TX EQ is fixed OFF in %1.").arg(eqModeName(mode)));
        return;
    }
    const QByteArray command = cmd::eqCurve(type, mode, curve);
    m_link.send(command, command.left(4));
    m_link.query(cmd::readEqCurve(type, mode));
    m_link.query(cmd::readEqLevels(type));
}

void Radio::setEqLevels(EqType type, const EqLevels &levels)
{
    m_eqLevels[int(type)] = levels;
    m_link.send(cmd::eqLevels(type, levels), type == EqType::Rx ? "UR" : "UT");
}

// ---------------------------------------------------------------- accessors

FilterLayout Radio::filterLayout() const
{
    const bool widthShift = m_data ? m_ssbDataWidthShift : m_ssbWidthShift;
    return filterLayoutFor(m_mode, m_data, widthShift);
}

std::optional<int> Radio::filterValue(FilterCommand command) const
{
    switch (command) {
    case FilterCommand::SH: return m_sh;
    case FilterCommand::SL: return m_sl;
    case FilterCommand::FW: return m_fw;
    case FilterCommand::IS: return m_is;
    case FilterCommand::None: break;
    }
    return std::nullopt;
}

std::optional<EqCurve> Radio::eqCurve(EqType type) const
{
    return m_eqCurves[int(type)][int(eqMode())];
}

}   // namespace ts590

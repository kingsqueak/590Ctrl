#include "Radio.h"

#include "CatTransport.h"

#include <algorithm>

namespace ts590 {

namespace {

constexpr int kMeterIntervalMs = 200;
constexpr int kIdentifyAttempts = 3;
// After PS1 the radio takes a few seconds to boot; poll ID until it answers.
constexpr int kBootPollMs = 1000;
constexpr int kBootAttempts = 15;
// Wake-up from panel-off: ";" then PS1 within 2 s, at least 200 ms apart.
constexpr int kWakeDelayMs = 300;
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
    m_bootTimer.setInterval(kBootPollMs);
    connect(&m_bootTimer, &QTimer::timeout, this, &Radio::bootTick);
    // Each transmission starts its SWR/COMP/ALC readings from zero.
    connect(this, &Radio::transmitChanged, this, [this](bool transmitting) {
        if (transmitting && (m_txMeters[0] || m_txMeters[1] || m_txMeters[2])) {
            std::fill(std::begin(m_txMeters), std::end(m_txMeters), 0);
            emit txMetersChanged();
        }
    });
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
    // Power state first: an off radio answers PS but nothing else.
    m_link.query("PS;");
}

void Radio::powerOnAndConnect(std::unique_ptr<CatTransport> transport)
{
    disconnectFromRadio();
    if (!transport->open()) {
        emit message(tr("Could not open %1: %2").arg(transport->description(), transport->errorString()));
        return;
    }
    connect(transport.get(), &CatTransport::failed, this, &Radio::handleTransportFailure);
    m_link.setTransport(std::move(transport));
    setState(State::Connecting);
    m_link.send(kFlushCommand);
    QTimer::singleShot(kWakeDelayMs, this, [this] {
        if (m_state != State::Connecting)
            return;
        m_link.send("PS1;");
        startBooting();
    });
}

void Radio::setPower(bool on)
{
    if (on && m_state == State::PoweredOff) {
        m_link.send("PS1;");
        startBooting();
    } else if (!on && m_state == State::Connected) {
        m_link.clear();
        if (m_keyed || m_transmitting)
            m_link.send("RX;");
        m_keyed = false;
        m_link.send("PS0;");
        enterPoweredOff();
    }
}

void Radio::enterPoweredOff()
{
    m_meterTimer.stop();
    m_bootTimer.stop();
    m_awaitingIdentity = false;
    resetRadioState();
    setState(State::PoweredOff);
}

void Radio::startBooting()
{
    m_awaitingIdentity = true;
    m_bootAttempts = 0;
    m_bootTimer.start();
    bootTick();
}

void Radio::bootTick()
{
    if (!m_awaitingIdentity) {
        m_bootTimer.stop();
        return;
    }
    if (++m_bootAttempts > kBootAttempts) {
        m_bootTimer.stop();
        m_awaitingIdentity = false;
        emit message(tr("The radio did not come up after power-on."));
        if (m_state == State::Connecting)
            teardown();
        return;
    }
    m_link.query("ID;");
}

void Radio::disconnectFromRadio()
{
    if (m_state == State::Disconnected)
        return;
    if (CatTransport *transport = m_link.transport(); transport && transport->isOpen()) {
        m_link.clear();
        // Bypass the queue. Never leave the radio transmitting.
        if (m_keyed || m_transmitting)
            transport->write("RX;");
        // Put AI back the way we found it.
        if (m_originalAutoInfo)
            transport->write(cmd::autoInfo(*m_originalAutoInfo));
        transport->flush();
    }
    teardown();
}

void Radio::teardown()
{
    m_meterTimer.stop();
    m_bootTimer.stop();
    m_awaitingIdentity = false;
    m_link.setTransport(nullptr);
    m_originalAutoInfo.reset();
    resetRadioState();
    setState(State::Disconnected);
}

// Everything learned from the radio, so a fresh sync re-reads it all and
// every value is reported as changed.
void Radio::resetRadioState()
{
    m_keyed = false;
    m_vox = false;
    m_voxGain.reset();
    m_antenna = 1;
    m_rxAntenna = m_driveOut = m_attenuator = m_preamp = false;
    m_rxTuner = m_txTuner = m_tunerTuning = false;
    m_modelId = 0;
    m_firmware.clear();
    m_mode = Mode::None;
    m_data = false;
    m_transmitting = false;
    m_sh = m_sl = m_fw = m_is = std::nullopt;
    m_usbAudioIn.reset();
    m_usbAudioOut.reset();
    m_rit = m_xit = false;
    m_ritXitOffset = 0;
    m_txPower.reset();
    m_tunePower.reset();
    m_powerFine = false;
    m_carrierLevel.reset();
    m_micGain.reset();
    m_txMonitor.reset();
    for (auto &filter : m_txFilters)
        filter.reset();
    m_notchMode = NotchMode::Off;
    m_notchWide = false;
    m_notchPosition.reset();
    m_noiseBlanker = NoiseBlanker::Off;
    m_nbLevel.reset();
    m_noiseReduction = NoiseReduction::Off;
    m_nrLevel.reset();
    m_agc = Agc::Off;
    m_agcTime.reset();
    m_processor = false;
    m_processorInput.reset();
    m_processorOutput.reset();
    for (auto &perType : m_eqCurves) {
        for (auto &curve : perType)
            curve.reset();
    }
    m_eqLevels[0].reset();
    m_eqLevels[1].reset();
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
    const MenuNumbers menus = menuNumbersFor(m_modelId);
    // Filter knob meaning depends on these, so read them before the mode.
    for (int menu :
         {menus.ssbFilterMode, menus.ssbDataFilterMode, menus.usbAudioIn, menus.usbAudioOut, menus.powerFine}) {
        if (menu >= 0)
            m_link.query(cmd::readMenu(menu));
    }
    for (int menu : menus.txFilter)
        m_link.query(cmd::readMenu(menu));
    for (const char *read : {"FA;", "FB;", "FR;", "FT;", "IF;", "RT;", "XT;", "DA;", "AG0;", "SQ0;", "RG;", "PC;", "NT;", "BP;", "NB;", "NR;", "CG;", "MG;", "ML;", "VX;", "VG;", "AN;", "RA;", "PA;", "AC;", "PR;", "PL;"})
        m_link.query(read);
    if (m_modelId == kModelTs590SG)
        m_link.query("TP;");
    // MD's answer sets the mode, which pulls everything mode-dependent.
    m_link.query("MD;");
}

void Radio::requestModeDependentState()
{
    // AGC isn't available in FM (the radio rejects GC), so read it per mode.
    if (agcSupported(m_mode))
        m_link.query("GC;");
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
    if (m_state != State::Connected || !m_meterPolling || m_link.pendingCount() >= 3)
        return;
    m_link.query("SM0;");
    // SWR/COMP/ALC only mean something on transmit; one RM read returns all three.
    if (m_transmitting)
        m_link.query("RM;");
}

// ------------------------------------------------------------------- answers

void Radio::handleFrame(const QByteArray &frame)
{
    if (frame.size() < 2)
        return;
    const QByteArrayView name = QByteArrayView(frame).first(2);

    if (name == "PS" && frame.size() == 3) {
        const bool on = frame[2] == '1';
        if (m_state == State::Connecting && !m_awaitingIdentity) {
            // Answer to the initial PS query.
            if (on) {
                m_awaitingIdentity = true;
                m_link.query("ID;");
            } else {
                enterPoweredOff();
            }
        } else if (!on && m_state == State::Connected) {
            enterPoweredOff();   // switched off at the panel (reported via AI)
        } else if (on && m_state == State::PoweredOff && !m_awaitingIdentity) {
            startBooting();      // switched on at the panel
        }
    } else if (name == "ID") {
        m_modelId = numberAt(frame, 2).value_or(0);
        if (!m_awaitingIdentity)
            return;
        m_awaitingIdentity = false;
        m_bootTimer.stop();
        if (m_modelId == kModelTs590S)
            emit message(tr("This is a TS-590S. 590Ctrl targets the TS-590SG; some filter tables differ."));
        else if (m_modelId != kModelTs590SG)
            emit message(tr("Unexpected radio ID %1; continuing anyway.").arg(m_modelId));
        // The S has no menu for it: SSB is HI/LO, SSB-DATA is WIDTH/SHIFT.
        m_ssbWidthShift = false;
        m_ssbDataWidthShift = m_modelId == kModelTs590S;
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
            if (info->rit != m_rit || info->xit != m_xit || info->ritXitOffsetHz != m_ritXitOffset) {
                m_rit = info->rit;
                m_xit = info->xit;
                m_ritXitOffset = info->ritXitOffsetHz;
                emit ritXitChanged();
            }
            if (info->mode != Mode::None)
                applyMode(info->mode, std::nullopt);
        }
    } else if ((name == "RT" || name == "XT") && frame.size() == 3) {
        bool &field = name == "RT" ? m_rit : m_xit;
        if (field != (frame[2] == '1')) {
            field = frame[2] == '1';
            emit ritXitChanged();
        }
    } else if ((name == "CG" || name == "MG" || name == "ML") && frame.size() == 5) {
        const auto level = numberAt(frame, 2);
        std::optional<int> &field = name == "CG" ? m_carrierLevel : name == "MG" ? m_micGain : m_txMonitor;
        if (level && field != *level) {
            field = *level;
            if (name == "CG")
                emit carrierLevelChanged(*level);
            else if (name == "MG")
                emit micGainChanged(*level);
            else
                emit txMonitorChanged(*level);
        }
    } else if ((name == "PC" || name == "TP") && frame.size() == 5) {
        const auto watts = numberAt(frame, 2);
        std::optional<int> &field = name == "PC" ? m_txPower : m_tunePower;
        if (watts && field != *watts) {
            field = *watts;
            if (name == "PC")
                emit txPowerChanged(*watts);
            else
                emit tunePowerChanged(*watts);
        }
    } else if (name == "NT" && frame.size() == 4) {
        const NotchMode mode = frame[2] == '1' ? NotchMode::Auto : frame[2] == '2' ? NotchMode::Manual : NotchMode::Off;
        const bool wide = frame[3] == '1';
        if (mode != m_notchMode || wide != m_notchWide) {
            m_notchMode = mode;
            m_notchWide = wide;
            emit notchChanged();
        }
    } else if (name == "NB" && frame.size() == 3) {
        const int value = frame[2] - '0';
        if (value < 0 || value > 3)
            return;
        if (NoiseBlanker(value) != m_noiseBlanker) {
            m_noiseBlanker = NoiseBlanker(value);
            // NB1 and NB2 keep separate levels; fetch the one now in use.
            m_nbLevel.reset();
            if (nbLevelAdjustable(m_noiseBlanker))
                m_link.query("NL;");
            emit noiseBlankerChanged();
        }
    } else if (name == "PR" && frame.size() == 3) {
        if (m_processor != (frame[2] == '1')) {
            m_processor = frame[2] == '1';
            emit processorChanged();
        }
    } else if (name == "PL" && frame.size() == 8) {
        const auto input = numberAt(frame, 2, 3);
        const auto output = numberAt(frame, 5, 3);
        if (input && output && (m_processorInput != *input || m_processorOutput != *output)) {
            m_processorInput = *input;
            m_processorOutput = *output;
            emit processorChanged();
        }
    } else if (name == "GC" && frame.size() == 3) {
        const int value = frame[2] - '0';
        if (value < 0 || value > 2)
            return;
        if (Agc(value) != m_agc) {
            m_agc = Agc(value);
            // Slow and fast keep their own time constants; fetch the active one.
            m_agcTime.reset();
            if (m_agc != Agc::Off)
                m_link.query("GT;");
            emit agcChanged();
        }
    } else if (name == "GT" && frame.size() == 4) {
        const auto value = numberAt(frame, 2);
        if (value && m_agcTime != *value) {
            m_agcTime = *value;
            emit agcChanged();
        }
    } else if (name == "NL" && frame.size() == 5) {
        const auto level = numberAt(frame, 2);
        if (level && m_nbLevel != *level) {
            m_nbLevel = *level;
            emit noiseBlankerChanged();
        }
    } else if (name == "NR" && frame.size() == 3) {
        const int value = frame[2] - '0';
        if (value < 0 || value > 2)
            return;
        if (NoiseReduction(value) != m_noiseReduction) {
            m_noiseReduction = NoiseReduction(value);
            m_nrLevel.reset();
            if (m_noiseReduction != NoiseReduction::Off)
                m_link.query("RL;");
            emit noiseReductionChanged();
        }
    } else if (name == "RL" && frame.size() == 4) {
        const auto level = numberAt(frame, 2);
        if (level && m_nrLevel != *level) {
            m_nrLevel = *level;
            emit noiseReductionChanged();
        }
    } else if (name == "BP" && frame.size() == 5) {
        const auto position = numberAt(frame, 2);
        if (position && m_notchPosition != *position) {
            m_notchPosition = *position;
            emit notchPositionChanged(*position);
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
    } else if (name == "RM" && frame.size() == 7) {
        const int meter = frame[2] - '1';
        const auto dots = numberAt(frame, 3);
        if (meter >= 0 && meter < 3 && dots && m_txMeters[meter] != *dots) {
            m_txMeters[meter] = *dots;
            emit txMetersChanged();
        }
    } else if (name == "VG" && frame.size() == 5) {
        if (const auto level = numberAt(frame, 2); level && m_voxGain != *level) {
            m_voxGain = *level;
            emit voxGainChanged(*level);
        }
    } else if (name == "AN" && frame.size() == 5) {
        const int antenna = frame[2] == '2' ? 2 : 1;
        const bool rxAntenna = frame[3] == '1';
        const bool driveOut = frame[4] == '1';
        if (antenna != m_antenna || rxAntenna != m_rxAntenna || driveOut != m_driveOut) {
            m_antenna = antenna;
            m_rxAntenna = rxAntenna;
            m_driveOut = driveOut;
            emit antennaChanged();
        }
    } else if (name == "PA" && frame.size() >= 3) {
        update(m_preamp, frame[2] == '1', &Radio::preampChanged);
    } else if (name == "AC" && frame.size() == 5) {
        const bool rx = frame[2] == '1', tx = frame[3] == '1', tuning = frame[4] == '1';
        if (rx != m_rxTuner || tx != m_txTuner || tuning != m_tunerTuning) {
            m_rxTuner = rx;
            m_txTuner = tx;
            m_tunerTuning = tuning;
            emit tunerChanged();
        }
    } else if (name == "RA" && frame.size() >= 4) {
        update(m_attenuator, frame.sliced(2, 2) == "01", &Radio::attenuatorChanged);
    } else if (name == "VX" && frame.size() == 3) {
        update(m_vox, frame[2] == '1', &Radio::voxChanged);
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
        const auto value = menu ? parseNumber(menu->value) : std::nullopt;
        if (!value)
            return;
        const MenuNumbers menus = menuNumbersFor(m_modelId);
        if (menu->number == menus.usbAudioIn || menu->number == menus.usbAudioOut) {
            const bool in = menu->number == menus.usbAudioIn;
            std::optional<int> &field = in ? m_usbAudioIn : m_usbAudioOut;
            if (field != int(*value)) {
                field = int(*value);
                if (in)
                    emit usbAudioInChanged(*field);
                else
                    emit usbAudioOutChanged(*field);
            }
        } else if (const auto it = std::find(menus.txFilter.begin(), menus.txFilter.end(), menu->number);
                   it != menus.txFilter.end()) {
            const auto filter = TxFilter(it - menus.txFilter.begin());
            std::optional<int> &field = m_txFilters[int(filter)];
            if (field != int(*value)) {
                field = int(*value);
                emit txFilterChanged(filter);
            }
        } else if (menu->number == menus.powerFine) {
            update(m_powerFine, *value == 1, &Radio::powerFineChanged);
        } else if (menu->number == menus.ssbFilterMode || menu->number == menus.ssbDataFilterMode) {
            // P5 = 0: "1 (HI/LO)", P5 = 1: "2 (WIDTH/SHIFT)".
            const bool widthShift = *value == 1;
            bool &field = menu->number == menus.ssbFilterMode ? m_ssbWidthShift : m_ssbDataWidthShift;
            if (field != widthShift) {
                field = widthShift;
                emit filterChanged();
                requestFilterValues();
            }
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
    if (command == "ID;" && m_bootTimer.isActive())
        return;   // still booting; the boot timer asks again
    if (m_awaitingIdentity && command == "ID;") {
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
    if (command == "TX0;" || command == "TX2;") {
        m_keyed = false;
        emit message(tr("The radio refused to transmit (check TX inhibit and the band edges)."));
        emit transmitChanged(false);
        return;
    }
    // AGC reads are refused in FM / with AGC off; that's expected, not an error.
    if (command == "GC;" || command == "GT;")
        return;
    if (command == cmd::startTuning()) {
        emit message(tr("The radio refused to start tuning (the tuner may not cover this band)."));
        m_link.query("AC;");
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
    if (command == "ID;" && m_bootTimer.isActive())
        return;   // still booting; the boot timer asks again
    if (m_state == State::Connecting && (command == "PS;" || command == "ID;")) {
        const CatTransport *transport = m_link.transport();
        emit message(tr("No answer from the radio on %1. If it is switched off, use the power button to "
                        "turn it on; otherwise check that the USB port speed (menu 068) matches.")
                         .arg(transport ? transport->description() : QString()));
        teardown();
        return;
    }
    if (command != "SM0;" && command != "RM;")   // meter polls: next poll will do
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

void Radio::requestVfoState()
{
    m_link.query("FR;");
    m_link.query("FT;");
    // The VFOs keep their own mode; pick up whatever the new one uses.
    m_link.query("MD;");
    m_link.query("DA;");
}

void Radio::selectVfo(Vfo vfo)
{
    if (vfo == Vfo::Memory)
        return;
    m_link.send(cmd::rxVfo(vfo), "FR");
    requestVfoState();
}

void Radio::setSplit(bool on)
{
    if (m_rxVfo == Vfo::Memory)
        return;
    if (on)
        m_link.send(cmd::txVfo(m_rxVfo == Vfo::A ? Vfo::B : Vfo::A), "FT");
    else
        m_link.send(cmd::rxVfo(m_rxVfo), "FR");   // FR returns to simplex
    requestVfoState();
}

void Radio::copyVfo()
{
    m_link.send(cmd::copyVfo());
    m_link.query("FA;");
    m_link.query("FB;");
    m_link.query("MD;");
    m_link.query("DA;");
}

void Radio::setMenuValue(int menu, std::optional<int> &field, int value, int maximum)
{
    if (menu < 0)
        return;
    value = std::clamp(value, 0, maximum);
    field = value;
    const QByteArray read = cmd::readMenu(menu);
    m_link.send(cmd::setMenu(menu, QByteArray::number(value)), read.chopped(1));
    // EX sets don't reliably echo; read back so the UI tracks the radio.
    m_link.query(read);
}

void Radio::setUsbAudioIn(int level)
{
    setMenuValue(menuNumbersFor(m_modelId).usbAudioIn, m_usbAudioIn, level, kUsbAudioLevelMax);
}

void Radio::setUsbAudioOut(int level)
{
    setMenuValue(menuNumbersFor(m_modelId).usbAudioOut, m_usbAudioOut, level, kUsbAudioLevelMax);
}

void Radio::setTxFilter(TxFilter filter, int index)
{
    setMenuValue(menuNumbersFor(m_modelId).txFilter[int(filter)], m_txFilters[int(filter)], index,
                 int(txFilterValuesHz(filter).size()) - 1);
}

void Radio::setRit(bool on)
{
    m_link.send(cmd::rit(on), "RT");
    m_link.query("RT;");
}

void Radio::setXit(bool on)
{
    m_link.send(cmd::xit(on), "XT");
    m_link.query("XT;");
}

void Radio::setRitXitOffset(int hz)
{
    if (!m_rit && !m_xit)
        return;
    m_ritXitOffset = std::clamp(hz, -kRitXitMaxHz, kRitXitMaxHz);
    // One coalescable command: dragging only ever sends the latest offset.
    m_link.send(cmd::ritXitOffset(m_ritXitOffset), "RC");
    m_link.query("IF;");
}

void Radio::setPower(QByteArray (*encode)(int), std::optional<int> &field, int watts, int maximum)
{
    watts = std::clamp(watts, kTxPowerMin, maximum);
    if (!m_powerFine)
        watts -= watts % 5;   // the radio rounds down to 5 W steps
    field = watts;
    const QByteArray command = encode(watts);
    m_link.send(command, command.left(2));
    m_link.query(command.left(2) + ';');
}

void Radio::setLevel(QByteArray (*encode)(int), std::optional<int> &field, int level, int maximum)
{
    field = std::clamp(level, 0, maximum);
    const QByteArray command = encode(*field);
    m_link.send(command, command.left(2));
    m_link.query(command.left(2) + ';');
}

void Radio::setCarrierLevel(int level)
{
    setLevel(&cmd::carrierLevel, m_carrierLevel, level, kCarrierLevelMax);
}

void Radio::setMicGain(int level)
{
    setLevel(&cmd::micGain, m_micGain, level, kMicGainMax);
}

void Radio::setTxMonitor(int level)
{
    setLevel(&cmd::txMonitor, m_txMonitor, level, txMonitorMax(m_modelId));
}

void Radio::setTxPower(int watts)
{
    setPower(&cmd::txPower, m_txPower, watts, txPowerMax(m_mode));
}

void Radio::setTunePower(int watts)
{
    if (m_modelId == kModelTs590SG)
        setPower(&cmd::tunePower, m_tunePower, watts, kTxPowerMax);   // TX tune isn't limited in AM
}

void Radio::setNotch(NotchMode mode, bool wide)
{
    if (!notchSupported(m_mode))
        return;
    m_link.send(cmd::notch(mode, wide), "NT");
    m_link.query("NT;");
}

void Radio::setNotchPosition(int position)
{
    if (!notchSupported(m_mode))
        return;
    m_notchPosition = std::clamp(position, 0, kNotchPositionMax);
    m_link.send(cmd::notchPosition(*m_notchPosition), "BP");
    m_link.query("BP;");
}

void Radio::setProcessor(bool on)
{
    m_link.send(cmd::processor(on), "PR");
    m_link.query("PR;");
}

void Radio::setProcessorLevels(int input, int output)
{
    m_processorInput = std::clamp(input, 0, kProcessorLevelMax);
    m_processorOutput = std::clamp(output, 0, kProcessorLevelMax);
    m_link.send(cmd::processorLevels(*m_processorInput, *m_processorOutput), "PL");
    m_link.query("PL;");
}

void Radio::selectBand(int band)
{
    if (m_state != State::Connected)
        return;
    // No coalescing key: pressing the same band twice steps its memories
    // twice, so every press has to reach the radio.
    m_link.send(cmd::band(band));
    for (const char *read : {"FA;", "FB;", "MD;", "DA;"})
        m_link.query(read);
}

void Radio::setAgc(Agc agc)
{
    if (!agcSupported(m_mode))
        return;
    // From off, the radio only accepts GC3 (back on in its previous slow/fast
    // mode); GC1/GC2 are rejected until AGC is on. No coalescing key on GC3,
    // so it can't be merged away by the GC1/GC2 that follows.
    if (m_agc == Agc::Off && agc != Agc::Off)
        m_link.send(cmd::agcResume());
    m_link.send(cmd::agc(agc), "GC");
    m_link.query("GC;");
}

void Radio::setAgcTimeConstant(int value)
{
    if (m_agc == Agc::Off || !agcSupported(m_mode))
        return;
    m_agcTime = std::clamp(value, kAgcTimeMin, kAgcTimeMax);
    m_link.send(cmd::agcTimeConstant(*m_agcTime), "GT");
    m_link.query("GT;");
}

void Radio::setNoiseBlanker(NoiseBlanker nb)
{
    if (nb != NoiseBlanker::Off && !noiseBlankerSupported(m_mode))
        return;
    m_link.send(cmd::noiseBlanker(nb), "NB");
    m_link.query("NB;");
}

void Radio::setNbLevel(int level)
{
    if (!nbLevelAdjustable(m_noiseBlanker))
        return;
    m_nbLevel = std::clamp(level, kNbLevelMin, kNbLevelMax);
    m_link.send(cmd::nbLevel(*m_nbLevel), "NL");
    m_link.query("NL;");
}

void Radio::setNoiseReduction(NoiseReduction nr)
{
    if (nr == NoiseReduction::NR2 && !nr2Supported(m_mode))
        return;
    m_link.send(cmd::noiseReduction(nr), "NR");
    m_link.query("NR;");
}

void Radio::setNrLevel(int level)
{
    if (m_noiseReduction == NoiseReduction::Off)
        return;
    const auto [low, high] = nrLevelRange(m_noiseReduction);
    m_nrLevel = std::clamp(level, low, high);
    m_link.send(cmd::nrLevel(m_noiseReduction, *m_nrLevel), "RL");
    m_link.query("RL;");
}

void Radio::setVox(bool on)
{
    m_link.send(on ? "VX1;" : "VX0;", "VX");
    m_link.query("VX;");
}

void Radio::setVoxGain(int level)
{
    setLevel(&cmd::voxGain, m_voxGain, level, kVoxGainMax);
}

void Radio::setAntenna(int number)
{
    m_link.send(cmd::antenna(number));
    m_link.query("AN;");
}

void Radio::setRxAntenna(bool on)
{
    m_link.send(cmd::rxAntenna(on));
    m_link.query("AN;");
}

void Radio::setDriveOut(bool on)
{
    m_link.send(cmd::driveOut(on));
    m_link.query("AN;");
}

void Radio::setAttenuator(bool on)
{
    m_link.send(cmd::attenuator(on), "RA");
    m_link.query("RA;");
}

void Radio::setPreamp(bool on)
{
    m_link.send(cmd::preamp(on), "PA");
    m_link.query("PA;");
}

void Radio::setTuner(bool on)
{
    m_link.send(cmd::tuner(m_rxTuner, on), "AC");
    m_link.query("AC;");
}

void Radio::setTunerTuning(bool on)
{
    if (m_state != State::Connected)
        return;
    m_link.send(on ? cmd::startTuning() : cmd::stopTuning(m_rxTuner), "AC");
    m_link.query("AC;");
}

void Radio::setTransmit(bool on)
{
    if (m_state != State::Connected && on)
        return;
    m_keyed = on;
    // Same key for TX and RX: a quick tap that's still queued collapses to
    // the final state instead of keying up and down.
    m_link.send(on ? "TX0;" : "RX;", "TX");
}

void Radio::setTuneCarrier(bool on)
{
    if (m_state != State::Connected && on)
        return;
    m_keyed = on;
    m_link.send(on ? "TX2;" : "RX;", "TX");
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

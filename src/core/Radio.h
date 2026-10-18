#pragma once

#include "CatCodec.h"
#include "CatLink.h"

#include <QObject>
#include <QTimer>

#include <memory>
#include <optional>

namespace ts590 {

class CatTransport;

// Live model of a TS-590SG. Setters send CAT commands; the cached state is
// updated from the radio's answers and its Auto Information (AI2) output, and
// the *Changed signals fire only when a value actually changes.
class Radio : public QObject
{
    Q_OBJECT

public:
    enum class State { Disconnected, Connecting, Connected };
    Q_ENUM(State)

    explicit Radio(QObject *parent = nullptr);
    ~Radio() override;

    // Opens the transport, identifies the radio and pulls its state.
    void connectTransport(std::unique_ptr<CatTransport> transport);
    // Restores the radio's original AI setting and closes the transport.
    void disconnectFromRadio();

    State state() const { return m_state; }
    CatLink *link() { return &m_link; }
    int modelId() const { return m_modelId; }
    QString firmware() const { return m_firmware; }

    qint64 frequency(Vfo vfo) const { return vfo == Vfo::B ? m_freqB : m_freqA; }
    Vfo rxVfo() const { return m_rxVfo; }
    Vfo txVfo() const { return m_txVfo; }
    bool split() const { return m_rxVfo != m_txVfo; }
    Mode mode() const { return m_mode; }
    bool dataMode() const { return m_data; }
    int afGain() const { return m_afGain; }
    int squelch() const { return m_squelch; }
    int rfGain() const { return m_rfGain; }
    int sMeter() const { return m_sMeter; }
    bool transmitting() const { return m_transmitting; }
    int txMeter(TxMeter meter) const { return m_txMeters[int(meter) - 1]; }   // dots, 0..30
    // Menu-backed USB audio levels, 0..9; nullopt until read.
    std::optional<int> usbAudioIn() const { return m_usbAudioIn; }
    std::optional<int> usbAudioOut() const { return m_usbAudioOut; }
    bool rit() const { return m_rit; }
    bool xit() const { return m_xit; }
    int ritXitOffset() const { return m_ritXitOffset; }   // Hz, shared by RIT and XIT
    std::optional<int> txPower() const { return m_txPower; }
    std::optional<int> tunePower() const { return m_tunePower; }   // TS-590SG only
    bool powerFine() const { return m_powerFine; }   // 1 W steps; otherwise 5 W
    std::optional<int> carrierLevel() const { return m_carrierLevel; }
    std::optional<int> micGain() const { return m_micGain; }
    std::optional<int> txMonitor() const { return m_txMonitor; }   // 0 = off
    // Index into txFilterValuesHz(filter); nullopt until read.
    std::optional<int> txFilter(TxFilter filter) const { return m_txFilters[int(filter)]; }
    NotchMode notchMode() const { return m_notchMode; }
    bool notchWide() const { return m_notchWide; }   // manual notch bandwidth
    std::optional<int> notchPosition() const { return m_notchPosition; }
    NoiseBlanker noiseBlanker() const { return m_noiseBlanker; }
    std::optional<int> nbLevel() const { return m_nbLevel; }   // of the blanker that's on
    NoiseReduction noiseReduction() const { return m_noiseReduction; }
    std::optional<int> nrLevel() const { return m_nrLevel; }   // NR1 level or NR2 speed

    FilterSlot filterSlot() const { return m_filterSlot; }
    FilterLayout filterLayout() const;
    std::optional<int> filterValue(FilterCommand command) const;

    EqMode eqMode() const { return eqModeFor(m_mode, m_data); }
    std::optional<EqCurve> eqCurve(EqType type) const;
    std::optional<EqLevels> eqLevels(EqType type) const { return m_eqLevels[int(type)]; }

    void setFrequency(Vfo vfo, qint64 hz);
    // Receive (and, simplex, transmit) on `vfo`. Cancels split.
    void selectVfo(Vfo vfo);
    // Split: transmit on the VFO not used for receive.
    void setSplit(bool on);
    // A=B: copies the receive VFO to the other one.
    void copyVfo();
    void setMode(Mode mode);
    void setDataMode(bool on);
    void setAfGain(int level);
    void setSquelch(int level);
    void setRfGain(int level);
    void setUsbAudioIn(int level);
    void setUsbAudioOut(int level);
    void setRit(bool on);
    void setXit(bool on);
    // Absolute offset. Needs RIT or XIT on, as on the radio.
    void setRitXitOffset(int hz);
    void setTxPower(int watts);
    void setTunePower(int watts);
    void setCarrierLevel(int level);
    void setMicGain(int level);
    void setTxMonitor(int level);   // 0 turns the monitor off
    void setTxFilter(TxFilter filter, int index);
    void setNotch(NotchMode mode, bool wide);
    void setNotchPosition(int position);
    void setNoiseBlanker(NoiseBlanker nb);
    void setNbLevel(int level);
    void setNoiseReduction(NoiseReduction nr);
    void setNrLevel(int level);
    void setFilterSlot(FilterSlot slot);
    void setFilterValue(FilterCommand command, int raw);
    // Selects the curve for the current mode family.
    void setEqCurve(EqType type, EqCurve curve);
    // Writes all 18 bands. Rejected by the radio while the curve is OFF; the
    // spec says levels persist only when the curve is U (User). There is one
    // U curve per EQ type, shared by all mode families (verified on hardware).
    void setEqLevels(EqType type, const EqLevels &levels);
    void refreshEq();
    void refreshEqLevels(EqType type);

    void setMeterPolling(bool on);

signals:
    void stateChanged(ts590::Radio::State state);
    void identified(int modelId, const QString &firmware);
    void frequencyChanged(ts590::Vfo vfo, qint64 hz);
    void vfoSelectionChanged(ts590::Vfo rx, ts590::Vfo tx);
    void modeChanged(ts590::Mode mode, bool data);
    void afGainChanged(int level);
    void squelchChanged(int level);
    void rfGainChanged(int level);
    void usbAudioInChanged(int level);
    void usbAudioOutChanged(int level);
    void ritXitChanged();   // on/off state or offset
    void txPowerChanged(int watts);
    void tunePowerChanged(int watts);
    void powerFineChanged(bool fine);
    void carrierLevelChanged(int level);
    void micGainChanged(int level);
    void txMonitorChanged(int level);
    void txFilterChanged(ts590::TxFilter filter);
    void notchChanged();   // mode or bandwidth
    void notchPositionChanged(int position);
    void noiseBlankerChanged();      // selection or level
    void noiseReductionChanged();    // selection or level
    void sMeterChanged(int dots);
    void transmitChanged(bool transmitting);
    void txMetersChanged();
    // Slot, layout or values changed.
    void filterChanged();
    void eqCurveChanged(ts590::EqType type);
    void eqLevelsChanged(ts590::EqType type);
    void message(const QString &text);   // user-facing errors and warnings

private:
    void handleFrame(const QByteArray &frame);
    void handleRejected(const QByteArray &command, char code);
    void handleTimeout(const QByteArray &command);
    void handleTransportFailure(const QString &message);
    void setState(State state);
    void requestFullState();
    void requestModeDependentState();
    void requestFilterValues();
    void applyMode(Mode mode, std::optional<bool> data);
    void setMenuValue(int menu, std::optional<int> &field, int value, int maximum);
    void requestVfoState();
    void setPower(QByteArray (*encode)(int), std::optional<int> &field, int watts, int maximum);
    void setLevel(QByteArray (*encode)(int), std::optional<int> &field, int level, int maximum);
    void pollMeter();
    void teardown();

    template <typename T, typename Signal>
    void update(T &field, T value, Signal signal)
    {
        if (field == value)
            return;
        field = value;
        emit(this->*signal)(value);
    }

    CatLink m_link;
    QTimer m_meterTimer;
    State m_state = State::Disconnected;
    bool m_meterPolling = true;

    int m_identifyAttempts = 0;
    int m_modelId = 0;
    QString m_firmware;
    std::optional<int> m_originalAutoInfo;

    qint64 m_freqA = 0;
    qint64 m_freqB = 0;
    Vfo m_rxVfo = Vfo::A;
    Vfo m_txVfo = Vfo::A;
    Mode m_mode = Mode::None;
    bool m_data = false;
    int m_afGain = 0;
    int m_squelch = 0;
    int m_rfGain = 0;
    int m_sMeter = 0;
    bool m_transmitting = false;
    int m_txMeters[3] = {};   // SWR, COMP, ALC
    std::optional<int> m_usbAudioIn;
    std::optional<int> m_usbAudioOut;
    bool m_rit = false;
    bool m_xit = false;
    int m_ritXitOffset = 0;
    std::optional<int> m_txPower;
    std::optional<int> m_tunePower;
    bool m_powerFine = false;
    std::optional<int> m_carrierLevel;
    std::optional<int> m_micGain;
    std::optional<int> m_txMonitor;
    std::optional<int> m_txFilters[kTxFilterCount];
    NotchMode m_notchMode = NotchMode::Off;
    bool m_notchWide = false;
    std::optional<int> m_notchPosition;
    NoiseBlanker m_noiseBlanker = NoiseBlanker::Off;
    std::optional<int> m_nbLevel;
    NoiseReduction m_noiseReduction = NoiseReduction::Off;
    std::optional<int> m_nrLevel;

    FilterSlot m_filterSlot = FilterSlot::A;
    bool m_ssbWidthShift = false;       // EX028
    bool m_ssbDataWidthShift = false;   // EX029
    std::optional<int> m_sh, m_sl, m_fw, m_is;

    std::optional<EqCurve> m_eqCurves[2][8];
    std::optional<EqLevels> m_eqLevels[2];
};

}   // namespace ts590

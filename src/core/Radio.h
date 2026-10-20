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
    // PoweredOff: the port is open and the radio answers PS, but it's
    // switched off (PS0), so nothing else is available.
    enum class State { Disconnected, Connecting, PoweredOff, Connected };
    Q_ENUM(State)

    explicit Radio(QObject *parent = nullptr);
    ~Radio() override;

    // Opens the transport, identifies the radio and pulls its state.
    void connectTransport(std::unique_ptr<CatTransport> transport);
    // Wakes a radio that was switched off at its panel (the PS1 procedure from
    // the reference guide), then connects as connectTransport does.
    void powerOnAndConnect(std::unique_ptr<CatTransport> transport);
    // Restores the radio's original AI setting and closes the transport.
    // Releases transmit first if this app keyed it.
    void disconnectFromRadio();

    // PS1 / PS0. Off uses PS0, not PS9, so PS1 can turn it back on directly.
    void setPower(bool on);

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
    bool vox() const { return m_vox; }   // break-in in CW
    std::optional<int> voxGain() const { return m_voxGain; }
    std::optional<int> voxDelay() const { return m_voxDelay; }   // ms
    bool locked() const { return m_lock; }
    bool muted() const { return m_mute; }
    bool fineTuning() const { return m_fineTuning; }
    int antenna() const { return m_antenna; }   // 1 or 2
    bool rxAntenna() const { return m_rxAntenna; }
    bool driveOut() const { return m_driveOut; }
    bool attenuator() const { return m_attenuator; }
    bool preamp() const { return m_preamp; }
    bool tuner() const { return m_txTuner; }          // TX-AT in line
    bool tunerTuning() const { return m_tunerTuning; }
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
    bool processor() const { return m_processor; }
    std::optional<int> processorInput() const { return m_processorInput; }
    std::optional<int> processorOutput() const { return m_processorOutput; }
    Agc agc() const { return m_agc; }
    std::optional<int> agcTimeConstant() const { return m_agcTime; }   // of the active AGC mode
    BeatCancel beatCancel() const { return m_beatCancel; }
    bool busy() const { return m_busy; }   // squelch open (BY)
    std::optional<int> keyerSpeed() const { return m_keyerSpeed; }       // WPM
    std::optional<int> breakInDelay() const { return m_breakInDelay; }   // ms, 0 = full break-in
    std::optional<int> cwPitch() const { return m_cwPitch; }             // menu index, see cwPitchHz()
    bool cwTune() const { return m_cwTune; }
    bool morseDecoder() const { return m_morseDecoder; }   // TS-590SG only
    std::optional<int> decoderThreshold() const { return m_decoderThreshold; }
    int cwQueued() const { return int(m_cwQueue.size()); }   // KY pieces not yet sent

    FilterSlot filterSlot() const { return m_filterSlot; }
    FilterLayout filterLayout() const;
    std::optional<int> filterValue(FilterCommand command) const;

    // Menu A or B (MF), as selected here or found at connect; nullopt until read.
    std::optional<MenuBank> menuBank() const { return m_menuBank; }

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
    // Mode and DATA together, e.g. CW -> USB-DATA, which setDataMode can't do
    // until the radio has confirmed the new mode.
    void setMode(Mode mode, bool data);
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
    void setVox(bool on);
    void setVoxGain(int level);
    void setVoxDelay(int ms);
    void setLock(bool on);
    // MU mutes only the received audio, so muting also takes the USB audio
    // output (to the computer and TCI clients) to 0, and unmuting puts the
    // previous level back unless it was changed in the meantime.
    void setMute(bool on);
    void setFineTuning(bool on);
    void setAntenna(int number);
    void setRxAntenna(bool on);
    void setDriveOut(bool on);
    void setAttenuator(bool on);
    void setPreamp(bool on);
    void setTuner(bool on);   // TX-AT in/through; doesn't start a tune
    // Runs (or aborts) an automatic tune cycle; transmits while it runs.
    void setTunerTuning(bool on);
    // TX0 (SEND, mic input) / RX.
    void setTransmit(bool on);
    // TX1 (DATA SEND: ACC2 or USB audio input, per the radio's menu) / RX.
    void setDataTransmit(bool on);
    // TX2 (TX Tune: carrier at the tune power level) / RX.
    void setTuneCarrier(bool on);
    void setTxFilter(TxFilter filter, int index);
    void setNotch(NotchMode mode, bool wide);
    void setNotchPosition(int position);
    void setNoiseBlanker(NoiseBlanker nb);
    void setNbLevel(int level);
    void setNoiseReduction(NoiseReduction nr);
    void setNrLevel(int level);
    void setAgc(Agc agc);
    void setProcessor(bool on);
    void setProcessorLevels(int input, int output);
    // BD: jump to a band (or step its band memories if already there).
    void selectBand(int band);
    void setAgcTimeConstant(int value);
    void setBeatCancel(BeatCancel bc);
    void setKeyerSpeed(int wpm);
    void setBreakInDelay(int ms);
    void setCwPitch(int index);
    // CA: auto-zero onto the CW signal being received (CW modes only).
    void setCwTune(bool on);
    void setMorseDecoder(bool on);
    void setDecoderThreshold(int level);
    // KY: keys the text in CW mode (transmitting if break-in is on). Text is
    // queued and fed to the radio as its buffer frees up.
    void sendCw(const QString &text);
    // Drops the queue and whatever the radio still has buffered.
    void stopCw();
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

    // MF: switches the radio to the other menu set, which changes every menu
    // setting at once.
    void selectMenuBank(MenuBank bank);
    // EX for either menu set. For the one not selected, the radio is switched
    // to it and back around the accesses. Answers arrive as menuItemRead.
    void readMenuItems(MenuBank bank, const QList<int> &numbers);
    void writeMenuItem(MenuBank bank, int number, const QByteArray &value);

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
    void voxChanged(bool on);
    void voxGainChanged(int level);
    void voxDelayChanged(int ms);
    void lockChanged(bool on);
    void muteChanged(bool on);
    void fineTuningChanged(bool on);
    void antennaChanged();   // ANT1/2, RX ANT or DRV
    void attenuatorChanged(bool on);
    void preampChanged(bool on);
    void tunerChanged();
    void txFilterChanged(ts590::TxFilter filter);
    void notchChanged();   // mode or bandwidth
    void notchPositionChanged(int position);
    void noiseBlankerChanged();      // selection or level
    void noiseReductionChanged();    // selection or level
    void agcChanged();               // mode or time constant
    void processorChanged();         // on/off or levels
    void beatCancelChanged();
    void busyChanged(bool busy);
    void keyerChanged();             // speed, break-in delay, pitch or CW TUNE
    void morseDecoderChanged();      // on/off or threshold
    void morseDecoded(const QString &text);
    void cwQueueChanged(int pieces);
    void sMeterChanged(int dots);
    void transmitChanged(bool transmitting);
    void txMetersChanged();
    // Slot, layout or values changed.
    void filterChanged();
    void eqCurveChanged(ts590::EqType type);
    void eqLevelsChanged(ts590::EqType type);
    void menuBankChanged(ts590::MenuBank bank);
    // Every EX answer, with the menu set it came from. value is the raw P5.
    void menuItemRead(ts590::MenuBank bank, int number, const QByteArray &value);
    void message(const QString &text);   // user-facing errors and warnings

private:
    void handleFrame(const QByteArray &frame);
    void handleRejected(const QByteArray &command, char code);
    void handleTimeout(const QByteArray &command);
    void handleTransportFailure(const QString &message);
    void setState(State state);
    void requestFullState();
    void requestTrackedMenus();
    // Runs `commands` with `bank` selected, switching to it and back if needed.
    template <typename Commands>
    void inMenuBank(MenuBank bank, Commands commands);
    void requestModeDependentState();
    void requestFilterValues();
    void applyMode(Mode mode, std::optional<bool> data);
    void setMenuValue(int menu, std::optional<int> &field, int value, int maximum, int width = 1);
    void requestVfoState();
    void setPower(QByteArray (*encode)(int), std::optional<int> &field, int watts, int maximum);
    void setLevel(QByteArray (*encode)(int), std::optional<int> &field, int level, int maximum);
    void pollMeter();
    void pumpCw();
    void teardown();
    void resetRadioState();
    void enterPoweredOff();
    void startBooting();
    void bootTick();
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
    bool m_awaitingIdentity = false;   // PS1 seen, waiting for ID to start the sync
    QTimer m_bootTimer;                // polls ID while the radio powers up
    int m_bootAttempts = 0;
    bool m_keyed = false;              // this app sent TX and hasn't sent RX
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
    bool m_vox = false;
    std::optional<int> m_voxGain;
    std::optional<int> m_voxDelay;
    bool m_lock = false;
    bool m_mute = false;
    bool m_fineTuning = false;
    std::optional<int> m_usbOutBeforeMute;   // restored on unmute
    int m_antenna = 1;
    bool m_rxAntenna = false;
    bool m_driveOut = false;
    bool m_attenuator = false;
    bool m_preamp = false;
    bool m_rxTuner = false;
    bool m_txTuner = false;
    bool m_tunerTuning = false;
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
    bool m_processor = false;
    std::optional<int> m_processorInput;
    std::optional<int> m_processorOutput;
    Agc m_agc = Agc::Off;
    std::optional<int> m_agcTime;
    BeatCancel m_beatCancel = BeatCancel::Off;
    bool m_busy = false;
    std::optional<int> m_keyerSpeed;
    std::optional<int> m_breakInDelay;
    std::optional<int> m_cwPitch;
    bool m_cwTune = false;
    bool m_morseDecoder = false;
    std::optional<int> m_decoderThreshold;
    QList<QByteArray> m_cwQueue;
    bool m_cwAwaitingBuffer = false;   // a KY read is out to check for buffer space
    QTimer m_cwTimer;                  // retries while the radio's buffer is full

    FilterSlot m_filterSlot = FilterSlot::A;
    bool m_ssbWidthShift = false;       // EX028
    bool m_ssbDataWidthShift = false;   // EX029
    std::optional<int> m_sh, m_sl, m_fw, m_is;

    std::optional<MenuBank> m_menuBank;    // the set in use
    std::optional<MenuBank> m_replyBank;   // the set selected when the latest answer was sent

    std::optional<EqCurve> m_eqCurves[2][8];
    std::optional<EqLevels> m_eqLevels[2];
};

}   // namespace ts590

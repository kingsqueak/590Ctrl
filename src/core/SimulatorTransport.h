#pragma once

#include "CatCodec.h"
#include "CatTransport.h"

#include <QElapsedTimer>
#include <QHash>
#include <QTimer>

namespace ts590 {

// An in-process stand-in for a TS-590SG covering the commands this app uses.
// It mirrors behavior observed on real hardware (e.g. a bare ";" answers "?;",
// UR/UT can be read but not written while the EQ curve is OFF) so the UI and
// the tests can run without a radio.
class SimulatorTransport : public CatTransport
{
    Q_OBJECT

public:
    explicit SimulatorTransport(QObject *parent = nullptr);

    bool open() override;
    void close() override;
    bool isOpen() const override;
    void write(const QByteArray &data) override;
    QString errorString() const override;
    QString description() const override;

    // Test hooks.
    QByteArrayList received() const { return m_received; }
    // Menu values in the selected menu set, or in a given one.
    int menu(int number) const { return menus().value(number); }
    void setMenu(int number, int value) { menus()[number] = value; }
    int menu(MenuBank bank, int number) const { return m_menuSets[int(bank)].value(number); }
    void setMenu(MenuBank bank, int number, int value) { m_menuSets[int(bank)][number] = value; }
    MenuBank menuBank() const { return m_menuBank; }
    EqLevels eqLevels(EqType type) const { return m_eqLevels[int(type)]; }
    EqCurve eqCurve(EqType type, EqMode mode) const { return m_eqCurves[int(type)][int(mode)]; }
    int afGain() const { return m_afGain; }
    qint64 frequency(Vfo vfo) const { return vfo == Vfo::B ? m_freqB : m_freqA; }
    Vfo rxVfo() const { return Vfo(m_rxVfo); }
    Vfo txVfo() const { return Vfo(m_txVfo); }
    int ritXitOffset() const { return m_ritXitOffset; }
    int txPower() const { return m_txPower; }
    int tunePower() const { return m_tunePower; }
    int carrierLevel() const { return m_carrierLevel; }
    int micGain() const { return m_micGain; }
    int txMonitor() const { return m_txMonitor; }
    int notchMode() const { return m_notchMode; }
    bool notchWide() const { return m_notchWide; }
    int notchPosition() const { return m_notchPosition; }
    int noiseBlanker() const { return m_noiseBlanker; }
    int agc() const { return m_agc; }
    bool processor() const { return m_processor; }
    int processorInput() const { return m_processorInput; }
    int processorOutput() const { return m_processorOutput; }
    int agcTime(int agc) const { return m_agcTimes[agc]; }
    int beatCancel() const { return m_beatCancel; }
    int keyerSpeed() const { return m_keyerSpeed; }
    int breakInDelay() const { return m_breakInDelay; }
    bool morseDecoder() const { return m_decoder; }
    int decoderThreshold() const { return m_decoderThreshold; }
    QByteArray keyedCw() const { return m_keyedCw; }   // everything KY has sent, padding dropped
    void setSMeter(int dots) { m_sMeter = dots; }
    void setSquelchLevel(int level) { m_squelch = level; }
    int nbLevel(int blanker) const { return m_nbLevels[blanker]; }
    int noiseReduction() const { return m_noiseReduction; }
    int nrLevel(int reduction) const { return m_nrLevels[reduction]; }
    void setUnresponsive(bool on) { m_unresponsive = on; }
    // PS0 state: answers PS only.
    void setPowered(bool on) { m_powered = on; }
    // Switched off at the front panel: silent until the wake-up sequence
    // (";" then "PS1;"), then booting for a moment before it answers.
    void setPanelOff(bool on) { m_panelOff = on; m_powered = !on; }
    bool powered() const { return m_powered; }
    bool transmitting() const { return m_transmitting; }
    bool vox() const { return m_vox; }
    int voxGain() const { return m_voxGain; }
    QByteArray antenna() const { return m_antenna; }
    bool attenuator() const { return m_attenuator; }
    bool preamp() const { return m_preamp; }
    QByteArray tuner() const { return m_tuner; }
    // Pretends to key up (reported via AI as TX/RX) with these meter readings.
    void simulateTransmit(bool on, int swr = 0, int comp = 0, int alc = 0);

private:
    void handle(const QByteArray &command);
    void reply(const QByteArray &answer);
    // With AI on, a successful set is echoed back as its answer.
    void echo(const QByteArray &answer);
    QByteArray answerFor(QByteArrayView name) const;
    EqMode currentEqMode() const { return eqModeFor(m_mode, m_data); }
    FilterLayout currentFilters() const;
    bool busy() const;

    bool m_open = false;
    bool m_unresponsive = false;
    QByteArray m_input;
    QByteArrayList m_received;

    int m_autoInfo = 0;
    qint64 m_freqA = 14'195'000;
    qint64 m_freqB = 7'074'000;
    int m_rxVfo = 0;
    int m_txVfo = 0;
    Mode m_mode = Mode::USB;
    bool m_data = false;
    int m_afGain = 80;
    int m_squelch = 0;
    int m_rfGain = 255;
    int m_filterSlot = 1;
    int m_sh = 10;
    int m_sl = 2;
    int m_fw = 500;
    int m_is = 800;
    int m_sMeter = 5;
    bool m_rit = false;
    bool m_xit = false;
    int m_ritXitOffset = 0;
    int m_txPower = 100;
    int m_tunePower = 10;
    int m_carrierLevel = 50;
    int m_micGain = 50;
    int m_txMonitor = 0;
    int m_notchMode = 0;
    bool m_notchWide = false;
    int m_notchPosition = 64;
    bool m_powered = true;
    bool m_panelOff = false;
    QElapsedTimer m_booting;
    bool m_vox = false;
    int m_voxGain = 4;
    QByteArray m_antenna = "100";   // AN P1 P2 P3
    bool m_attenuator = false;
    bool m_preamp = false;
    QByteArray m_tuner = "000";   // AC P1 P2 P3
    bool m_transmitting = false;
    int m_txMeters[3] = {};   // SWR, COMP, ALC
    bool m_processor = false;
    int m_processorInput = 50;
    int m_processorOutput = 50;
    int m_band = 4;                  // 14 MHz, matching m_freqA
    qint64 m_bandMemory[11] = {1'840'000, 3'573'000, 7'074'000, 10'136'000, 14'195'000, 18'100'000,
                               21'074'000, 24'915'000, 28'074'000, 50'313'000, 5'000'000};
    int m_beatCancel = 0;
    int m_keyerSpeed = 20;
    int m_breakInDelay = 300;
    bool m_cwTune = false;
    QByteArray m_keyedCw;
    bool m_decoder = false;
    int m_decoderThreshold = 10;
    QTimer m_decoderTimer;   // streams canned CD2 characters while the decoder is on
    int m_decoderPos = 0;
    int m_agc = 1;
    int m_agcBeforeOff = 1;
    int m_agcTimes[3] = {0, 12, 4};   // [1] slow, [2] fast
    int m_noiseBlanker = 0;
    int m_nbLevels[3] = {0, 3, 6};   // per blanker: [1] NB1, [2] NB2
    int m_noiseReduction = 0;
    int m_nrLevels[3] = {0, 5, 2};   // [1] NR1 level, [2] NR2 speed
    QHash<int, int> &menus() { return m_menuSets[int(m_menuBank)]; }
    const QHash<int, int> &menus() const { return m_menuSets[int(m_menuBank)]; }
    QHash<int, int> m_menuSets[2];   // menu A and B
    MenuBank m_menuBank = MenuBank::A;
    EqCurve m_eqCurves[2][8] = {};
    EqLevels m_eqLevels[2];
};

}   // namespace ts590

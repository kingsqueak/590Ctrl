#pragma once

#include "CatCodec.h"
#include "CatTransport.h"

#include <QElapsedTimer>
#include <QHash>

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
    int menu(int number) const { return m_menus.value(number); }
    void setMenu(int number, int value) { m_menus[number] = value; }
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
    bool m_transmitting = false;
    int m_txMeters[3] = {};   // SWR, COMP, ALC
    int m_noiseBlanker = 0;
    int m_nbLevels[3] = {0, 3, 6};   // per blanker: [1] NB1, [2] NB2
    int m_noiseReduction = 0;
    int m_nrLevels[3] = {0, 5, 2};   // [1] NR1 level, [2] NR2 speed
    QHash<int, int> m_menus;
    EqCurve m_eqCurves[2][8] = {};
    EqLevels m_eqLevels[2];
};

}   // namespace ts590

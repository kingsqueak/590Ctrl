#pragma once

#include "CatCodec.h"
#include "EqAutoLoader.h"
#include "EqPresetLibrary.h"
#include "Radio.h"

#include <QMainWindow>
#include <QTimer>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QFormLayout;
class QGroupBox;
class QLineEdit;
class QPlainTextEdit;
class EqualizerPanel;
class FrequencyDisplay;
class PowerButton;
class SMeter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createConnectionBar();
    QWidget *createVfoPanel();
    QWidget *createModeBar();
    QWidget *createOperatePage();
    QWidget *createRitXitGroup();
    QWidget *createTxFilterGroup();
    QWidget *createNotchGroup();
    QWidget *createTransmitGroup();
    QWidget *createEqualizerPage();
    QWidget *createBandsPage();
    QWidget *createCwPage();
    QSlider *createLevelSlider(void (ts590::Radio::*setter)(int), int maximum = 255);

    void refreshPorts();
    void toggleConnection();
    void togglePower();
    std::unique_ptr<ts590::CatTransport> createTransport();   // from the port/baud selection
    void onStateChanged(ts590::Radio::State state);
    void onModeChanged(ts590::Mode mode, bool data);
    void onVfoSelectionChanged(ts590::Vfo rx, ts590::Vfo tx);
    void refreshFilters();
    void refreshRitXit();
    void refreshNotch();
    void refreshNoise();
    void refreshAgc();
    void refreshBand();
    void refreshProcessor();
    void refreshBeatCancel();
    void refreshSquelchOpen();
    void refreshKeyer();
    void refreshDecoder();
    void refreshMonitor();
    void refreshAntenna();
    void refreshFrontEnd();   // ATT / PRE button
    void refreshEq(ts590::EqType type);
    void showMessage(const QString &text);

    ts590::Radio m_radio;
    ts590::EqPresetLibrary m_eqPresets;
    ts590::EqAutoLoader m_eqAutoLoader;
    bool m_applyingRadioState = false;   // suppresses UI -> radio while mirroring the radio

    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QPushButton *m_connectButton = nullptr;
    PowerButton *m_powerButton = nullptr;

    FrequencyDisplay *m_vfoA = nullptr;
    FrequencyDisplay *m_vfoB = nullptr;
    QLabel *m_vfoStatus = nullptr;
    QButtonGroup *m_vfoButtons = nullptr;
    QPushButton *m_splitButton = nullptr;
    QLabel *m_txIndicator = nullptr;
    SMeter *m_sMeter = nullptr;

    QButtonGroup *m_modeButtons = nullptr;
    QCheckBox *m_dataCheck = nullptr;
    QPushButton *m_antennaButton = nullptr;   // ANT1 / ANT2
    QPushButton *m_rxAntennaButton = nullptr;
    QPushButton *m_driveOutButton = nullptr;
    QPushButton *m_tunerButton = nullptr;
    QPushButton *m_tuneCycleButton = nullptr;   // ATun: run an AT tune
    QPushButton *m_attenuatorButton = nullptr;   // cycles ATT -> PRE -> off

    QSlider *m_afSlider = nullptr;
    QSlider *m_sqlSlider = nullptr;
    QSlider *m_rfSlider = nullptr;
    QSlider *m_usbInSlider = nullptr;
    QSlider *m_usbOutSlider = nullptr;
    QSlider *m_micGainSlider = nullptr;
    QPushButton *m_monitorButton = nullptr;
    QSlider *m_monitorSlider = nullptr;
    QPushButton *m_nbButton = nullptr;
    QSlider *m_nbSlider = nullptr;
    QPushButton *m_nrButton = nullptr;
    QSlider *m_nrSlider = nullptr;
    QLabel *m_nrValue = nullptr;
    QPushButton *m_agcButton = nullptr;
    QSlider *m_agcSlider = nullptr;
    QLabel *m_agcValue = nullptr;

    QPushButton *m_ritButton = nullptr;
    QPushButton *m_xitButton = nullptr;
    QPushButton *m_clearOffsetButton = nullptr;
    QSpinBox *m_offsetSpin = nullptr;
    QTimer m_offsetHold;   // ignore offset echoes briefly while the user adjusts

    QFormLayout *m_transmitForm = nullptr;
    QSlider *m_txPowerSlider = nullptr;
    QSlider *m_tunePowerSlider = nullptr;
    QSlider *m_carrierSlider = nullptr;
    QSlider *m_voxGainSlider = nullptr;
    // PROC: one button steps on -> IN (slider = input level) -> OUT (slider
    // = output level) -> off.
    enum class ProcessorTarget { None, Input, Output };
    ProcessorTarget m_processorTarget = ProcessorTarget::None;
    QPushButton *m_processorButton = nullptr;
    QSlider *m_processorSlider = nullptr;
    QLabel *m_processorValue = nullptr;
    QButtonGroup *m_bandButtons = nullptr;
    QPushButton *m_voxButton = nullptr;
    QPushButton *m_sendButton = nullptr;   // latching transmit
    QPushButton *m_txButton = nullptr;     // momentary transmit
    QPushButton *m_tuneCarrierButton = nullptr;   // TUN: latching TX Tune carrier

    QRadioButton *m_filterA = nullptr;
    QRadioButton *m_filterB = nullptr;
    QLabel *m_filterLabels[2] = {};
    QComboBox *m_filterCombos[2] = {};
    ts590::FilterLayout m_filterLayout;
    QComboBox *m_txFilterCombos[ts590::kTxFilterCount] = {};

    QPushButton *m_beatCancelButton = nullptr;   // cycles BC -> BC1 -> BC2 -> off
    QPushButton *m_autoNotchButton = nullptr;
    QPushButton *m_manualNotchButton = nullptr;
    QCheckBox *m_notchWideCheck = nullptr;
    QSlider *m_notchSlider = nullptr;

    QSlider *m_keyerSpeedSlider = nullptr;
    QSlider *m_pitchSlider = nullptr;
    QPushButton *m_breakInButton = nullptr;   // VOX key, which is break-in in CW
    QSlider *m_breakInDelaySlider = nullptr;  // 50 ms steps, 0 = full break-in
    QPushButton *m_cwTuneButton = nullptr;
    QLineEdit *m_cwEdit = nullptr;
    QPushButton *m_cwSendButton = nullptr;
    QPushButton *m_cwStopButton = nullptr;
    QLabel *m_cwQueueLabel = nullptr;
    QGroupBox *m_decoderGroup = nullptr;      // TS-590SG only
    QPushButton *m_decoderButton = nullptr;
    QSlider *m_decoderSlider = nullptr;
    QPlainTextEdit *m_decoderText = nullptr;

    EqualizerPanel *m_rxEq = nullptr;
    EqualizerPanel *m_txEq = nullptr;
    QLabel *m_radioInfo = nullptr;
    QList<QWidget *> m_radioControls;   // enabled only while connected
};

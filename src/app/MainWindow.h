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
    void refreshMonitor();
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

    QPushButton *m_ritButton = nullptr;
    QPushButton *m_xitButton = nullptr;
    QPushButton *m_clearOffsetButton = nullptr;
    QSpinBox *m_offsetSpin = nullptr;
    QTimer m_offsetHold;   // ignore offset echoes briefly while the user adjusts

    QFormLayout *m_transmitForm = nullptr;
    QSlider *m_txPowerSlider = nullptr;
    QSlider *m_tunePowerSlider = nullptr;
    QSlider *m_carrierSlider = nullptr;
    QPushButton *m_voxButton = nullptr;
    QPushButton *m_sendButton = nullptr;   // latching transmit
    QPushButton *m_txButton = nullptr;     // momentary transmit

    QRadioButton *m_filterA = nullptr;
    QRadioButton *m_filterB = nullptr;
    QLabel *m_filterLabels[2] = {};
    QComboBox *m_filterCombos[2] = {};
    ts590::FilterLayout m_filterLayout;
    QComboBox *m_txFilterCombos[ts590::kTxFilterCount] = {};

    QPushButton *m_autoNotchButton = nullptr;
    QPushButton *m_manualNotchButton = nullptr;
    QCheckBox *m_notchWideCheck = nullptr;
    QSlider *m_notchSlider = nullptr;

    EqualizerPanel *m_rxEq = nullptr;
    EqualizerPanel *m_txEq = nullptr;
    QLabel *m_radioInfo = nullptr;
    QList<QWidget *> m_radioControls;   // enabled only while connected
};

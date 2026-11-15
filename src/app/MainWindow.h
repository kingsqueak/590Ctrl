#pragma once

#include "CatCodec.h"
#include "DeepCwDecoder.h"
#include "EqAutoLoader.h"
#include "EqPresetLibrary.h"
#include "Qrz.h"
#include "Radio.h"
#include "RadioAudio.h"
#include "TciServer.h"
#include "UpdateChecker.h"

#include <QMainWindow>
#include <QTimer>

#include <memory>
#include <vector>

class QAction;
class QBoxLayout;
class QButtonGroup;
class QComboBox;
class QFrame;
class QLabel;
class QPushButton;
class QSettings;
class QSlider;
class QSpinBox;
class QLineEdit;
class QTabWidget;
class QPlainTextEdit;
class QTableWidget;
class EqualizerPanel;
class FrequencyDisplay;
class MiniWindow;
class PowerButton;
class SMeter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Where the tabs (CW, QRZ, Equalizer, Menu, Setup) sit: below the
    // operating controls, or beside them in a wider, shorter window.
    enum class Arrangement { Below, Side };
    void setArrangement(Arrangement arrangement);
    void setTabsVisible(bool visible);
    void showTab(int index);
    QSize tabMinimumSize(int index) const;   // for tools/ui-shots
    QWidget *miniWindow() const;             // for tools/ui-shots
    void connectSimulator();   // for tools/ui-shots

    // The full or the mini window, whichever was in use at the last quit.
    void showStartupWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createTopLine();
    QWidget *createVfoPanel();
    QWidget *createBandModeRow();
    QWidget *createOperatePanels();
    QWidget *createReceivePanel();
    QWidget *createFilterPanel();
    QWidget *createTransmitPanel();
    QWidget *createEqualizerPage();
    QWidget *createCwPage();
    QWidget *createQrzPage();
    QWidget *createSetupPage();
    QSlider *createLevelSlider(void (ts590::Radio::*setter)(int), int maximum = 255);
    QSlider *createPowerSlider(void (ts590::Radio::*setter)(int));
    QPushButton *createToggle(const QString &text, const QString &tip, void (ts590::Radio::*setter)(bool));

    void applyArrangement();
    void rememberSize();
    void fitWindow();
    void setRadioInfo(const QString &text, bool connected);

    void refreshPorts();
    void toggleConnection();
    void togglePower();
    std::unique_ptr<ts590::CatTransport> createTransport();   // from the port/baud selection
    void onStateChanged(ts590::Radio::State state);
    void onModeChanged(ts590::Mode mode, bool data);
    void onVfoSelectionChanged(ts590::Vfo rx, ts590::Vfo tx);
    void setMiniMode(bool mini);
    void shutDown();   // once, on close or quit: release the radio, save the windows
    void refreshFilters();
    void refreshRitXit();
    void refreshNotch();
    void refreshNoise();
    void refreshAgc();
    void refreshBand();
    void refreshProcessor();
    void refreshBeatCancel();
    void refreshPanelToggles();   // LOCK, MUTE, FINE
    void refreshSquelchOpen();
    void refreshKeyer();
    void refreshDecoder();
    // The CW tab's decoders: the radio's own, or DeepCW on the radio's USB audio.
    bool deepCwChosen() const;
    void updateDeepCw();   // start or stop DeepCW to match the tab and the radio
    void stopDeepCw();
    void failDeepCw(const QString &message);
    void showDeepCwNote();
    void appendDecoded(const QString &text);
    void showDecoded();
    // QRZ
    void loadQrzAccount();
    void editQrzAccount();
    void lookUpCall();
    void logContact();
    void showLookup(const qrz::LookupResponse &response);
    void setQrzStatus(const QString &text, bool error = false);   // replaces both lines
    void refreshQrzStatus();
    void setCallWorked(bool worked);
    QString stationCallsign() const;
    void refreshMonitor();
    void refreshAntenna();
    void refreshFrontEnd();   // ATT / PRE button
    void refreshEq(ts590::EqType type);
    void showMessage(const QString &text);
    void showAbout();
    void checkForUpdates(bool userAsked);
    void onUpdateChecked(bool ok, const QString &version, const QString &page);
    void startTciServer();   // from the saved settings
    void editTciServer();
    void refreshTciStatus();
    static QString tciAddress(const QSettings &settings);

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
    QList<SMeter *> m_meters;   // the full and mini windows' meters

    MiniWindow *m_mini = nullptr;
    bool m_miniMode = false;
    bool m_shutDown = false;

    QButtonGroup *m_modeButtons = nullptr;
    QPushButton *m_dataButton = nullptr;
    QButtonGroup *m_antennaButtons = nullptr;   // ANT 1 / 2
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

    QList<QWidget *> m_tunePowerWidgets;   // the Setup row the TS-590S doesn't have
    QSlider *m_txPowerSlider = nullptr;
    QSlider *m_tunePowerSlider = nullptr;
    QSlider *m_carrierSlider = nullptr;
    QSlider *m_voxGainSlider = nullptr;
    QSlider *m_voxDelaySlider = nullptr;   // steps of kVoxDelayStepMs
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

    QPushButton *m_filterA = nullptr;
    QPushButton *m_filterB = nullptr;
    QLabel *m_filterLabels[2] = {};
    QComboBox *m_filterCombos[2] = {};
    ts590::FilterLayout m_filterLayout;
    QComboBox *m_txFilterCombos[ts590::kTxFilterCount] = {};

    QPushButton *m_beatCancelButton = nullptr;   // cycles BC -> BC1 -> BC2 -> off
    QPushButton *m_lockButton = nullptr;
    QPushButton *m_muteButton = nullptr;
    QPushButton *m_fineButton = nullptr;
    QPushButton *m_autoNotchButton = nullptr;
    QPushButton *m_manualNotchButton = nullptr;
    QPushButton *m_notchWideButton = nullptr;
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
    QWidget *m_decoderGroup = nullptr;
    QWidget *m_decoderChoice = nullptr;     // Radio | DeepCW, when there are both
    QButtonGroup *m_decoderButtons = nullptr;
    QPushButton *m_decoderButton = nullptr;
    QPushButton *m_wordsButton = nullptr;
    QList<QWidget *> m_thresholdWidgets;    // the radio's decoder only
    QSlider *m_decoderSlider = nullptr;
    QLabel *m_decoderNote = nullptr;        // DeepCW's state
    QPlainTextEdit *m_decoderText = nullptr;
    QString m_decodedRaw;                   // what the decoder printed
    QString m_decodedShown;                 // and what the tab shows: words formed, if WORDS is on
    bool m_hasRadioDecoder = true;          // TS-590SG; the S has none
    bool m_deepCw = false;                  // DeepCW chosen over the radio's decoder
    bool m_deepCwOn = false;                // and its DECODE button
    bool m_deepCwFailed = false;            // it stopped: don't retry until DECODE is pressed again
    bool m_deepCwReady = false;
    QString m_deepCwError;
    std::unique_ptr<ts590::DeepCwDecoder> m_deepCwDecoder;
    std::vector<float> m_deepCwMono;

    qrz::Client m_qrz;
    std::optional<qrz::LookupResponse> m_lastLookup;
    QString m_qrzUsername;   // the account; the password lives only in m_qrz
    QUrl m_qrzImageUrl;
    // Status area: the log check's line above the lookup's (or logging's).
    QString m_qrzWorkedText;
    bool m_qrzWorked = false;
    QString m_qrzStatusText;
    bool m_qrzStatusError = false;
    QLineEdit *m_qrzCall = nullptr;
    QPushButton *m_qrzLookupButton = nullptr;
    QPushButton *m_qrzLogButton = nullptr;
    QComboBox *m_qrzMode = nullptr;
    QLineEdit *m_qrzRstSent = nullptr;
    QLineEdit *m_qrzRstRcvd = nullptr;
    QLineEdit *m_qrzComment = nullptr;
    QLabel *m_qrzStatus = nullptr;
    QTableWidget *m_qrzTable = nullptr;
    QLabel *m_qrzImage = nullptr;

    EqualizerPanel *m_rxEq = nullptr;
    EqualizerPanel *m_txEq = nullptr;
    // Top line: status chips and passing messages.
    QLabel *m_radioChip = nullptr;
    QLabel *m_tciChip = nullptr;
    QLabel *m_tciSetupLabel = nullptr;   // the same, in words, on the Setup tab
    QLabel *m_message = nullptr;
    QTimer m_messageTimer;

    Arrangement m_arrangement = Arrangement::Below;
    bool m_tabsVisible = true;
    QWidget *m_operate = nullptr;   // header, band/mode row and the three panels
    QFrame *m_tabPanel = nullptr;
    QTabWidget *m_tabs = nullptr;
    QBoxLayout *m_body = nullptr;           // m_operate and m_tabPanel, below or beside
    QBoxLayout *m_setupColumns = nullptr;   // the Setup tab's columns, in a row or stacked
    QBoxLayout *m_cwColumns = nullptr;      // the CW tab's keyer and decoder, likewise
    QWidget *m_cwKeyer = nullptr;
    QPushButton *m_tabsButton = nullptr;
    QAction *m_tabsAction = nullptr;
    QAction *m_belowAction = nullptr;
    QAction *m_sideAction = nullptr;
    ts590::RadioAudio m_radioAudio;   // before m_tci, which uses it
    ts590::TciServer m_tci{&m_radio};

    // Update note to the right of the Mini button: a link to the newer
    // release and a close button that hides it until the next version.
    ts590::UpdateChecker m_updates;
    QWidget *m_updateBanner = nullptr;
    QLabel *m_updateLabel = nullptr;
    QString m_updateVersion;
    bool m_updateUserAsked = false;
    QList<QWidget *> m_radioControls;   // enabled only while connected
};

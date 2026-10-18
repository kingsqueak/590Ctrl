#pragma once

#include "CatCodec.h"
#include "Radio.h"

#include <QMainWindow>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class EqualizerPanel;
class FrequencyDisplay;

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
    QWidget *createReceivePage();
    QWidget *createEqualizerPage();
    QSlider *createLevelSlider(void (ts590::Radio::*setter)(int));

    void refreshPorts();
    void toggleConnection();
    void onStateChanged(ts590::Radio::State state);
    void onModeChanged(ts590::Mode mode, bool data);
    void onVfoSelectionChanged(ts590::Vfo rx, ts590::Vfo tx);
    void refreshFilters();
    void refreshEq(ts590::EqType type);
    void showMessage(const QString &text);

    ts590::Radio m_radio;
    bool m_applyingRadioState = false;   // suppresses UI -> radio while mirroring the radio

    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QPushButton *m_connectButton = nullptr;

    FrequencyDisplay *m_vfoA = nullptr;
    FrequencyDisplay *m_vfoB = nullptr;
    QLabel *m_vfoStatus = nullptr;
    QLabel *m_txIndicator = nullptr;
    QProgressBar *m_sMeter = nullptr;

    QButtonGroup *m_modeButtons = nullptr;
    QCheckBox *m_dataCheck = nullptr;

    QSlider *m_afSlider = nullptr;
    QSlider *m_sqlSlider = nullptr;
    QSlider *m_rfSlider = nullptr;

    QRadioButton *m_filterA = nullptr;
    QRadioButton *m_filterB = nullptr;
    QLabel *m_filterLabels[2] = {};
    QComboBox *m_filterCombos[2] = {};
    ts590::FilterLayout m_filterLayout;

    EqualizerPanel *m_rxEq = nullptr;
    EqualizerPanel *m_txEq = nullptr;
    QLabel *m_radioInfo = nullptr;
    QList<QWidget *> m_radioControls;   // enabled only while connected
};

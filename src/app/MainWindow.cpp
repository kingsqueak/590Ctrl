#include "MainWindow.h"

#include "PowerButton.h"
#include "CatMonitor.h"
#include "EqualizerPanel.h"
#include "FrequencyDisplay.h"
#include "SerialTransport.h"
#include "SMeter.h"
#include "SimulatorTransport.h"
#include "WheelSteps.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ts590;

namespace {

const QString kSimulatorPort = QStringLiteral("__simulator__");
constexpr int kMessageTimeoutMs = 8000;
const QList<qint32> kBaudRates = {9600, 19200, 38400, 57600, 115200};

constexpr int kVfoAPointSize = 36;
constexpr int kRitXitStepHz = 10;
constexpr int kOffsetHoldMs = 500;

// RIT/XIT offset: signed display ("+120 Hz") and one step per wheel click on
// macOS, like the frequency display.
class OffsetSpinBox : public QSpinBox
{
public:
    using QSpinBox::QSpinBox;

protected:
    QString textFromValue(int value) const override
    {
        return value > 0 ? QLatin1Char('+') + QString::number(value) : QString::number(value);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        event->accept();
        if (const int steps = wheelSteps(event, m_wheelRemainder))
            stepBy(steps);
    }

private:
    int m_wheelRemainder = 0;
};

// RL carries 0..9 for NR2, meaning a 2..20 ms SPAC follow speed.
QString nrLevelText(NoiseReduction nr, int level)
{
    return nr == NoiseReduction::NR2 ? QCoreApplication::translate("MainWindow", "%1 ms").arg((level + 1) * 2)
                                     : QString::number(level);
}

// Mode buttons in front-panel order.
const QList<Mode> kModes = {Mode::LSB, Mode::USB, Mode::CW, Mode::CWR,
                            Mode::FSK, Mode::FSKR, Mode::FM, Mode::AM};

}   // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_eqPresets(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/eq-presets"))
    , m_eqAutoLoader(&m_radio, &m_eqPresets)
{
    setWindowTitle(QStringLiteral("590Ctrl"));

    addToolBar(Qt::TopToolBarArea, [this] {
        auto *bar = new QToolBar(tr("Connection"));
        bar->setObjectName(QStringLiteral("connectionBar"));
        bar->setMovable(false);
        bar->addWidget(createConnectionBar());
        return bar;
    }());

    auto *tabs = new QTabWidget;
    tabs->addTab(createOperatePage(), tr("Operate"));
    tabs->addTab(createEqualizerPage(), tr("Equalizer"));

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(createVfoPanel());
    layout->addWidget(createModeBar());
    layout->addWidget(tabs, 1);
    setCentralWidget(central);

    auto *monitorDock = new QDockWidget(tr("CAT Monitor"));
    monitorDock->setObjectName(QStringLiteral("catMonitor"));
    monitorDock->setWidget(new CatMonitor(m_radio.link()));
    addDockWidget(Qt::BottomDockWidgetArea, monitorDock);
    monitorDock->hide();
    menuBar()->addMenu(tr("&View"))->addAction(monitorDock->toggleViewAction());

    m_radioInfo = new QLabel;
    statusBar()->addPermanentWidget(m_radioInfo);

    // ---- radio -> UI
    connect(&m_radio, &Radio::stateChanged, this, &MainWindow::onStateChanged);
    connect(&m_radio, &Radio::message, this, &MainWindow::showMessage);
    connect(&m_radio, &Radio::identified, this, [this](int model, const QString &firmware) {
        const QString name = model == kModelTs590SG  ? QStringLiteral("TS-590SG")
                             : model == kModelTs590S ? QStringLiteral("TS-590S")
                                                     : tr("ID %1").arg(model);
        // TX Tune power (TP) only exists on the SG.
        m_transmitForm->setRowVisible(1, model == kModelTs590SG);
        m_monitorSlider->setMaximum(txMonitorMax(model));
        m_radioInfo->setText(tr("%1 · firmware %2 · %3")
                                 .arg(name, firmware, m_radio.link()->transport()->description()));
    });
    connect(&m_radio, &Radio::frequencyChanged, this, [this](Vfo vfo, qint64 hz) {
        (vfo == Vfo::B ? m_vfoB : m_vfoA)->setFrequency(hz);
    });
    connect(&m_radio, &Radio::vfoSelectionChanged, this, &MainWindow::onVfoSelectionChanged);
    connect(&m_radio, &Radio::modeChanged, this, &MainWindow::onModeChanged);
    connect(&m_radio, &Radio::sMeterChanged, m_sMeter, &SMeter::setValue);
    connect(&m_radio, &Radio::txMetersChanged, this, [this] {
        m_sMeter->setTxMeters(m_radio.txMeter(TxMeter::Swr), m_radio.txMeter(TxMeter::Comp),
                              m_radio.txMeter(TxMeter::Alc));
    });
    connect(&m_radio, &Radio::transmitChanged, this, [this](bool tx) {
        m_txIndicator->setEnabled(tx);
        m_sMeter->setTransmitting(tx);
        // Back on receive for any reason (TX released, time-out timer,
        // refused): SEND is no longer latched.
        if (!tx)
            m_sendButton->setChecked(false);
    });
    connect(&m_radio, &Radio::voxChanged, m_voxButton, &QPushButton::setChecked);
    auto follow = [this](QSlider *slider) {
        return [this, slider](int value) {
            if (slider->isSliderDown())
                return;
            // Not a QSignalBlocker: the value label must still follow.
            m_applyingRadioState = true;
            slider->setValue(value);
            m_applyingRadioState = false;
        };
    };
    connect(&m_radio, &Radio::afGainChanged, this, follow(m_afSlider));
    connect(&m_radio, &Radio::squelchChanged, this, follow(m_sqlSlider));
    connect(&m_radio, &Radio::rfGainChanged, this, follow(m_rfSlider));
    connect(&m_radio, &Radio::usbAudioInChanged, this, follow(m_usbInSlider));
    connect(&m_radio, &Radio::usbAudioOutChanged, this, follow(m_usbOutSlider));
    connect(&m_radio, &Radio::txPowerChanged, this, follow(m_txPowerSlider));
    connect(&m_radio, &Radio::carrierLevelChanged, this, follow(m_carrierSlider));
    connect(&m_radio, &Radio::micGainChanged, this, follow(m_micGainSlider));
    connect(&m_radio, &Radio::txMonitorChanged, this, &MainWindow::refreshMonitor);
    connect(&m_radio, &Radio::tunePowerChanged, this, follow(m_tunePowerSlider));
    connect(&m_radio, &Radio::ritXitChanged, this, &MainWindow::refreshRitXit);
    connect(&m_radio, &Radio::notchChanged, this, &MainWindow::refreshNotch);
    connect(&m_radio, &Radio::noiseBlankerChanged, this, &MainWindow::refreshNoise);
    connect(&m_radio, &Radio::noiseReductionChanged, this, &MainWindow::refreshNoise);
    connect(&m_radio, &Radio::notchPositionChanged, this, follow(m_notchSlider));
    connect(&m_radio, &Radio::txFilterChanged, this, [this](TxFilter filter) {
        QComboBox *combo = m_txFilterCombos[int(filter)];
        combo->setCurrentIndex(m_radio.txFilter(filter).value_or(-1));
    });
    connect(&m_radio, &Radio::powerFineChanged, this, [this](bool fine) {
        // Match the radio: with Power fine OFF it only takes 5 W steps.
        for (QSlider *slider : {m_txPowerSlider, m_tunePowerSlider}) {
            slider->setSingleStep(fine ? 1 : 5);
            slider->setPageStep(fine ? 5 : 10);
        }
    });
    connect(&m_radio, &Radio::filterChanged, this, &MainWindow::refreshFilters);
    connect(&m_radio, &Radio::eqCurveChanged, this, &MainWindow::refreshEq);
    connect(&m_radio, &Radio::eqLevelsChanged, this, &MainWindow::refreshEq);
    connect(&m_eqAutoLoader, &EqAutoLoader::applied, this, [this](EqType type, EqMode mode, const QString &preset) {
        showMessage(tr("Loaded %1 preset “%2” into U for %3")
                        .arg(type == EqType::Rx ? tr("RX") : tr("TX"), preset, eqModeName(mode)));
    });

    // ---- UI -> radio
    connect(m_vfoA, &FrequencyDisplay::frequencyRequested, this,
            [this](qint64 hz) { m_radio.setFrequency(Vfo::A, hz); });
    connect(m_vfoB, &FrequencyDisplay::frequencyRequested, this,
            [this](qint64 hz) { m_radio.setFrequency(Vfo::B, hz); });
    for (EqualizerPanel *panel : {m_rxEq, m_txEq}) {
        const EqType type = panel == m_rxEq ? EqType::Rx : EqType::Tx;
        connect(panel, &EqualizerPanel::curveSelected, this,
                [this, type](EqCurve curve) { m_radio.setEqCurve(type, curve); });
        connect(panel, &EqualizerPanel::levelsEdited, this,
                [this, type](const EqLevels &levels) { m_radio.setEqLevels(type, levels); });
        connect(panel, &EqualizerPanel::reloadRequested, &m_radio, &Radio::refreshEq);
    }

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    onStateChanged(Radio::State::Disconnected);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_radio.disconnectFromRadio();
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    event->accept();
}

// ------------------------------------------------------------------ builders

QWidget *MainWindow::createConnectionBar()
{
    m_portCombo = new QComboBox;
    m_portCombo->setMinimumContentsLength(28);
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    auto *refresh = new QToolButton;
    refresh->setText(QStringLiteral("⟳"));
    refresh->setToolTip(tr("Rescan serial ports"));
    connect(refresh, &QToolButton::clicked, this, &MainWindow::refreshPorts);

    m_baudCombo = new QComboBox;
    for (qint32 baud : kBaudRates)
        m_baudCombo->addItem(tr("%1 bps").arg(baud), baud);
    m_baudCombo->setToolTip(tr("Must match radio menu 068 (USB port speed)"));
    const qint32 savedBaud = QSettings().value(QStringLiteral("serial/baud"), 115200).toInt();
    m_baudCombo->setCurrentIndex(std::max(0, m_baudCombo->findData(savedBaud)));

    m_connectButton = new QPushButton(tr("Connect"));
    m_connectButton->setDefault(true);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);

    m_powerButton = new PowerButton;
    m_powerButton->setToolTip(tr("Radio power"));
    connect(m_powerButton, &PowerButton::clicked, this, &MainWindow::togglePower);

    auto *bar = new QWidget;
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->addWidget(new QLabel(tr("Port:")));
    layout->addWidget(m_portCombo);
    layout->addWidget(refresh);
    layout->addWidget(m_baudCombo);
    layout->addWidget(m_connectButton);
    layout->addSpacing(6);
    layout->addWidget(m_powerButton);
    layout->addStretch();

    refreshPorts();
    return bar;
}

QWidget *MainWindow::createVfoPanel()
{
    m_vfoA = new FrequencyDisplay(kVfoAPointSize);
    m_vfoA->setLabel(QStringLiteral("A"));
    m_vfoB = new FrequencyDisplay(kVfoAPointSize / 2);
    m_vfoB->setLabel(QStringLiteral("B"));
    for (FrequencyDisplay *display : {m_vfoA, m_vfoB})
        display->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_vfoButtons = new QButtonGroup(this);
    m_vfoButtons->setExclusive(true);
    auto *vfoRow = new QHBoxLayout;
    for (Vfo vfo : {Vfo::A, Vfo::B}) {
        auto *button = new QPushButton(vfo == Vfo::A ? QStringLiteral("A") : QStringLiteral("B"));
        button->setCheckable(true);
        button->setToolTip(tr("Receive and transmit on VFO %1").arg(button->text()));
        m_vfoButtons->addButton(button, int(vfo));
        vfoRow->addWidget(button);
        m_radioControls << button;
    }
    connect(m_vfoButtons, &QButtonGroup::idClicked, this, [this](int id) { m_radio.selectVfo(Vfo(id)); });

    m_splitButton = new QPushButton(tr("SPLIT"));
    m_splitButton->setCheckable(true);
    m_splitButton->setToolTip(tr("Transmit on the other VFO"));
    connect(m_splitButton, &QPushButton::clicked, &m_radio, &Radio::setSplit);
    auto *copyButton = new QPushButton(tr("A=B"));
    copyButton->setToolTip(tr("Copy the receive VFO's frequency and mode to the other VFO"));
    connect(copyButton, &QPushButton::clicked, &m_radio, &Radio::copyVfo);
    vfoRow->addSpacing(8);
    vfoRow->addWidget(m_splitButton);
    vfoRow->addWidget(copyButton);
    m_radioControls << m_splitButton << copyButton;

    m_vfoStatus = new QLabel;
    m_txIndicator = new QLabel(tr(" TX "));
    m_txIndicator->setAlignment(Qt::AlignCenter);
    // Enabled == transmitting; the disabled look reads as "off".
    m_txIndicator->setStyleSheet(QStringLiteral(
        "QLabel { border-radius: 4px; padding: 2px 6px; font-weight: bold; color: white; background: #c0392b; }"
        "QLabel:disabled { color: palette(mid); background: transparent; border: 1px solid palette(mid); }"));
    m_txIndicator->setEnabled(false);

    // Peak readout at half the size of the main VFO digits.
    m_sMeter = new SMeter(kVfoAPointSize / 2);

    // One inset box, like a display window: VFO A on top, VFO B below it,
    // then the VFO buttons.
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addLayout(vfoRow);
    buttonRow->addWidget(m_vfoStatus);
    buttonRow->addStretch();
    buttonRow->addWidget(m_txIndicator);

    auto *vfoBox = new QFrame;
    vfoBox->setFrameShape(QFrame::StyledPanel);
    vfoBox->setFrameShadow(QFrame::Sunken);
    vfoBox->setAutoFillBackground(true);
    vfoBox->setBackgroundRole(QPalette::Base);
    auto *boxLayout = new QVBoxLayout(vfoBox);
    boxLayout->setContentsMargins(4, 4, 8, 6);
    boxLayout->setSpacing(2);
    boxLayout->addWidget(m_vfoA);
    boxLayout->addWidget(m_vfoB);
    boxLayout->addSpacing(4);
    boxLayout->addLayout(buttonRow);
    boxLayout->addStretch();

    auto *panel = new QWidget;
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(vfoBox, 3);
    layout->addWidget(m_sMeter, 2, Qt::AlignVCenter);
    m_radioControls << m_vfoA << m_vfoB;
    return panel;
}

QWidget *MainWindow::createModeBar()
{
    m_modeButtons = new QButtonGroup(this);
    m_modeButtons->setExclusive(true);

    auto *bar = new QWidget;
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    for (Mode mode : kModes) {
        auto *button = new QPushButton(modeName(mode));
        button->setCheckable(true);
        m_modeButtons->addButton(button, int(mode));
        layout->addWidget(button);
        m_radioControls << button;
    }
    connect(m_modeButtons, &QButtonGroup::idClicked, this, [this](int id) { m_radio.setMode(Mode(id)); });

    m_dataCheck = new QCheckBox(tr("DATA"));
    m_dataCheck->setToolTip(tr("Data mode (LSB/USB/FM/AM only)"));
    connect(m_dataCheck, &QCheckBox::clicked, &m_radio, &Radio::setDataMode);
    layout->addSpacing(12);
    layout->addWidget(m_dataCheck);
    layout->addStretch();
    m_radioControls << m_dataCheck;
    return bar;
}

QSlider *MainWindow::createLevelSlider(void (Radio::*setter)(int), int maximum)
{
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, maximum);
    slider->setPageStep(std::max(1, (maximum + 1) / 16));
    if (maximum < 16) {
        slider->setTickPosition(QSlider::TicksBelow);
        slider->setTickInterval(1);
    }
    connect(slider, &QSlider::valueChanged, this, [this, setter](int value) {
        if (!m_applyingRadioState)
            (m_radio.*setter)(value);
    });
    m_radioControls << slider;
    return slider;
}

QWidget *MainWindow::createOperatePage()
{
    m_afSlider = createLevelSlider(&Radio::setAfGain);
    m_sqlSlider = createLevelSlider(&Radio::setSquelch);
    m_rfSlider = createLevelSlider(&Radio::setRfGain);
    m_usbInSlider = createLevelSlider(&Radio::setUsbAudioIn, kUsbAudioLevelMax);
    m_usbInSlider->setToolTip(tr("USB audio input level (menu 071): audio from the computer to the transmitter"));
    m_usbOutSlider = createLevelSlider(&Radio::setUsbAudioOut, kUsbAudioLevelMax);
    m_usbOutSlider->setToolTip(tr("USB audio output level (menu 072): received audio to the computer"));

    auto *levels = new QGroupBox(tr("Levels"));
    auto *levelsLayout = new QFormLayout(levels);
    auto addLevel = [levelsLayout](const QString &name, QSlider *slider) {
        auto *value = new QLabel(QStringLiteral("0"));
        value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("255")) + 4);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(slider, &QSlider::valueChanged, value, qOverload<int>(&QLabel::setNum));
        auto *row = new QHBoxLayout;
        row->addWidget(slider, 1);
        row->addWidget(value);
        levelsLayout->addRow(name, row);
    };
    addLevel(tr("AF gain"), m_afSlider);
    addLevel(tr("Squelch"), m_sqlSlider);
    addLevel(tr("RF gain"), m_rfSlider);
    addLevel(tr("USB in"), m_usbInSlider);
    addLevel(tr("USB out"), m_usbOutSlider);
    m_micGainSlider = createLevelSlider(&Radio::setMicGain, kMicGainMax);
    m_micGainSlider->setToolTip(tr("Microphone gain for SSB and AM (FM mic gain is a menu setting)"));
    addLevel(tr("Mic gain"), m_micGainSlider);

    // NB / NR: the button steps through the variants like the front-panel
    // key; the slider sets the level of whichever variant is on.
    auto createNoiseRow = [this, levelsLayout](QPushButton *&button, QSlider *&slider, QLabel *value) {
        button = new QPushButton;
        button->setCheckable(true);
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(QStringLiteral("NB1+2")) + 24);
        slider = new QSlider(Qt::Horizontal);
        slider->setPageStep(1);
        slider->setTickPosition(QSlider::TicksBelow);
        slider->setTickInterval(1);
        value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("20 ms")) + 4);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *row = new QHBoxLayout;
        row->addWidget(slider, 1);
        row->addWidget(value);
        levelsLayout->addRow(button, row);
    };
    // TX monitor: ML has no on/off of its own (0 is off), so the button
    // sends 0 or the slider's level.
    auto *monitorValue = new QLabel;
    createNoiseRow(m_monitorButton, m_monitorSlider, monitorValue);
    m_monitorButton->setText(tr("MON"));
    m_monitorButton->setToolTip(tr("TX monitor: hear your own transmit audio"));
    m_monitorSlider->setRange(1, txMonitorMax(kModelTs590SG));
    m_monitorSlider->setValue(5);
    m_monitorSlider->setToolTip(tr("TX monitor level"));
    connect(m_monitorSlider, &QSlider::valueChanged, monitorValue, qOverload<int>(&QLabel::setNum));
    monitorValue->setNum(m_monitorSlider->value());
    connect(m_monitorSlider, &QSlider::valueChanged, this, [this](int level) {
        if (!m_applyingRadioState && m_radio.txMonitor().value_or(0) > 0)
            m_radio.setTxMonitor(level);
    });
    connect(m_monitorButton, &QPushButton::clicked, this, [this](bool on) {
        m_radio.setTxMonitor(on ? m_monitorSlider->value() : 0);
        // Radio caches the new level right away, so the echo won't signal a
        // change; update the row now.
        refreshMonitor();
    });

    auto *nbValue = new QLabel;
    createNoiseRow(m_nbButton, m_nbSlider, nbValue);
    m_nbButton->setToolTip(tr("Noise blanker: OFF → NB1 → NB2 → NB1+NB2"));
    m_nbSlider->setRange(kNbLevelMin, kNbLevelMax);
    m_nbSlider->setToolTip(tr("Level of the active noise blanker (not adjustable with both on)"));
    connect(m_nbSlider, &QSlider::valueChanged, nbValue, qOverload<int>(&QLabel::setNum));
    connect(m_nbSlider, &QSlider::valueChanged, this, [this](int level) {
        if (!m_applyingRadioState)
            m_radio.setNbLevel(level);
    });
    connect(m_nbButton, &QPushButton::clicked, this, [this] {
        static constexpr NoiseBlanker next[] = {NoiseBlanker::NB1, NoiseBlanker::NB2, NoiseBlanker::Both,
                                                NoiseBlanker::Off};
        m_radio.setNoiseBlanker(next[int(m_radio.noiseBlanker())]);
        refreshNoise();   // undo the button's own toggle until the radio answers
    });

    m_nrValue = new QLabel;
    createNoiseRow(m_nrButton, m_nrSlider, m_nrValue);
    m_nrButton->setToolTip(tr("Noise reduction: OFF → NR1 → NR2 (NR2 not in FM)"));
    m_nrSlider->setToolTip(tr("NR1: reduction level. NR2: SPAC follow speed."));
    connect(m_nrSlider, &QSlider::valueChanged, this, [this](int level) {
        m_nrValue->setText(nrLevelText(m_radio.noiseReduction(), level));
        if (!m_applyingRadioState)
            m_radio.setNrLevel(level);
    });
    connect(m_nrButton, &QPushButton::clicked, this, [this] {
        NoiseReduction next = NoiseReduction::Off;
        if (m_radio.noiseReduction() == NoiseReduction::Off)
            next = NoiseReduction::NR1;
        else if (m_radio.noiseReduction() == NoiseReduction::NR1 && nr2Supported(m_radio.mode()))
            next = NoiseReduction::NR2;
        m_radio.setNoiseReduction(next);
        refreshNoise();
    });

    m_filterA = new QRadioButton(tr("Filter A"));
    m_filterB = new QRadioButton(tr("Filter B"));
    connect(m_filterA, &QRadioButton::clicked, this, [this] { m_radio.setFilterSlot(FilterSlot::A); });
    connect(m_filterB, &QRadioButton::clicked, this, [this] { m_radio.setFilterSlot(FilterSlot::B); });
    m_radioControls << m_filterA << m_filterB;

    auto *filters = new QGroupBox(tr("IF Filter"));
    auto *filterLayout = new QGridLayout(filters);
    auto *slotRow = new QHBoxLayout;
    slotRow->addWidget(m_filterA);
    slotRow->addWidget(m_filterB);
    slotRow->addStretch();
    filterLayout->addLayout(slotRow, 0, 0, 1, 2);
    for (int i = 0; i < 2; ++i) {
        m_filterLabels[i] = new QLabel;
        m_filterCombos[i] = new QComboBox;
        connect(m_filterCombos[i], &QComboBox::activated, this, [this, i](int index) {
            const FilterControl &control = i == 0 ? m_filterLayout.first : m_filterLayout.second;
            m_radio.setFilterValue(control.command, filterRawForIndex(control, index));
        });
        filterLayout->addWidget(m_filterLabels[i], i + 1, 0);
        filterLayout->addWidget(m_filterCombos[i], i + 1, 1);
        m_radioControls << m_filterCombos[i];
    }
    filterLayout->setColumnStretch(1, 1);
    filterLayout->setRowStretch(3, 1);

    auto *right = new QVBoxLayout;
    right->addWidget(createRitXitGroup());
    right->addWidget(createTransmitGroup(), 1);

    auto *middle = new QVBoxLayout;
    middle->addWidget(filters);
    middle->addWidget(createTxFilterGroup());
    middle->addWidget(createNotchGroup());
    middle->addStretch();

    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->addWidget(levels, 3);
    layout->addLayout(middle, 2);
    layout->addLayout(right, 2);
    return page;
}

QWidget *MainWindow::createTxFilterGroup()
{
    auto *group = new QGroupBox(tr("TX Filter"));
    group->setToolTip(tr("Transmit audio filters (menus 031–034). SSB settings also apply to AM."));
    auto *layout = new QGridLayout(group);
    layout->addWidget(new QLabel(tr("Low cut")), 0, 1);
    layout->addWidget(new QLabel(tr("High cut")), 0, 2);
    layout->addWidget(new QLabel(tr("SSB/AM")), 1, 0);
    layout->addWidget(new QLabel(tr("SSB-DATA")), 2, 0);
    for (int i = 0; i < kTxFilterCount; ++i) {
        const auto filter = TxFilter(i);
        auto *combo = new QComboBox;
        for (int hz : txFilterValuesHz(filter))
            combo->addItem(tr("%1 Hz").arg(hz));
        combo->setCurrentIndex(-1);
        connect(combo, &QComboBox::activated, this, [this, filter](int index) { m_radio.setTxFilter(filter, index); });
        // TxFilter order: low/high for SSB, then low/high for SSB-DATA.
        layout->addWidget(combo, 1 + i / 2, 1 + i % 2);
        m_txFilterCombos[i] = combo;
        m_radioControls << combo;
    }
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(2, 1);
    return group;
}

QWidget *MainWindow::createNotchGroup()
{
    m_autoNotchButton = new QPushButton(tr("AUTO"));
    m_autoNotchButton->setCheckable(true);
    m_autoNotchButton->setToolTip(tr("Auto notch: tracks and removes a carrier"));
    connect(m_autoNotchButton, &QPushButton::clicked, this, [this](bool on) {
        m_radio.setNotch(on ? NotchMode::Auto : NotchMode::Off, m_notchWideCheck->isChecked());
    });
    m_manualNotchButton = new QPushButton(tr("MANUAL"));
    m_manualNotchButton->setCheckable(true);
    m_manualNotchButton->setToolTip(tr("Manual notch at the position set below"));
    connect(m_manualNotchButton, &QPushButton::clicked, this, [this](bool on) {
        m_radio.setNotch(on ? NotchMode::Manual : NotchMode::Off, m_notchWideCheck->isChecked());
    });
    m_notchWideCheck = new QCheckBox(tr("Wide"));
    m_notchWideCheck->setToolTip(tr("Manual notch bandwidth"));
    connect(m_notchWideCheck, &QCheckBox::clicked, this,
            [this](bool wide) { m_radio.setNotch(NotchMode::Manual, wide); });

    m_notchSlider = new QSlider(Qt::Horizontal);
    m_notchSlider->setRange(0, kNotchPositionMax);
    m_notchSlider->setPageStep(8);
    m_notchSlider->setToolTip(tr("Manual notch frequency (radio scale 0–127)"));
    connect(m_notchSlider, &QSlider::valueChanged, this, [this](int position) {
        if (!m_applyingRadioState)
            m_radio.setNotchPosition(position);
    });
    auto *value = new QLabel(QStringLiteral("0"));
    value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("127")) + 4);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_notchSlider, &QSlider::valueChanged, value, qOverload<int>(&QLabel::setNum));

    auto *group = new QGroupBox(tr("Notch"));
    auto *layout = new QGridLayout(group);
    layout->addWidget(m_autoNotchButton, 0, 0);
    layout->addWidget(m_manualNotchButton, 0, 1);
    layout->addWidget(m_notchWideCheck, 0, 2);
    auto *row = new QHBoxLayout;
    row->addWidget(m_notchSlider, 1);
    row->addWidget(value);
    layout->addLayout(row, 1, 0, 1, 3);
    return group;
}

QWidget *MainWindow::createRitXitGroup()
{
    m_ritButton = new QPushButton(tr("RIT"));
    m_ritButton->setCheckable(true);
    m_ritButton->setToolTip(tr("Receive incremental tuning"));
    connect(m_ritButton, &QPushButton::clicked, &m_radio, &Radio::setRit);
    m_xitButton = new QPushButton(tr("XIT"));
    m_xitButton->setCheckable(true);
    m_xitButton->setToolTip(tr("Transmit incremental tuning"));
    connect(m_xitButton, &QPushButton::clicked, &m_radio, &Radio::setXit);
    m_radioControls << m_ritButton << m_xitButton;

    m_offsetSpin = new OffsetSpinBox;
    m_offsetSpin->setRange(-kRitXitMaxHz, kRitXitMaxHz);
    m_offsetSpin->setSingleStep(kRitXitStepHz);
    m_offsetSpin->setSuffix(tr(" Hz"));
    m_offsetSpin->setAlignment(Qt::AlignRight);
    m_offsetSpin->setKeyboardTracking(false);
    m_offsetSpin->setToolTip(tr("Offset shared by RIT and XIT. Turn one of them on to adjust it."));
    m_offsetHold.setSingleShot(true);
    m_offsetHold.setInterval(kOffsetHoldMs);
    connect(&m_offsetHold, &QTimer::timeout, this, &MainWindow::refreshRitXit);
    connect(m_offsetSpin, &QSpinBox::valueChanged, this, [this](int hz) {
        if (m_applyingRadioState)
            return;
        m_offsetHold.start();
        m_radio.setRitXitOffset(hz);
    });

    m_clearOffsetButton = new QPushButton(tr("CLEAR"));
    m_clearOffsetButton->setToolTip(tr("Set the RIT/XIT offset to 0"));
    connect(m_clearOffsetButton, &QPushButton::clicked, this, [this] { m_offsetSpin->setValue(0); });

    auto *group = new QGroupBox(tr("RIT / XIT"));
    auto *layout = new QGridLayout(group);
    layout->addWidget(m_ritButton, 0, 0);
    layout->addWidget(m_xitButton, 0, 1);
    layout->addWidget(m_offsetSpin, 1, 0);
    layout->addWidget(m_clearOffsetButton, 1, 1);
    return group;
}

QWidget *MainWindow::createTransmitGroup()
{
    auto createPowerSlider = [this](void (Radio::*setter)(int)) {
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(kTxPowerMin, kTxPowerMax);
        connect(slider, &QSlider::valueChanged, this, [this, setter](int watts) {
            if (!m_applyingRadioState)
                (m_radio.*setter)(watts);
        });
        m_radioControls << slider;
        return slider;
    };
    m_txPowerSlider = createPowerSlider(&Radio::setTxPower);
    m_txPowerSlider->setToolTip(tr("Transmit output power (5–100 W, 5–25 W in AM)"));
    m_tunePowerSlider = createPowerSlider(&Radio::setTunePower);
    m_tunePowerSlider->setToolTip(tr("Output power used for TX Tune"));
    m_carrierSlider = createLevelSlider(&Radio::setCarrierLevel, kCarrierLevelMax);
    m_carrierSlider->setToolTip(tr("Carrier level for CW, FSK and AM"));

    auto *group = new QGroupBox(tr("Transmit"));
    m_transmitForm = new QFormLayout(group);
    for (auto [name, slider] : {std::pair{tr("TX power"), m_txPowerSlider}, std::pair{tr("Tune power"), m_tunePowerSlider}}) {
        auto *value = new QLabel;
        value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("100 W")) + 4);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(slider, &QSlider::valueChanged, value, [value](int watts) { value->setText(tr("%1 W").arg(watts)); });
        auto *row = new QHBoxLayout;
        row->addWidget(slider, 1);
        row->addWidget(value);
        m_transmitForm->addRow(name, row);
    }
    auto *carrierValue = new QLabel(QStringLiteral("0"));
    carrierValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    carrierValue->setMinimumWidth(carrierValue->fontMetrics().horizontalAdvance(QStringLiteral("100 W")) + 4);
    connect(m_carrierSlider, &QSlider::valueChanged, carrierValue, qOverload<int>(&QLabel::setNum));
    auto *carrierRow = new QHBoxLayout;
    carrierRow->addWidget(m_carrierSlider, 1);
    carrierRow->addWidget(carrierValue);
    m_transmitForm->addRow(tr("Carrier"), carrierRow);

    m_voxButton = new QPushButton(tr("VOX"));
    m_voxButton->setCheckable(true);
    m_voxButton->setToolTip(tr("Voice-operated transmit (break-in in CW)"));
    connect(m_voxButton, &QPushButton::clicked, &m_radio, &Radio::setVox);

    // SEND latches: red while transmitting until clicked again.
    m_sendButton = new QPushButton(tr("SEND"));
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setCheckable(true);
    m_sendButton->setToolTip(tr("Transmit until clicked again"));
    m_sendButton->setStyleSheet(QStringLiteral(
        "#sendButton { padding: 4px 12px; border: 1px solid palette(mid); border-radius: 4px;"
        "  background: palette(button); }"
        "#sendButton:checked { background: #e74c3c; color: white; font-weight: bold; border-color: #c0392b; }"
        "#sendButton:disabled { color: palette(mid); }"));
    connect(m_sendButton, &QPushButton::clicked, &m_radio, &Radio::setTransmit);

    // TX is momentary: transmits only while held down.
    m_txButton = new QPushButton(tr("TX"));
    m_txButton->setObjectName(QStringLiteral("txButton"));
    m_txButton->setToolTip(tr("Hold to transmit"));
    m_txButton->setStyleSheet(QStringLiteral(
        "#txButton { padding: 8px; border: none; border-radius: 4px; font-weight: bold; color: white;"
        "  background: #2ecc71; }"
        "#txButton:pressed { background: #e74c3c; }"
        "#txButton:disabled { background: palette(midlight); color: palette(mid); }"));
    connect(m_txButton, &QPushButton::pressed, this, [this] { m_radio.setTransmit(true); });
    connect(m_txButton, &QPushButton::released, this, [this] { m_radio.setTransmit(false); });
    // A held button can miss its release if the app loses focus mid-press.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState appState) {
        if (appState != Qt::ApplicationActive && m_txButton->isDown()) {
            m_txButton->setDown(false);
            m_radio.setTransmit(false);
        }
    });
    m_radioControls << m_voxButton << m_sendButton << m_txButton;

    auto *keying = new QGridLayout;
    keying->addWidget(m_voxButton, 0, 0);
    keying->addWidget(m_sendButton, 0, 1);
    keying->addWidget(m_txButton, 1, 0, 1, 2);
    m_transmitForm->addRow(keying);
    return group;
}

QWidget *MainWindow::createEqualizerPage()
{
    m_rxEq = new EqualizerPanel(EqType::Rx, &m_eqPresets);
    m_txEq = new EqualizerPanel(EqType::Tx, &m_eqPresets);
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    tabs->addTab(m_rxEq, tr("RX Equalizer"));
    tabs->addTab(m_txEq, tr("TX Equalizer"));
    return tabs;
}

// ---------------------------------------------------------------- connection

void MainWindow::refreshPorts()
{
    const QString current = m_portCombo->currentData().toString();
    const QString saved = QSettings().value(QStringLiteral("serial/port")).toString();
    m_portCombo->clear();

    int radioIndex = -1;
    for (const SerialPortCandidate &port : SerialTransport::availablePorts()) {
        QString text = port.portName;
        if (!port.description.isEmpty())
            text += QStringLiteral(" — ") + port.description;
        if (port.isTs590) {
            text += tr(" (TS-590)");
            if (radioIndex < 0)
                radioIndex = m_portCombo->count();
        }
        m_portCombo->addItem(text, port.portName);
    }
    m_portCombo->addItem(tr("Simulator (no radio)"), kSimulatorPort);

    // Prefer what was selected, then what was used last, then the radio.
    for (const QString &wanted : {current, saved}) {
        const int index = wanted.isEmpty() ? -1 : m_portCombo->findData(wanted);
        if (index >= 0) {
            m_portCombo->setCurrentIndex(index);
            return;
        }
    }
    m_portCombo->setCurrentIndex(radioIndex >= 0 ? radioIndex : 0);
}

void MainWindow::toggleConnection()
{
    if (m_radio.state() != Radio::State::Disconnected) {
        m_radio.disconnectFromRadio();
        return;
    }
    m_radio.connectTransport(createTransport());
}

std::unique_ptr<CatTransport> MainWindow::createTransport()
{
    const QString port = m_portCombo->currentData().toString();
    const qint32 baud = m_baudCombo->currentData().toInt();
    QSettings settings;
    settings.setValue(QStringLiteral("serial/port"), port);
    settings.setValue(QStringLiteral("serial/baud"), baud);
    if (port == kSimulatorPort)
        return std::make_unique<SimulatorTransport>();
    return std::make_unique<SerialTransport>(SerialSettings{port, baud});
}

void MainWindow::togglePower()
{
    switch (m_radio.state()) {
    case Radio::State::Disconnected:
        // Wakes a radio switched off at its panel, then connects.
        m_radio.powerOnAndConnect(createTransport());
        break;
    case Radio::State::PoweredOff:
        m_radio.setPower(true);
        showMessage(tr("Powering on…"));
        break;
    case Radio::State::Connected:
        if (QMessageBox::question(this, tr("Radio Power"), tr("Turn the radio off?")) == QMessageBox::Yes)
            m_radio.setPower(false);
        break;
    case Radio::State::Connecting: break;
    }
}

void MainWindow::onStateChanged(Radio::State state)
{
    const bool connected = state == Radio::State::Connected;
    m_connectButton->setText(state == Radio::State::Disconnected ? tr("Connect") : tr("Disconnect"));
    m_powerButton->setPowered(connected);
    m_powerButton->setEnabled(state != Radio::State::Connecting);
    m_powerButton->setToolTip(connected                                ? tr("Turn the radio off")
                              : state == Radio::State::PoweredOff      ? tr("Turn the radio on")
                              : state == Radio::State::Disconnected    ? tr("Turn the radio on and connect")
                                                                       : tr("Radio power"));
    m_portCombo->setEnabled(state == Radio::State::Disconnected);
    m_baudCombo->setEnabled(state == Radio::State::Disconnected);
    for (QWidget *widget : std::as_const(m_radioControls))
        widget->setEnabled(connected);
    m_rxEq->setConnected(connected);
    m_txEq->setConnected(connected);

    switch (state) {
    case Radio::State::Disconnected:
    case Radio::State::PoweredOff:
        m_radioInfo->setText(state == Radio::State::PoweredOff ? tr("Radio is switched off") : tr("Disconnected"));
        m_sMeter->reset();
        m_txIndicator->setEnabled(false);
        onVfoSelectionChanged(Vfo::A, Vfo::A);
        refreshRitXit();
        refreshNotch();
        refreshNoise();
        refreshMonitor();
        break;
    case Radio::State::Connecting: m_radioInfo->setText(tr("Connecting…")); break;
    case Radio::State::Connected:
        onModeChanged(m_radio.mode(), m_radio.dataMode());
        onVfoSelectionChanged(m_radio.rxVfo(), m_radio.txVfo());
        refreshRitXit();
        refreshNotch();
        refreshNoise();
        refreshMonitor();
        break;
    }
}

// ------------------------------------------------------------------- updates

void MainWindow::onModeChanged(Mode mode, bool data)
{
    {
        // AM tops out at 25 W. Changing the range may clamp the value; that
        // must not be sent back to the radio.
        m_applyingRadioState = true;
        m_txPowerSlider->setMaximum(txPowerMax(mode));
        if (const auto watts = m_radio.txPower())
            m_txPowerSlider->setValue(*watts);
        m_applyingRadioState = false;
    }
    if (QAbstractButton *button = m_modeButtons->button(int(mode))) {
        button->setChecked(true);
    } else if (QAbstractButton *checked = m_modeButtons->checkedButton()) {
        m_modeButtons->setExclusive(false);
        checked->setChecked(false);
        m_modeButtons->setExclusive(true);
    }
    m_dataCheck->setChecked(data);
    m_dataCheck->setEnabled(m_radio.state() == Radio::State::Connected && modeSupportsData(mode));
    m_rxEq->setEqMode(m_radio.eqMode());
    m_txEq->setEqMode(m_radio.eqMode());
    refreshNotch();
    refreshNoise();
}

void MainWindow::onVfoSelectionChanged(Vfo rx, Vfo tx)
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    auto role = [&](Vfo vfo) {
        if (!connected)
            return QString();
        if (rx == vfo && tx == vfo)
            return tr("RX/TX");
        return rx == vfo ? tr("RX") : tx == vfo ? tr("TX") : QString();
    };
    m_vfoA->setRole(role(Vfo::A));
    m_vfoB->setRole(role(Vfo::B));

    if (QAbstractButton *button = connected ? m_vfoButtons->button(int(rx)) : nullptr) {
        button->setChecked(true);
    } else if (QAbstractButton *checked = m_vfoButtons->checkedButton()) {
        // Memory mode or disconnected: neither VFO is selected.
        m_vfoButtons->setExclusive(false);
        checked->setChecked(false);
        m_vfoButtons->setExclusive(true);
    }
    m_splitButton->setChecked(connected && rx != tx && rx != Vfo::Memory);
    m_vfoStatus->setText(connected && rx == Vfo::Memory ? tr("Memory channel") : QString());
}

void MainWindow::refreshFilters()
{
    const FilterSlot slot = m_radio.filterSlot();
    m_filterA->setChecked(slot == FilterSlot::A);
    m_filterB->setChecked(slot == FilterSlot::B);

    m_filterLayout = m_radio.filterLayout();
    const FilterControl *controls[2] = {&m_filterLayout.first, &m_filterLayout.second};
    for (int i = 0; i < 2; ++i) {
        const FilterControl &control = *controls[i];
        QComboBox *combo = m_filterCombos[i];
        const bool present = control.command != FilterCommand::None;
        m_filterLabels[i]->setVisible(present);
        combo->setVisible(present);
        if (!present)
            continue;
        m_filterLabels[i]->setText(control.label);

        QStringList items;
        for (int hz : control.valuesHz)
            items << tr("%1 Hz").arg(hz);
        const QSignalBlocker blocker(combo);
        if (combo->count() != items.size() || combo->itemText(0) != items.value(0)
            || combo->itemText(combo->count() - 1) != items.constLast()) {
            combo->clear();
            combo->addItems(items);
        }
        const auto raw = m_radio.filterValue(control.command);
        combo->setCurrentIndex(raw ? filterIndexForRaw(control, *raw) : -1);
    }
}

void MainWindow::refreshEq(EqType type)
{
    EqualizerPanel *panel = type == EqType::Rx ? m_rxEq : m_txEq;
    panel->setEqMode(m_radio.eqMode());
    panel->setCurve(m_radio.eqCurve(type));
    panel->setLevels(m_radio.eqLevels(type));
}

void MainWindow::refreshRitXit()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    m_ritButton->setChecked(connected && m_radio.rit());
    m_xitButton->setChecked(connected && m_radio.xit());
    // As on the radio, the offset only applies while RIT or XIT is on.
    const bool adjustable = connected && (m_radio.rit() || m_radio.xit());
    m_offsetSpin->setEnabled(adjustable);
    m_clearOffsetButton->setEnabled(adjustable);
    if (m_offsetHold.isActive())
        return;   // refreshRitXit runs again when the hold expires
    m_applyingRadioState = true;
    m_offsetSpin->setValue(connected ? m_radio.ritXitOffset() : 0);
    m_applyingRadioState = false;
}

void MainWindow::refreshNotch()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    const NotchMode mode = connected ? m_radio.notchMode() : NotchMode::Off;
    m_autoNotchButton->setChecked(mode == NotchMode::Auto);
    m_manualNotchButton->setChecked(mode == NotchMode::Manual);
    const bool available = connected && notchSupported(m_radio.mode());
    m_autoNotchButton->setEnabled(available);
    m_manualNotchButton->setEnabled(available);
    // Bandwidth and position only apply to the manual notch.
    const bool manual = available && mode == NotchMode::Manual;
    m_notchWideCheck->setEnabled(manual);
    // Outside manual the radio always reports "normal"; keep the user's last
    // choice so re-enabling manual restores it.
    if (manual)
        m_notchWideCheck->setChecked(m_radio.notchWide());
    m_notchSlider->setEnabled(manual);
}

void MainWindow::refreshNoise()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    const Mode mode = m_radio.mode();
    m_applyingRadioState = true;

    const NoiseBlanker nb = connected ? m_radio.noiseBlanker() : NoiseBlanker::Off;
    m_nbButton->setText(noiseBlankerName(nb));
    m_nbButton->setChecked(nb != NoiseBlanker::Off);
    m_nbButton->setEnabled(connected && noiseBlankerSupported(mode));
    m_nbSlider->setEnabled(connected && noiseBlankerSupported(mode) && nbLevelAdjustable(nb));
    if (const auto level = m_radio.nbLevel(); level && !m_nbSlider->isSliderDown())
        m_nbSlider->setValue(*level);

    const NoiseReduction nr = connected ? m_radio.noiseReduction() : NoiseReduction::Off;
    m_nrButton->setText(noiseReductionName(nr));
    m_nrButton->setChecked(nr != NoiseReduction::Off);
    m_nrButton->setEnabled(connected);
    m_nrSlider->setEnabled(connected && nr != NoiseReduction::Off);
    const auto [low, high] = nrLevelRange(nr);
    m_nrSlider->setRange(low, high);
    if (const auto level = m_radio.nrLevel(); level && !m_nrSlider->isSliderDown())
        m_nrSlider->setValue(*level);
    // The unit depends on NR1 vs NR2, so refresh even if the value didn't move.
    m_nrValue->setText(nr == NoiseReduction::Off ? QString() : nrLevelText(nr, m_nrSlider->value()));

    m_applyingRadioState = false;
}

void MainWindow::refreshMonitor()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    const int level = connected ? m_radio.txMonitor().value_or(0) : 0;
    m_monitorButton->setChecked(level > 0);
    m_monitorButton->setEnabled(connected);
    m_monitorSlider->setEnabled(level > 0);
    // While off, the slider keeps the level to restore when it's turned on.
    if (level > 0 && !m_monitorSlider->isSliderDown()) {
        m_applyingRadioState = true;
        m_monitorSlider->setValue(level);
        m_applyingRadioState = false;
    }
}

void MainWindow::showMessage(const QString &text)
{
    statusBar()->showMessage(text, kMessageTimeoutMs);
}

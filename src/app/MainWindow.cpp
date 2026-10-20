#include "MainWindow.h"

#include "AppStyle.h"
#include "PowerButton.h"
#include "CatMonitor.h"
#include "EqualizerPanel.h"
#include "MenuPanel.h"
#include "FrequencyDisplay.h"
#include "SerialTransport.h"
#include "SMeter.h"
#include "SimulatorTransport.h"
#include "WheelSteps.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QMediaDevices>
#include <QPermissions>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimeZone>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ts590;

namespace {

const QString kSimulatorPort = QStringLiteral("__simulator__");
constexpr int kMessageTimeoutMs = 8000;
const QList<qint32> kBaudRates = {9600, 19200, 38400, 57600, 115200};

constexpr int kVfoAPointSize = 36;
constexpr int kCwTextPointSize = 14;   // keyboard CW entry and decoded text
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

// macOS defaults to right-aligned labels and fields kept at their size hint,
// which leaves slider rows ragged. Left-align labels and stretch fields so
// every slider in a form starts and ends at the same x.
void alignForm(QFormLayout *form)
{
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
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
    setWindowTitle(QStringLiteral("590Ctrl %1").arg(QCoreApplication::applicationVersion()));

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
    tabs->addTab(createBandsPage(), tr("Bands"));
    tabs->addTab(createCwPage(), tr("CW"));
    tabs->addTab(new MenuPanel(&m_radio), tr("Menu"));
    tabs->addTab(createQrzPage(), tr("QRZ"));

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(createVfoPanel());
    layout->addWidget(createModeBar());
    layout->addWidget(tabs, 1);
    setCentralWidget(central);

    // The macOS style's slider minimum is just the knob, so a narrow window
    // squeezed the sliders to nothing. Keep every slider usable.
    for (QSlider *slider : central->findChildren<QSlider *>()) {
        if (slider->orientation() == Qt::Horizontal)
            slider->setMinimumWidth(120);
    }

    auto *monitorDock = new QDockWidget(tr("CAT Monitor"));
    monitorDock->setObjectName(QStringLiteral("catMonitor"));
    auto *monitor = new CatMonitor(m_radio.link());
    monitor->addTciServer(&m_tci);
    monitorDock->setWidget(monitor);
    addDockWidget(Qt::BottomDockWidgetArea, monitorDock);
    monitorDock->hide();
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(monitorDock->toggleViewAction());

    // Widget style and light/dark appearance, applied live and remembered.
    QMenu *styleMenu = viewMenu->addMenu(tr("Style"));
    auto *styleGroup = new QActionGroup(this);
    const QString current = currentStyleName();
    for (const QString &name : availableStyles()) {
        QAction *action = styleMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name.compare(current, Qt::CaseInsensitive) == 0);
        styleGroup->addAction(action);
        connect(action, &QAction::triggered, this, [name] {
            applyStyle(name);
            saveStyle(name);
        });
    }
    QMenu *appearanceMenu = viewMenu->addMenu(tr("Appearance"));
    auto *appearanceGroup = new QActionGroup(this);
    for (auto [appearance, label] : {std::pair{Appearance::System, tr("System")},
                                     std::pair{Appearance::Light, tr("Light")},
                                     std::pair{Appearance::Dark, tr("Dark")}}) {
        QAction *action = appearanceMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(appearance == currentAppearance());
        appearanceGroup->addAction(action);
        connect(action, &QAction::triggered, this, [appearance] {
            applyAppearance(appearance);
            saveAppearance(appearance);
        });
    }

    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(tr("TCI Server…"), this, &MainWindow::editTciServer);

    // On macOS the About roles move these into the application menu.
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *aboutAction = helpMenu->addAction(tr("About 590Ctrl"), this, &MainWindow::showAbout);
    aboutAction->setMenuRole(QAction::AboutRole);
    QAction *aboutQtAction = helpMenu->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);
    aboutQtAction->setMenuRole(QAction::AboutQtRole);

    m_radioInfo = new QLabel;
    statusBar()->addPermanentWidget(m_radioInfo);
    m_tciStatus = new QLabel;
    statusBar()->addPermanentWidget(m_tciStatus);
    connect(&m_tci, &TciServer::clientsChanged, this, &MainWindow::refreshTciStatus);
    connect(&m_tci, &TciServer::audioError, this, &MainWindow::showMessage);
    startTciServer();

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
        m_decoderGroup->setVisible(model == kModelTs590SG);   // the S has no decoder
        m_radioInfo->setText(tr("%1 · firmware %2 · %3")
                                 .arg(name, firmware, m_radio.link()->transport()->description()));
    });
    connect(&m_radio, &Radio::frequencyChanged, this, [this](Vfo vfo, qint64 hz) {
        (vfo == Vfo::B ? m_vfoB : m_vfoA)->setFrequency(hz);
        refreshBand();
    });
    connect(&m_radio, &Radio::vfoSelectionChanged, this, &MainWindow::onVfoSelectionChanged);
    connect(&m_radio, &Radio::vfoSelectionChanged, this, &MainWindow::refreshBand);
    connect(&m_radio, &Radio::processorChanged, this, &MainWindow::refreshProcessor);
    connect(&m_radio, &Radio::modeChanged, this, &MainWindow::onModeChanged);
    connect(&m_radio, &Radio::sMeterChanged, m_sMeter, &SMeter::setValue);
    // Follows the slider, so the marker moves while dragging too.
    connect(m_rfSlider, &QSlider::valueChanged, m_sMeter, &SMeter::setRfGain);
    connect(&m_radio, &Radio::txMetersChanged, this, [this] {
        m_sMeter->setTxMeters(m_radio.txMeter(TxMeter::Swr), m_radio.txMeter(TxMeter::Comp),
                              m_radio.txMeter(TxMeter::Alc));
    });
    connect(&m_radio, &Radio::transmitChanged, this, [this](bool tx) {
        m_txIndicator->setEnabled(tx);
        m_sMeter->setTransmitting(tx);
        // Back on receive for any reason (TX released, time-out timer,
        // refused): SEND is no longer latched.
        if (!tx) {
            m_sendButton->setChecked(false);
            m_tuneCarrierButton->setChecked(false);
        }
    });
    connect(&m_radio, &Radio::voxChanged, m_voxButton, &QPushButton::setChecked);
    connect(&m_radio, &Radio::voxChanged, m_breakInButton, &QPushButton::setChecked);
    connect(&m_radio, &Radio::beatCancelChanged, this, &MainWindow::refreshBeatCancel);
    connect(&m_radio, &Radio::busyChanged, this, &MainWindow::refreshSquelchOpen);
    connect(m_sqlSlider, &QSlider::valueChanged, this, &MainWindow::refreshSquelchOpen);
    connect(&m_radio, &Radio::keyerChanged, this, &MainWindow::refreshKeyer);
    connect(&m_radio, &Radio::cwQueueChanged, this, &MainWindow::refreshKeyer);
    connect(&m_radio, &Radio::morseDecoderChanged, this, &MainWindow::refreshDecoder);
    connect(&m_radio, &Radio::morseDecoded, this, [this](const QString &text) {
        m_decoderText->moveCursor(QTextCursor::End);
        m_decoderText->insertPlainText(text);
        m_decoderText->ensureCursorVisible();
    });
    connect(&m_radio, &Radio::antennaChanged, this, &MainWindow::refreshAntenna);
    connect(&m_radio, &Radio::attenuatorChanged, this, &MainWindow::refreshFrontEnd);
    connect(&m_radio, &Radio::preampChanged, this, &MainWindow::refreshFrontEnd);
    connect(&m_radio, &Radio::tunerChanged, this, &MainWindow::refreshAntenna);
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
    connect(&m_radio, &Radio::voxGainChanged, this, follow(m_voxGainSlider));
    connect(&m_radio, &Radio::micGainChanged, this, follow(m_micGainSlider));
    connect(&m_radio, &Radio::txMonitorChanged, this, &MainWindow::refreshMonitor);
    connect(&m_radio, &Radio::tunePowerChanged, this, follow(m_tunePowerSlider));
    connect(&m_radio, &Radio::ritXitChanged, this, &MainWindow::refreshRitXit);
    connect(&m_radio, &Radio::notchChanged, this, &MainWindow::refreshNotch);
    connect(&m_radio, &Radio::noiseBlankerChanged, this, &MainWindow::refreshNoise);
    connect(&m_radio, &Radio::noiseReductionChanged, this, &MainWindow::refreshNoise);
    connect(&m_radio, &Radio::agcChanged, this, &MainWindow::refreshAgc);
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

    // ---- QRZ
    m_qrz.setAgent(QStringLiteral("590Ctrl/%1").arg(QCoreApplication::applicationVersion()));
    loadQrzAccount();
    connect(&m_qrz, &qrz::Client::lookupFinished, this, &MainWindow::showLookup);
    connect(&m_qrz, &qrz::Client::lookupFailed, this, [this](const QString &call, const QString &error) {
        m_qrzLookupButton->setEnabled(true);
        m_lastLookup.reset();
        m_qrzStatusText = call.isEmpty() ? error : tr("%1: %2").arg(call, error);
        m_qrzStatusError = true;
        refreshQrzStatus();
    });
    connect(&m_qrz, &qrz::Client::logChecked, this, [this](const QString &call, const qrz::FetchResult &result) {
        if (call != m_qrzCall->text().trimmed().toUpper())
            return;   // the box has moved on
        const auto last = qrz::latestQso(result.records);
        setCallWorked(last.has_value());
        if (!last) {
            m_qrzWorkedText = tr("Not in your QRZ log");
        } else {
            const QDateTime when(QDate::fromString(last->value(QStringLiteral("qso_date")), QStringLiteral("yyyyMMdd")),
                                 QTime::fromString(last->value(QStringLiteral("time_on")).left(4), QStringLiteral("HHmm")),
                                 QTimeZone::UTC);
            // FT4 reads better than its ADIF mode, MFSK; SSB better than USB.
            const QString mode = last->value(QStringLiteral("mode"));
            const QString submode = last->value(QStringLiteral("submode"));
            const bool useSubmode = !submode.isEmpty() && mode.compare(QLatin1String("SSB"), Qt::CaseInsensitive) != 0;
            QStringList parts = {tr("Last worked %1 UTC").arg(when.toString(QStringLiteral("yyyy-MM-dd HH:mm"))),
                                 QStringList{last->value(QStringLiteral("band")), useSubmode ? submode : mode}
                                     .join(QLatin1Char(' '))
                                     .trimmed()};
            const int count = std::max(result.count, int(result.records.size()));
            if (count > 1)
                parts << tr("%n QSOs in your log", nullptr, count);
            parts.removeAll(QString());
            m_qrzWorkedText = parts.join(QStringLiteral(" · "));
        }
        refreshQrzStatus();
    });
    connect(&m_qrz, &qrz::Client::logCheckFailed, this, [this](const QString &call, const QString &error) {
        if (call != m_qrzCall->text().trimmed().toUpper())
            return;
        m_qrzWorkedText = tr("Log check failed: %1").arg(error);
        refreshQrzStatus();
    });
    connect(&m_qrz, &qrz::Client::imageFetched, this, [this](const QUrl &url, const QByteArray &data) {
        QPixmap pixmap;
        if (url != m_qrzImageUrl || !pixmap.loadFromData(data))
            return;
        m_qrzImage->setPixmap(pixmap.scaled(m_qrzImage->maximumSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_qrzImage->show();
    });
    connect(&m_qrz, &qrz::Client::qsoInserted, this, [this](const qrz::LogResult &result) {
        m_qrzLogButton->setEnabled(true);
        setQrzStatus(result.logId.isEmpty() ? tr("Logged to QRZ.") : tr("Logged to QRZ (log ID %1).").arg(result.logId));
        setCallWorked(true);
    });
    connect(&m_qrz, &qrz::Client::qsoFailed, this, [this](const QString &reason) {
        m_qrzLogButton->setEnabled(true);
        setQrzStatus(tr("Not logged: %1").arg(reason), true);
    });
    // The QSO's mode and reports follow the radio; both stay editable.
    connect(&m_radio, &Radio::modeChanged, this, [this](Mode mode, bool data) {
        const qrz::AdifMode adif = qrz::adifModeFor(mode, data);
        const QString adifMode = adif.mode;
        if (adifMode.isEmpty())
            return;
        // PSK31 is in the list by its submode; SSB's submode is just the sideband.
        m_qrzMode->setCurrentText(adifMode == QLatin1String("PSK") ? adif.submode : adifMode);
        m_qrzRstSent->setText(qrz::defaultReport(adifMode));
        m_qrzRstRcvd->setText(qrz::defaultReport(adifMode));
    });

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    onStateChanged(Radio::State::Disconnected);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_tci.close();   // releases transmit if a TCI client keyed it
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

    // Antenna selection (AN): ANT1/ANT2, the separate RX antenna input, and
    // the DRV output (antenna out on an SG whose menu 085 is set to ANT).
    m_antennaButton = new QPushButton(QStringLiteral("ANT1"));
    m_antennaButton->setToolTip(tr("Switch between ANT1 and ANT2"));
    connect(m_antennaButton, &QPushButton::clicked, this,
            [this] { m_radio.setAntenna(m_radio.antenna() == 1 ? 2 : 1); });
    m_rxAntennaButton = new QPushButton(tr("RX ANT"));
    m_rxAntennaButton->setCheckable(true);
    m_rxAntennaButton->setToolTip(tr("Receive on the RX ANT input"));
    connect(m_rxAntennaButton, &QPushButton::clicked, &m_radio, &Radio::setRxAntenna);
    m_driveOutButton = new QPushButton(tr("DRV"));
    m_driveOutButton->setCheckable(true);
    m_driveOutButton->setToolTip(tr("Drive output (or antenna output, per menu 085)"));
    connect(m_driveOutButton, &QPushButton::clicked, &m_radio, &Radio::setDriveOut);
    m_tunerButton = new QPushButton(tr("AT"));
    m_tunerButton->setCheckable(true);
    m_tunerButton->setToolTip(tr("Internal antenna tuner in line (does not start a tune)"));
    connect(m_tunerButton, &QPushButton::clicked, &m_radio, &Radio::setTuner);
    m_tuneCycleButton = new QPushButton(tr("ATun"));
    m_tuneCycleButton->setCheckable(true);
    m_tuneCycleButton->setToolTip(tr("Run an antenna tuner cycle (transmits while tuning; click again to stop)"));
    connect(m_tuneCycleButton, &QPushButton::clicked, this, [this](bool on) {
        m_radio.setTunerTuning(on);
        refreshAntenna();   // lit only once the radio reports it's tuning
    });

    auto *antennaRow = new QHBoxLayout;
    for (QPushButton *button :
         {m_antennaButton, m_rxAntennaButton, m_driveOutButton, m_tunerButton, m_tuneCycleButton}) {
        antennaRow->addWidget(button);
        m_radioControls << button;
    }
    antennaRow->addStretch();

    // Meter level with the top of the VFO box, antenna controls under it.
    auto *right = new QVBoxLayout;
    right->setContentsMargins(0, 2, 0, 0);
    right->addWidget(m_sMeter);
    right->addLayout(antennaRow);
    right->addStretch();

    auto *panel = new QWidget;
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(vfoBox, 3);
    layout->addLayout(right, 2);
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
    alignForm(levelsLayout);
    // One width for every value readout, so the sliders end together too.
    const int valueWidth = levels->fontMetrics().horizontalAdvance(QStringLiteral("20 ms")) + 6;
    auto addLevel = [levelsLayout, valueWidth](const QString &name, QSlider *slider) {
        auto *value = new QLabel(QStringLiteral("0"));
        value->setFixedWidth(valueWidth);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(slider, &QSlider::valueChanged, value, qOverload<int>(&QLabel::setNum));
        auto *row = new QHBoxLayout;
        row->addWidget(slider, 1);
        row->addWidget(value);
        levelsLayout->addRow(name, row);
    };
    addLevel(tr("AF gain"), m_afSlider);
    addLevel(tr("Squelch"), m_sqlSlider);
    {
        // RF gain row, with the attenuator as a small button in front.
        m_attenuatorButton = new QPushButton(tr("ATT"));
        m_attenuatorButton->setCheckable(true);
        m_attenuatorButton->setToolTip(tr("Click to cycle: attenuator → preamp → off"));
        const QFontMetrics metrics = m_attenuatorButton->fontMetrics();
        m_attenuatorButton->setFixedWidth(
            std::max(metrics.horizontalAdvance(tr("ATT")), metrics.horizontalAdvance(tr("PRE"))) + 20);
        connect(m_attenuatorButton, &QPushButton::clicked, this, [this] {
            const bool att = m_radio.attenuator(), pre = m_radio.preamp();
            if (!att && !pre) {
                m_radio.setAttenuator(true);
            } else if (att && !pre) {
                m_radio.setAttenuator(false);
                m_radio.setPreamp(true);
            } else {   // PRE, or both on (set from the panel): all off
                m_radio.setAttenuator(false);
                m_radio.setPreamp(false);
            }
            refreshFrontEnd();   // undo the button's own toggle until the radio answers
        });
        m_radioControls << m_attenuatorButton;
        auto *value = new QLabel(QStringLiteral("0"));
        value->setFixedWidth(valueWidth);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(m_rfSlider, &QSlider::valueChanged, value, qOverload<int>(&QLabel::setNum));
        auto *row = new QHBoxLayout;
        row->addWidget(m_rfSlider, 1);
        row->addWidget(value);
        // ATT sits in the label column so this slider lines up with the rest.
        auto *label = new QWidget;
        auto *labelLayout = new QHBoxLayout(label);
        labelLayout->setContentsMargins(0, 0, 0, 0);
        labelLayout->addWidget(new QLabel(tr("RF gain")));
        labelLayout->addWidget(m_attenuatorButton);
        labelLayout->addStretch();
        levelsLayout->addRow(label, row);
    }
    addLevel(tr("USB in"), m_usbInSlider);
    addLevel(tr("USB out"), m_usbOutSlider);
    m_micGainSlider = createLevelSlider(&Radio::setMicGain, kMicGainMax);
    m_micGainSlider->setToolTip(tr("Microphone gain for SSB and AM (FM mic gain is a menu setting)"));
    addLevel(tr("Mic gain"), m_micGainSlider);

    // NB / NR: the button steps through the variants like the front-panel
    // key; the slider sets the level of whichever variant is on.
    auto createNoiseRow = [this, levelsLayout, valueWidth](QPushButton *&button, QSlider *&slider, QLabel *value) {
        button = new QPushButton;
        button->setCheckable(true);
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(QStringLiteral("NB1+2")) + 24);
        slider = new QSlider(Qt::Horizontal);
        slider->setPageStep(1);
        slider->setTickPosition(QSlider::TicksBelow);
        slider->setTickInterval(1);
        value->setFixedWidth(valueWidth);
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

    // AGC: OFF -> SLOW -> FAST -> OFF. Slow and fast each keep their own time
    // constant, so the slider shows whichever is active.
    m_agcValue = new QLabel;
    createNoiseRow(m_agcButton, m_agcSlider, m_agcValue);
    m_agcButton->setToolTip(tr("AGC: OFF → SLOW → FAST (not available in FM)"));
    m_agcSlider->setRange(kAgcTimeMin, kAgcTimeMax);
    m_agcSlider->setToolTip(tr("AGC time constant for the active mode (slow or fast)"));
    connect(m_agcSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_radio.agc() != Agc::Off)
            m_agcValue->setNum(value);
        if (!m_applyingRadioState)
            m_radio.setAgcTimeConstant(value);
    });
    connect(m_agcButton, &QPushButton::clicked, this, [this] {
        static constexpr Agc next[] = {Agc::Slow, Agc::Fast, Agc::Off};
        m_radio.setAgc(next[int(m_radio.agc())]);
        refreshAgc();   // undo the button's own toggle until the radio answers
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
    m_beatCancelButton = new QPushButton(tr("BC"));
    m_beatCancelButton->setCheckable(true);
    m_beatCancelButton->setToolTip(tr("Beat cancel: OFF → BC1 → BC2 → OFF. BC1 removes a steady beat, "
                                      "BC2 several or keyed ones. Not in CW or FSK."));
    connect(m_beatCancelButton, &QPushButton::clicked, this, [this] {
        static constexpr BeatCancel next[] = {BeatCancel::BC1, BeatCancel::BC2, BeatCancel::Off};
        m_radio.setBeatCancel(next[int(m_radio.beatCancel())]);
        refreshBeatCancel();   // undo the button's own toggle until the radio answers
    });
    m_radioControls << m_beatCancelButton;

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

    auto *group = new QGroupBox(tr("Beat Cancel / Notch"));
    auto *layout = new QGridLayout(group);
    layout->addWidget(m_beatCancelButton, 0, 0);
    layout->addWidget(m_autoNotchButton, 0, 1);
    layout->addWidget(m_manualNotchButton, 0, 2);
    layout->addWidget(m_notchWideCheck, 0, 3);
    auto *row = new QHBoxLayout;
    row->addWidget(m_notchSlider, 1);
    row->addWidget(value);
    layout->addLayout(row, 1, 0, 1, 4);
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
    alignForm(m_transmitForm);
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

    m_voxGainSlider = createLevelSlider(&Radio::setVoxGain, kVoxGainMax);
    m_voxGainSlider->setToolTip(tr("VOX gain: how loud you must speak to key up"));
    auto *voxGainValue = new QLabel(QStringLiteral("0"));
    voxGainValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    voxGainValue->setMinimumWidth(carrierValue->minimumWidth());
    connect(m_voxGainSlider, &QSlider::valueChanged, voxGainValue, qOverload<int>(&QLabel::setNum));
    auto *voxGainRow = new QHBoxLayout;
    voxGainRow->addWidget(m_voxGainSlider, 1);
    voxGainRow->addWidget(voxGainValue);
    m_transmitForm->addRow(tr("VOX level"), voxGainRow);

    m_processorButton = new QPushButton(tr("PROC"));
    m_processorButton->setCheckable(true);
    m_processorButton->setToolTip(tr("Speech processor: click to turn on, again for the IN level, "
                                     "again for the OUT level, again for off"));
    m_processorButton->setMinimumWidth(m_processorButton->fontMetrics().horizontalAdvance(tr("PROC OUT")) + 24);
    connect(m_processorButton, &QPushButton::clicked, this, [this] {
        // Only the first and last clicks reach the radio; IN and OUT just
        // choose which level the slider edits.
        if (!m_radio.processor()) {
            m_processorTarget = ProcessorTarget::None;
            m_radio.setProcessor(true);
        } else if (m_processorTarget == ProcessorTarget::None) {
            m_processorTarget = ProcessorTarget::Input;
        } else if (m_processorTarget == ProcessorTarget::Input) {
            m_processorTarget = ProcessorTarget::Output;
        } else {
            m_processorTarget = ProcessorTarget::None;
            m_radio.setProcessor(false);
        }
        refreshProcessor();
    });
    // PL carries both levels, so the slider writes whichever is selected and
    // sends the other one unchanged.
    m_processorSlider = new QSlider(Qt::Horizontal);
    m_processorSlider->setRange(0, kProcessorLevelMax);
    m_processorSlider->setPageStep(10);
    m_processorValue = new QLabel;
    m_processorValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_processorValue->setMinimumWidth(carrierValue->minimumWidth());
    connect(m_processorSlider, &QSlider::valueChanged, this, [this](int level) {
        m_processorValue->setNum(level);
        if (m_applyingRadioState || !m_radio.processor() || m_processorTarget == ProcessorTarget::None)
            return;
        const int input = m_radio.processorInput().value_or(0);
        const int output = m_radio.processorOutput().value_or(0);
        if (m_processorTarget == ProcessorTarget::Input)
            m_radio.setProcessorLevels(level, output);
        else
            m_radio.setProcessorLevels(input, level);
    });
    auto *processorRow = new QHBoxLayout;
    processorRow->addWidget(m_processorSlider, 1);
    processorRow->addWidget(m_processorValue);
    m_transmitForm->addRow(m_processorButton, processorRow);
    m_radioControls << m_processorButton;

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
    connect(m_sendButton, &QPushButton::clicked, this, [this](bool on) {
        m_tuneCarrierButton->setChecked(false);   // one kind of transmit at a time
        m_radio.setTransmit(on);
    });

    // TUN latches like SEND, but keys TX Tune: a carrier at Tune power.
    m_tuneCarrierButton = new QPushButton(tr("TUN"));
    m_tuneCarrierButton->setObjectName(QStringLiteral("tuneCarrierButton"));
    m_tuneCarrierButton->setCheckable(true);
    m_tuneCarrierButton->setToolTip(tr("Transmit a tune carrier at the Tune power level until clicked again"));
    m_tuneCarrierButton->setStyleSheet(m_sendButton->styleSheet().replace(QLatin1String("#sendButton"),
                                                                          QLatin1String("#tuneCarrierButton")));
    connect(m_tuneCarrierButton, &QPushButton::clicked, this, [this](bool on) {
        m_sendButton->setChecked(false);
        m_radio.setTuneCarrier(on);
    });

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
    m_radioControls << m_voxButton << m_sendButton << m_tuneCarrierButton << m_txButton;

    auto *keying = new QGridLayout;
    keying->addWidget(m_voxButton, 0, 0);
    keying->addWidget(m_sendButton, 0, 1);
    keying->addWidget(m_tuneCarrierButton, 0, 2);
    keying->addWidget(m_txButton, 1, 0, 1, 3);
    m_transmitForm->addRow(keying);
    return group;
}

QWidget *MainWindow::createBandsPage()
{
    m_bandButtons = new QButtonGroup(this);
    m_bandButtons->setExclusive(true);

    auto *grid = new QGridLayout;
    grid->setSpacing(8);
    constexpr int kColumns = 6;
    for (int band = 0; band < kBandCount; ++band) {
        const QString wavelength = bandWavelength(band);
        auto *button = new QPushButton(wavelength.isEmpty() ? bandLabel(band)
                                                            : QStringLiteral("%1\n%2").arg(bandLabel(band), wavelength));
        button->setCheckable(true);
        button->setMinimumSize(96, 64);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QFont font = button->font();
        font.setPointSizeF(font.pointSizeF() * 1.25);
        font.setBold(true);
        button->setFont(font);
        button->setToolTip(band == kGeneralBand ? tr("General coverage")
                                                : tr("%1 MHz band (%2)").arg(bandLabel(band), wavelength));
        m_bandButtons->addButton(button, band);
        grid->addWidget(button, band / kColumns, band % kColumns);
        m_radioControls << button;
    }
    connect(m_bandButtons, &QButtonGroup::idClicked, this, [this](int band) {
        m_radio.selectBand(band);
        refreshBand();   // the highlight follows the radio's frequency, not the click
    });

    auto *hint = new QLabel(tr("Click the current band again to step through its band memories."));
    hint->setForegroundRole(QPalette::PlaceholderText);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addLayout(grid);
    layout->addWidget(hint);
    layout->addStretch();
    return page;
}

QWidget *MainWindow::createCwPage()
{
    // Each slider gets a readout at its right, all the same width.
    const int valueWidth = fontMetrics().horizontalAdvance(QStringLiteral("1000 ms")) + 6;
    auto sliderRow = [valueWidth](QSlider *slider, QLabel *value) {
        value->setFixedWidth(valueWidth);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *row = new QHBoxLayout;
        row->addWidget(slider, 1);
        row->addWidget(value);
        return row;
    };

    m_keyerSpeedSlider = createLevelSlider(&Radio::setKeyerSpeed, kKeyerSpeedMax);
    m_keyerSpeedSlider->setMinimum(kKeyerSpeedMin);
    m_keyerSpeedSlider->setPageStep(5);
    m_keyerSpeedSlider->setToolTip(tr("Electronic keyer speed (also used for keyboard CW)"));
    auto *speedValue = new QLabel;
    connect(m_keyerSpeedSlider, &QSlider::valueChanged, speedValue,
            [speedValue](int wpm) { speedValue->setText(tr("%1 WPM").arg(wpm)); });

    m_pitchSlider = createLevelSlider(&Radio::setCwPitch, kCwPitchIndexMax);
    m_pitchSlider->setToolTip(tr("CW pitch and sidetone frequency (a menu setting)"));
    auto *pitchValue = new QLabel;
    connect(m_pitchSlider, &QSlider::valueChanged, pitchValue,
            [pitchValue](int index) { pitchValue->setText(tr("%1 Hz").arg(cwPitchHz(index))); });

    m_breakInButton = new QPushButton(tr("BREAK-IN"));
    m_breakInButton->setCheckable(true);
    m_breakInButton->setToolTip(tr("Break-in: keying transmits (the VOX key in CW)"));
    connect(m_breakInButton, &QPushButton::clicked, &m_radio, &Radio::setVox);
    m_radioControls << m_breakInButton;
    m_breakInDelaySlider = new QSlider(Qt::Horizontal);
    m_breakInDelaySlider->setRange(0, kBreakInDelayMax / kBreakInDelayStep);
    m_breakInDelaySlider->setPageStep(2);
    m_breakInDelaySlider->setToolTip(tr("Break-in delay: how long the transmitter stays keyed after the "
                                        "last element. FULL is full break-in (QSK)."));
    connect(m_breakInDelaySlider, &QSlider::valueChanged, this, [this](int steps) {
        if (!m_applyingRadioState)
            m_radio.setBreakInDelay(steps * kBreakInDelayStep);
    });
    m_radioControls << m_breakInDelaySlider;
    auto *delayValue = new QLabel;
    connect(m_breakInDelaySlider, &QSlider::valueChanged, delayValue, [delayValue](int steps) {
        delayValue->setText(steps == 0 ? tr("FULL") : tr("%1 ms").arg(steps * kBreakInDelayStep));
    });

    m_cwTuneButton = new QPushButton(tr("CW TUNE"));
    m_cwTuneButton->setCheckable(true);
    m_cwTuneButton->setToolTip(tr("Zero in on the CW signal being received (CW mode only)"));
    connect(m_cwTuneButton, &QPushButton::clicked, this, [this](bool on) {
        m_radio.setCwTune(on);
        refreshKeyer();   // lit only once the radio reports it's tuning
    });

    auto *keyer = new QGroupBox(tr("Keyer"));
    auto *keyerForm = new QFormLayout(keyer);
    alignForm(keyerForm);
    keyerForm->addRow(tr("Speed"), sliderRow(m_keyerSpeedSlider, speedValue));
    keyerForm->addRow(tr("Pitch"), sliderRow(m_pitchSlider, pitchValue));
    keyerForm->addRow(m_breakInButton, sliderRow(m_breakInDelaySlider, delayValue));
    auto *tuneRow = new QHBoxLayout;
    tuneRow->addWidget(m_cwTuneButton);
    tuneRow->addStretch();
    keyerForm->addRow(tuneRow);

    // Keyboard CW: Enter (or SEND) queues the line; the radio keys it at the
    // keyer speed, transmitting if break-in is on.
    m_cwEdit = new QLineEdit;
    // Same face and size as the decoded text below.
    QFont cwFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    cwFont.setPointSize(kCwTextPointSize);
    m_cwEdit->setFont(cwFont);
    m_cwEdit->setTextMargins(4, 3, 4, 3);
    m_cwEdit->setPlaceholderText(tr("Type text and press Enter to send it"));
    m_cwEdit->setToolTip(tr("Letters, digits and . , / ? = : ' \" ( ) * + - @. "
                            "Prosigns: [ BT, ] KN, < AS, > SK, _ AR, \\ BK, # HH, % SN."));
    m_radioControls << m_cwEdit;
    m_cwSendButton = new QPushButton(tr("SEND"));
    m_cwSendButton->setToolTip(tr("Key the text (CW mode only)"));
    auto send = [this] {
        if (m_cwEdit->text().trimmed().isEmpty() || !m_cwSendButton->isEnabled())
            return;
        m_radio.sendCw(m_cwEdit->text());
        m_cwEdit->clear();
    };
    connect(m_cwSendButton, &QPushButton::clicked, this, send);
    connect(m_cwEdit, &QLineEdit::returnPressed, this, send);
    m_cwStopButton = new QPushButton(tr("STOP"));
    m_cwStopButton->setToolTip(tr("Stop sending and drop any text not yet keyed"));
    connect(m_cwStopButton, &QPushButton::clicked, &m_radio, &Radio::stopCw);
    m_cwQueueLabel = new QLabel;
    m_cwQueueLabel->setForegroundRole(QPalette::PlaceholderText);

    auto *sendGroup = new QGroupBox(tr("Keyboard CW"));
    auto *sendLayout = new QGridLayout(sendGroup);
    sendLayout->addWidget(m_cwEdit, 0, 0, 1, 3);
    sendLayout->addWidget(m_cwQueueLabel, 1, 0);
    sendLayout->addWidget(m_cwSendButton, 1, 1);
    sendLayout->addWidget(m_cwStopButton, 1, 2);
    sendLayout->setColumnStretch(0, 1);

    // Morse decoder (TS-590SG): the radio streams what it decodes.
    m_decoderButton = new QPushButton(tr("DECODE"));
    m_decoderButton->setCheckable(true);
    m_decoderButton->setToolTip(tr("The radio's Morse decoder (CW mode)"));
    connect(m_decoderButton, &QPushButton::clicked, this, [this](bool on) {
        m_radio.setMorseDecoder(on);
        refreshDecoder();
    });
    m_radioControls << m_decoderButton;
    m_decoderSlider = createLevelSlider(&Radio::setDecoderThreshold, kDecoderThresholdMax);
    m_decoderSlider->setMinimum(kDecoderThresholdMin);
    m_decoderSlider->setPageStep(3);
    m_decoderSlider->setToolTip(tr("Decoder threshold: raise it if noise decodes as characters"));
    auto *thresholdValue = new QLabel;
    connect(m_decoderSlider, &QSlider::valueChanged, thresholdValue, qOverload<int>(&QLabel::setNum));
    m_decoderText = new QPlainTextEdit;
    m_decoderText->setReadOnly(true);
    QFont decoderFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    decoderFont.setPointSize(kCwTextPointSize);
    m_decoderText->setFont(decoderFont);
    m_decoderText->setMaximumBlockCount(500);
    m_decoderText->setPlaceholderText(tr("Decoded text appears here"));
    auto *clearButton = new QPushButton(tr("Clear"));
    connect(clearButton, &QPushButton::clicked, m_decoderText, &QPlainTextEdit::clear);

    m_decoderGroup = new QGroupBox(tr("Morse Decoder"));
    auto *decoderLayout = new QVBoxLayout(m_decoderGroup);
    auto *decoderRow = new QHBoxLayout;
    decoderRow->addWidget(m_decoderButton);
    decoderRow->addWidget(new QLabel(tr("Threshold")));
    decoderRow->addLayout(sliderRow(m_decoderSlider, thresholdValue), 1);
    decoderRow->addWidget(clearButton);
    decoderLayout->addLayout(decoderRow);
    decoderLayout->addWidget(m_decoderText, 1);

    auto *left = new QVBoxLayout;
    left->addWidget(keyer);
    left->addWidget(sendGroup);
    left->addStretch();

    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->addLayout(left, 1);
    layout->addWidget(m_decoderGroup, 1);
    return page;
}

QWidget *MainWindow::createQrzPage()
{
    m_qrzCall = new QLineEdit;
    m_qrzCall->setPlaceholderText(tr("Callsign"));
    m_qrzCall->setMaxLength(20);
    m_qrzCall->setClearButtonEnabled(true);
    QFont callFont = m_qrzCall->font();
    callFont.setPointSizeF(callFont.pointSizeF() * 1.4);
    callFont.setBold(true);
    m_qrzCall->setFont(callFont);
    const int callWidth = m_qrzCall->fontMetrics().horizontalAdvance(QStringLiteral("WW/WW0WWW/WW")) + 40;
    m_qrzCall->setMinimumWidth(callWidth);
    m_qrzCall->setMaximumWidth(callWidth * 3 / 2);
    connect(m_qrzCall, &QLineEdit::textEdited, this, [this](const QString &text) {
        // Calls are upper case; keep the cursor where it was.
        const int cursor = m_qrzCall->cursorPosition();
        m_qrzCall->setText(text.toUpper());
        m_qrzCall->setCursorPosition(cursor);
        setCallWorked(false);   // until this call is checked
    });
    connect(m_qrzCall, &QLineEdit::returnPressed, this, &MainWindow::lookUpCall);

    m_qrzLookupButton = new QPushButton(tr("Look Up"));
    m_qrzLookupButton->setToolTip(tr("Look up the callsign on QRZ.com"));
    connect(m_qrzLookupButton, &QPushButton::clicked, this, &MainWindow::lookUpCall);
    m_qrzLogButton = new QPushButton(tr("Log Contact"));
    m_qrzLogButton->setToolTip(tr("Add a QSO with this callsign to your QRZ Logbook, using the radio's frequency "
                                  "and mode and the current UTC time"));
    connect(m_qrzLogButton, &QPushButton::clicked, this, &MainWindow::logContact);
    auto *accountButton = new QPushButton(tr("Account…"));
    accountButton->setToolTip(tr("QRZ username, password, station callsign and Logbook API key"));
    connect(accountButton, &QPushButton::clicked, this, &MainWindow::editQrzAccount);

    auto *lookupRow = new QHBoxLayout;
    lookupRow->addWidget(m_qrzCall);
    lookupRow->addWidget(m_qrzLookupButton);
    lookupRow->addWidget(m_qrzLogButton);
    lookupRow->addStretch();
    lookupRow->addWidget(accountButton);

    // QSO details sent with Log Contact, besides frequency, mode and time.
    m_qrzMode = new QComboBox;
    m_qrzMode->setEditable(true);
    m_qrzMode->addItems(qrz::modeNames());
    m_qrzMode->setToolTip(tr("ADIF mode to log. Follows the radio; DATA modes are a guess (PSK31 on SSB-DATA), "
                             "so pick the one you're using."));
    m_qrzRstSent = new QLineEdit(QStringLiteral("59"));
    m_qrzRstRcvd = new QLineEdit(QStringLiteral("59"));
    for (QLineEdit *rst : {m_qrzRstSent, m_qrzRstRcvd}) {
        rst->setMaxLength(6);
        rst->setFixedWidth(rst->fontMetrics().horizontalAdvance(QStringLiteral("-22 dB")) + 16);
    }
    m_qrzComment = new QLineEdit;
    m_qrzComment->setPlaceholderText(tr("Comment (optional)"));
    auto *qsoRow = new QHBoxLayout;
    qsoRow->addWidget(new QLabel(tr("Mode")));
    qsoRow->addWidget(m_qrzMode);
    qsoRow->addSpacing(8);
    qsoRow->addWidget(new QLabel(tr("RST sent")));
    qsoRow->addWidget(m_qrzRstSent);
    qsoRow->addWidget(new QLabel(tr("rcvd")));
    qsoRow->addWidget(m_qrzRstRcvd);
    qsoRow->addSpacing(8);
    qsoRow->addWidget(m_qrzComment, 1);

    m_qrzStatus = new QLabel;
    m_qrzStatus->setWordWrap(true);
    m_qrzStatus->setTextFormat(Qt::RichText);
    m_qrzStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_qrzTable = new QTableWidget(0, 2);
    m_qrzTable->horizontalHeader()->hide();
    m_qrzTable->verticalHeader()->hide();
    m_qrzTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_qrzTable->horizontalHeader()->setStretchLastSection(true);
    m_qrzTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_qrzTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_qrzTable->setAlternatingRowColors(true);
    m_qrzTable->setShowGrid(false);
    m_qrzTable->setWordWrap(true);
    m_qrzTable->setToolTip(tr("Double-click the callsign, web page, email or image to open it"));
    connect(m_qrzTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QString field = m_qrzTable->item(row, 0)->data(Qt::UserRole).toString();
        const QString value = m_qrzTable->item(row, 1)->text();
        if (field == QLatin1String("call"))
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.qrz.com/db/") + value));
        else if (field == QLatin1String("email"))
            QDesktopServices::openUrl(QUrl(QStringLiteral("mailto:") + value));
        else if (field == QLatin1String("url") || field == QLatin1String("image"))
            QDesktopServices::openUrl(QUrl::fromUserInput(value));
    });

    m_qrzImage = new QLabel;
    m_qrzImage->setMaximumSize(320, 320);
    m_qrzImage->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_qrzImage->hide();

    auto *results = new QHBoxLayout;
    results->addWidget(m_qrzTable, 1);
    results->addWidget(m_qrzImage, 0, Qt::AlignTop);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addLayout(lookupRow);
    layout->addLayout(qsoRow);
    layout->addWidget(m_qrzStatus);
    layout->addLayout(results, 1);
    return page;
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
        refreshAgc();
        refreshMonitor();
        refreshAntenna();
        refreshFrontEnd();
        refreshBand();
        refreshProcessor();
        refreshBeatCancel();
        refreshSquelchOpen();
        refreshKeyer();
        refreshDecoder();
        break;
    case Radio::State::Connecting: m_radioInfo->setText(tr("Connecting…")); break;
    case Radio::State::Connected:
        // The slider may already hold this value, so it won't signal a change.
        m_sMeter->setRfGain(m_rfSlider->value());
        onModeChanged(m_radio.mode(), m_radio.dataMode());
        onVfoSelectionChanged(m_radio.rxVfo(), m_radio.txVfo());
        refreshRitXit();
        refreshNotch();
        refreshNoise();
        refreshAgc();
        refreshMonitor();
        refreshAntenna();
        refreshFrontEnd();
        refreshBand();
        refreshProcessor();
        refreshBeatCancel();
        refreshSquelchOpen();
        refreshKeyer();
        refreshDecoder();
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
    refreshAgc();
    refreshBeatCancel();
    refreshKeyer();
    refreshDecoder();
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

void MainWindow::refreshAntenna()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    m_antennaButton->setText(connected && m_radio.antenna() == 2 ? QStringLiteral("ANT2") : QStringLiteral("ANT1"));
    m_rxAntennaButton->setChecked(connected && m_radio.rxAntenna());
    m_driveOutButton->setChecked(connected && m_radio.driveOut());
    m_tunerButton->setChecked(connected && m_radio.tuner());
    m_tuneCycleButton->setChecked(connected && m_radio.tunerTuning());
}

void MainWindow::refreshFrontEnd()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    const bool att = connected && m_radio.attenuator();
    const bool pre = connected && m_radio.preamp();
    m_attenuatorButton->setText(pre && !att ? tr("PRE") : tr("ATT"));
    m_attenuatorButton->setChecked(att || pre);
}

void MainWindow::refreshBand()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    const Vfo rx = m_radio.rxVfo();
    QAbstractButton *current = connected && rx != Vfo::Memory
                                   ? m_bandButtons->button(bandForFrequency(m_radio.frequency(rx)))
                                   : nullptr;
    if (current) {
        current->setChecked(true);
    } else if (QAbstractButton *checked = m_bandButtons->checkedButton()) {
        m_bandButtons->setExclusive(false);
        checked->setChecked(false);
        m_bandButtons->setExclusive(true);
    }
}

void MainWindow::refreshProcessor()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    const bool on = connected && m_radio.processor();
    if (!on)
        m_processorTarget = ProcessorTarget::None;
    const ProcessorTarget target = m_processorTarget;
    const bool output = target == ProcessorTarget::Output;
    m_processorButton->setText(target == ProcessorTarget::Input ? tr("PROC IN")
                               : output                         ? tr("PROC OUT")
                                                                : tr("PROC"));
    m_processorButton->setChecked(on);
    // Plain "on" selects no level, so there's nothing for the slider to edit.
    m_processorSlider->setEnabled(on && target != ProcessorTarget::None);
    m_processorSlider->setToolTip(output ? tr("Speech processor output level") : tr("Speech processor input level"));
    if (target == ProcessorTarget::None) {
        m_processorValue->clear();
        return;
    }
    const auto level = output ? m_radio.processorOutput() : m_radio.processorInput();
    if (level && !m_processorSlider->isSliderDown()) {
        m_applyingRadioState = true;
        m_processorSlider->setValue(*level);
        m_applyingRadioState = false;
        m_processorValue->setNum(*level);
    }
}

void MainWindow::refreshBeatCancel()
{
    const bool available = m_radio.state() == Radio::State::Connected && beatCancelSupported(m_radio.mode());
    const BeatCancel bc = available ? m_radio.beatCancel() : BeatCancel::Off;
    m_beatCancelButton->setText(beatCancelName(bc));
    m_beatCancelButton->setChecked(bc != BeatCancel::Off);
    m_beatCancelButton->setEnabled(available);
}

void MainWindow::refreshSquelchOpen()
{
    // With the squelch at 0 it's always open; only show it when it's doing
    // something.
    m_sMeter->setSquelchOpen(m_radio.state() == Radio::State::Connected && m_sqlSlider->value() > 0
                             && m_radio.busy());
}

void MainWindow::refreshKeyer()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    const bool cw = connected && isCwMode(m_radio.mode());
    m_applyingRadioState = true;
    if (const auto wpm = m_radio.keyerSpeed(); wpm && !m_keyerSpeedSlider->isSliderDown())
        m_keyerSpeedSlider->setValue(*wpm);
    if (const auto ms = m_radio.breakInDelay(); ms && !m_breakInDelaySlider->isSliderDown())
        m_breakInDelaySlider->setValue(*ms / kBreakInDelayStep);
    if (const auto index = m_radio.cwPitch(); index && !m_pitchSlider->isSliderDown())
        m_pitchSlider->setValue(*index);
    m_applyingRadioState = false;
    m_breakInButton->setChecked(connected && m_radio.vox());

    m_cwTuneButton->setChecked(connected && m_radio.cwTune());
    m_cwTuneButton->setEnabled(cw);
    m_cwSendButton->setEnabled(cw);
    const int queued = connected ? m_radio.cwQueued() : 0;
    m_cwStopButton->setEnabled(cw);
    m_cwQueueLabel->setText(queued > 0             ? tr("Sending…")
                            : connected && !cw ? tr("Switch to CW or CW-R to send")
                                               : QString());
}

void MainWindow::refreshDecoder()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    // The radio keeps the decoder setting outside CW but only decodes in CW,
    // so show it off there.
    const bool cw = connected && isCwMode(m_radio.mode());
    const bool on = cw && m_radio.morseDecoder();
    m_decoderButton->setChecked(on);
    m_decoderButton->setEnabled(cw);
    m_decoderSlider->setEnabled(on);
    if (const auto level = m_radio.decoderThreshold(); level && !m_decoderSlider->isSliderDown()) {
        m_applyingRadioState = true;
        m_decoderSlider->setValue(*level);
        m_applyingRadioState = false;
    }
}

void MainWindow::refreshAgc()
{
    const bool available = m_radio.state() == Radio::State::Connected && agcSupported(m_radio.mode());
    const Agc agc = available ? m_radio.agc() : Agc::Off;
    m_agcButton->setText(agcName(agc));
    m_agcButton->setChecked(agc != Agc::Off);
    m_agcButton->setEnabled(available);
    m_agcSlider->setEnabled(agc != Agc::Off);
    m_applyingRadioState = true;
    if (const auto value = m_radio.agcTimeConstant(); value && agc != Agc::Off && !m_agcSlider->isSliderDown())
        m_agcSlider->setValue(*value);
    m_applyingRadioState = false;
    m_agcValue->setText(agc == Agc::Off ? QString() : QString::number(m_agcSlider->value()));
}

// ---------------------------------------------------------------------- QRZ

// ---------------------------------------------------------------------- TCI

void MainWindow::startTciServer()
{
    m_tci.close();
    QSettings settings;
    const bool enabled = settings.value(QStringLiteral("tci/enabled"), false).toBool();
    m_tci.setAudio(nullptr);
    if (enabled && settings.value(QStringLiteral("tci/audio"), true).toBool()) {
        // Receive audio comes in through what macOS treats as a microphone.
        const QMicrophonePermission microphone;
        switch (qApp->checkPermission(microphone)) {
        case Qt::PermissionStatus::Undetermined:
            qApp->requestPermission(microphone, this, [this](const QPermission &) { startTciServer(); });
            return;
        case Qt::PermissionStatus::Denied:
            showMessage(tr("TCI audio is off: allow 590Ctrl in System Settings → Privacy & Security → Microphone."));
            break;
        case Qt::PermissionStatus::Granted:
            m_radioAudio.setDevices(
                RadioAudio::findInput(settings.value(QStringLiteral("tci/audioInput")).toByteArray()),
                RadioAudio::findOutput(settings.value(QStringLiteral("tci/audioOutput")).toByteArray()));
            m_tci.setAudio(&m_radioAudio);
            break;
        }
    }
    if (enabled) {
        const auto port = quint16(settings.value(QStringLiteral("tci/port"), tci::kDefaultPort).toUInt());
        const QHostAddress address(tciAddress(settings));
        if (!m_tci.listen(port, address))
            showMessage(tr("TCI server can't use %1 port %2: %3").arg(address.toString()).arg(port)
                            .arg(m_tci.errorString()));
    }
    refreshTciStatus();
}

// The address to listen on. Before there was a choice, a checkbox picked
// between this computer only and every interface.
QString MainWindow::tciAddress(const QSettings &settings)
{
    const QString address = settings.value(QStringLiteral("tci/address")).toString();
    if (!address.isEmpty())
        return address;
    return settings.value(QStringLiteral("tci/allowRemote"), false).toBool() ? QStringLiteral("0.0.0.0")
                                                                              : QStringLiteral("127.0.0.1");
}

void MainWindow::refreshTciStatus()
{
    m_tciStatus->setVisible(m_tci.isListening());
    const int clients = m_tci.clientCount();
    m_tciStatus->setText(tr("TCI %1:%2 · %3")
                             .arg(m_tci.address().toString())
                             .arg(m_tci.port())
                             .arg(clients == 1 ? tr("1 client") : tr("%1 clients").arg(clients)));
}

void MainWindow::editTciServer()
{
    QSettings settings;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("TCI Server"));
    auto *enabled = new QCheckBox(tr("Let TCI programs control the radio"));
    enabled->setChecked(settings.value(QStringLiteral("tci/enabled"), false).toBool());
    auto *port = new QSpinBox;
    port->setRange(1024, 65535);
    port->setValue(settings.value(QStringLiteral("tci/port"), tci::kDefaultPort).toInt());
    // Editable: pick a common choice or one of this computer's addresses, or
    // type one. Only the part before the first space is the address.
    auto *address = new QComboBox;
    address->setEditable(true);
    address->addItem(tr("127.0.0.1 (this computer only)"));
    address->addItem(tr("0.0.0.0 (all network interfaces)"));
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) || (interface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol)
                address->addItem(QStringLiteral("%1 (%2)").arg(ip.toString(), interface.humanReadableName()));
        }
    }
    address->setCurrentText(tciAddress(settings));
    address->setToolTip(tr("TCI has no password: anyone who can reach this address and port can tune and key "
                           "the radio. Use 127.0.0.1 unless the TCI program runs on another computer."));
    const auto addressText = [address] { return address->currentText().section(QLatin1Char(' '), 0, 0).trimmed(); };
    auto *audio = new QCheckBox(tr("Share the radio's USB audio (48 kHz)"));
    audio->setChecked(settings.value(QStringLiteral("tci/audio"), true).toBool());
    audio->setToolTip(tr("Receive audio goes to TCI programs and their transmit audio to the radio, so they "
                         "don't need a sound card of their own."));
    // First entry: find the radio's codec by name, wherever it's plugged in.
    auto deviceCombo = [&](const QList<QAudioDevice> &devices, const QString &key) {
        auto *combo = new QComboBox;
        combo->addItem(tr("USB Audio CODEC (the radio)"), QByteArray());
        for (const QAudioDevice &device : devices)
            combo->addItem(device.description(), device.id());
        const int index = combo->findData(settings.value(key).toByteArray());
        combo->setCurrentIndex(std::max(index, 0));
        return combo;
    };
    auto *input = deviceCombo(QMediaDevices::audioInputs(), QStringLiteral("tci/audioInput"));
    auto *output = deviceCombo(QMediaDevices::audioOutputs(), QStringLiteral("tci/audioOutput"));
    input->setEnabled(audio->isChecked());
    output->setEnabled(audio->isChecked());
    connect(audio, &QCheckBox::toggled, input, &QWidget::setEnabled);
    connect(audio, &QCheckBox::toggled, output, &QWidget::setEnabled);
    auto *note = new QLabel(tr("Programs such as fldigi connect to ws://address:port (with 0.0.0.0, any of this "
                               "computer's addresses). Tuning, mode, "
                               "PTT (DATA SEND), tune, power, split, RIT/XIT, filter and CW are shared. For "
                               "transmit audio over USB, set the radio's DATA SEND input to USB."));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);

    auto *form = new QFormLayout;
    alignForm(form);
    form->addRow(QString(), enabled);
    form->addRow(tr("Address"), address);
    form->addRow(tr("Port"), port);
    form->addRow(QString(), audio);
    form->addRow(tr("Receive audio from"), input);
    form->addRow(tr("Transmit audio to"), output);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (QHostAddress(addressText()).isNull()) {
            QMessageBox::warning(&dialog, tr("TCI Server"),
                                 tr("“%1” isn't an IP address. Use 127.0.0.1, 0.0.0.0 or one of this "
                                    "computer's addresses.").arg(addressText()));
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
    dialog.resize(420, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted)
        return;

    settings.setValue(QStringLiteral("tci/enabled"), enabled->isChecked());
    settings.setValue(QStringLiteral("tci/port"), port->value());
    settings.setValue(QStringLiteral("tci/address"), QHostAddress(addressText()).toString());
    settings.remove(QStringLiteral("tci/allowRemote"));
    settings.setValue(QStringLiteral("tci/audio"), audio->isChecked());
    settings.setValue(QStringLiteral("tci/audioInput"), input->currentData().toByteArray());
    settings.setValue(QStringLiteral("tci/audioOutput"), output->currentData().toByteArray());
    startTciServer();
}

void MainWindow::loadQrzAccount()
{
    QSettings settings;
    m_qrzUsername = settings.value(QStringLiteral("qrz/username")).toString();
    m_qrz.setCredentials(m_qrzUsername, settings.value(QStringLiteral("qrz/password")).toString());
    m_qrz.setLogbookKey(settings.value(QStringLiteral("qrz/logbookKey")).toString());
}

QString MainWindow::stationCallsign() const
{
    // QRZ usernames are callsigns, so that's the default.
    const QString call = QSettings().value(QStringLiteral("qrz/stationCall")).toString().trimmed();
    return (call.isEmpty() ? m_qrzUsername : call).toUpper();
}

void MainWindow::editQrzAccount()
{
    QSettings settings;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("QRZ Account"));
    auto *username = new QLineEdit(m_qrzUsername);
    // The one in use, which is the saved one unless it isn't remembered.
    auto *password = new QLineEdit(m_qrz.password());
    password->setEchoMode(QLineEdit::Password);
    auto *remember = new QCheckBox(tr("Remember password"));
    remember->setChecked(settings.value(QStringLiteral("qrz/rememberPassword"), true).toBool());
    remember->setToolTip(tr("Saved unencrypted in this app's settings file"));
    auto *stationCall = new QLineEdit(settings.value(QStringLiteral("qrz/stationCall")).toString());
    stationCall->setPlaceholderText(tr("Same as the username"));
    auto *key = new QLineEdit(settings.value(QStringLiteral("qrz/logbookKey")).toString());
    key->setPlaceholderText(tr("Optional: only needed for Log Contact"));
    key->setToolTip(tr("From your QRZ Logbook: Settings → API → Current Key. Each station callsign has its own "
                       "logbook and key. Logging through the API needs a QRZ subscription."));
    auto *note = new QLabel(tr("Lookups use the QRZ XML service; without a subscription QRZ returns only part of "
                               "each record."));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);

    auto *form = new QFormLayout;
    alignForm(form);
    form->addRow(tr("Username"), username);
    form->addRow(tr("Password"), password);
    form->addRow(QString(), remember);
    form->addRow(tr("Station callsign"), stationCall);
    form->addRow(tr("Logbook API key"), key);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
    dialog.resize(440, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_qrzUsername = username->text().trimmed();
    settings.setValue(QStringLiteral("qrz/username"), m_qrzUsername);
    settings.setValue(QStringLiteral("qrz/rememberPassword"), remember->isChecked());
    if (remember->isChecked())
        settings.setValue(QStringLiteral("qrz/password"), password->text());
    else
        settings.remove(QStringLiteral("qrz/password"));
    settings.setValue(QStringLiteral("qrz/stationCall"), stationCall->text().trimmed().toUpper());
    settings.setValue(QStringLiteral("qrz/logbookKey"), key->text().trimmed());
    m_qrz.setCredentials(m_qrzUsername, password->text());
    m_qrz.setLogbookKey(key->text());
}

void MainWindow::setQrzStatus(const QString &text, bool error)
{
    m_qrzWorkedText.clear();
    m_qrzStatusText = text;
    m_qrzStatusError = error;
    refreshQrzStatus();
}

void MainWindow::refreshQrzStatus()
{
    QStringList lines;
    if (!m_qrzWorkedText.isEmpty()) {
        const QString text = m_qrzWorkedText.toHtmlEscaped();
        if (m_qrzWorked)
            lines << QStringLiteral("<b style=\"color:#27ae60\">%1</b>").arg(text);
        else
            lines << QStringLiteral("<span style=\"color:%1\">%2</span>")
                         .arg(palette().color(QPalette::PlaceholderText).name(), text);
    }
    if (!m_qrzStatusText.isEmpty()) {
        lines << (m_qrzStatusError ? QStringLiteral("<span style=\"color:#e74c3c\">%1</span>")
                                   : QStringLiteral("%1"))
                     .arg(m_qrzStatusText.toHtmlEscaped());
    }
    m_qrzStatus->setText(lines.join(QStringLiteral("<br>")));
}

void MainWindow::setCallWorked(bool worked)
{
    m_qrzWorked = worked;
    m_qrzCall->setStyleSheet(worked ? QStringLiteral("QLineEdit { color: #27ae60; }") : QString());
    m_qrzCall->setToolTip(worked ? tr("Already in your QRZ log") : QString());
}

void MainWindow::lookUpCall()
{
    const QString call = m_qrzCall->text().trimmed().toUpper();
    if (call.isEmpty())
        return;
    if (!m_qrz.hasCredentials()) {
        editQrzAccount();
        if (!m_qrz.hasCredentials())
            return;
    }
    m_qrzTable->setRowCount(0);
    m_qrzImage->clear();
    m_qrzImage->hide();
    m_qrzImageUrl.clear();
    m_lastLookup.reset();
    m_qrzLookupButton->setEnabled(false);
    setCallWorked(false);
    setQrzStatus(tr("Looking up %1…").arg(call));
    if (m_qrz.hasLogbookKey()) {
        m_qrzWorkedText = tr("Checking your QRZ log…");
        refreshQrzStatus();
        m_qrz.checkLog(call);
    }
    m_qrz.lookup(call);
}

void MainWindow::showLookup(const qrz::LookupResponse &response)
{
    m_qrzLookupButton->setEnabled(true);
    m_lastLookup = response;

    // The spec's order, then anything new in the order QRZ sent it.
    QList<qrz::Field> fields;
    for (const qrz::Field &field : response.callsign) {
        if (!field.value.isEmpty())
            fields << field;
    }
    std::stable_sort(fields.begin(), fields.end(), [](const qrz::Field &a, const qrz::Field &b) {
        return qrz::fieldRank(a.name) < qrz::fieldRank(b.name);
    });
    m_qrzTable->setRowCount(int(fields.size()));
    for (int row = 0; row < fields.size(); ++row) {
        auto *label = new QTableWidgetItem(qrz::fieldLabel(fields[row].name));
        label->setData(Qt::UserRole, fields[row].name);
        label->setForeground(palette().color(QPalette::PlaceholderText));
        m_qrzTable->setItem(row, 0, label);
        m_qrzTable->setItem(row, 1, new QTableWidgetItem(qrz::fieldDisplayValue(fields[row].name, fields[row].value)));
    }
    m_qrzTable->resizeRowsToContents();

    QStringList status;
    status << tr("%1 found.").arg(response.value(u"call"));
    if (!response.message.isEmpty())
        status << response.message;
    if (!response.lookupCount.isEmpty())
        status << tr("Lookups in the last 24 h: %1").arg(response.lookupCount);
    if (!response.subscriptionExpiry.isEmpty())
        status << tr("Subscription: %1").arg(response.subscriptionExpiry);
    m_qrzStatusText = status.join(QStringLiteral(" · "));
    m_qrzStatusError = false;
    refreshQrzStatus();

    if (const QString image = response.value(u"image"); !image.isEmpty()) {
        m_qrzImageUrl = QUrl(image);
        m_qrz.fetchImage(m_qrzImageUrl);
    }
}

void MainWindow::logContact()
{
    const QString call = m_qrzCall->text().trimmed().toUpper();
    if (call.isEmpty()) {
        setQrzStatus(tr("Enter the callsign to log."), true);
        return;
    }
    if (m_radio.state() != Radio::State::Connected) {
        setQrzStatus(tr("Connect to the radio first: the QSO's frequency and mode come from it."), true);
        return;
    }
    if (!m_qrz.hasLogbookKey() || stationCallsign().isEmpty()) {
        editQrzAccount();
        if (!m_qrz.hasLogbookKey() || stationCallsign().isEmpty()) {
            setQrzStatus(tr("Logging needs your station callsign and QRZ Logbook API key (Account…)."), true);
            return;
        }
    }

    qrz::Qso qso;
    qso.call = call;
    qso.stationCall = stationCallsign();
    qso.timeOn = QDateTime::currentDateTimeUtc();
    qso.txHz = m_radio.frequency(m_radio.txVfo());
    if (m_radio.split())
        qso.rxHz = m_radio.frequency(m_radio.rxVfo());
    if (qrz::adifBand(qso.txHz).isEmpty()) {
        setQrzStatus(tr("%1 MHz is outside the amateur bands, so there's no band to log.")
                         .arg(qso.txHz / 1e6, 0, 'f', 3),
                     true);
        return;
    }
    const qrz::AdifMode mode = qrz::adifModeNamed(m_qrzMode->currentText(), m_radio.mode());
    qso.mode = mode.mode;
    qso.submode = mode.submode;
    qso.rstSent = m_qrzRstSent->text().trimmed();
    qso.rstRcvd = m_qrzRstRcvd->text().trimmed();
    qso.txPower = m_radio.txPower();
    qso.comment = m_qrzComment->text().trimmed();
    if (m_lastLookup)
        qrz::applyLookup(qso, *m_lastLookup);

    m_qrzLogButton->setEnabled(false);
    setQrzStatus(tr("Logging %1 on %2 %3 at %4 UTC…")
                     .arg(call, qrz::adifBand(qso.txHz), m_qrzMode->currentText().toUpper(),
                          qso.timeOn.toString(QStringLiteral("HH:mm"))));
    m_qrz.insertQso(qrz::adifRecord(qso));
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About 590Ctrl"),
        // The macOS style draws message box text bold; only the heading should be.
        tr("<h3>590Ctrl %1</h3><div style=\"font-weight:normal\">"
           "<p>Control for the Kenwood TS-590SG over USB.</p>"
           "<p>Copyright © 2026 KC2RGW Chris Esser<br>"
           "Licensed under the GNU General Public License v3.0.</p>"
           "<p><a href=\"https://github.com/kingsqueak/590Ctrl\">github.com/kingsqueak/590Ctrl</a></p>"
           "<p><small>Built with Qt %2, running on Qt %3.</small></p></div>")
            .arg(QCoreApplication::applicationVersion(), QStringLiteral(QT_VERSION_STR), QString::fromLatin1(qVersion())));
}

void MainWindow::showMessage(const QString &text)
{
    statusBar()->showMessage(text, kMessageTimeoutMs);
}

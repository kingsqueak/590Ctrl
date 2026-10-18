#include "MainWindow.h"

#include "CatMonitor.h"
#include "EqualizerPanel.h"
#include "FrequencyDisplay.h"
#include "SerialTransport.h"
#include "SimulatorTransport.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
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
    tabs->addTab(createReceivePage(), tr("Receive"));
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
        m_radioInfo->setText(tr("%1 · firmware %2 · %3")
                                 .arg(name, firmware, m_radio.link()->transport()->description()));
    });
    connect(&m_radio, &Radio::frequencyChanged, this, [this](Vfo vfo, qint64 hz) {
        (vfo == Vfo::B ? m_vfoB : m_vfoA)->setFrequency(hz);
    });
    connect(&m_radio, &Radio::vfoSelectionChanged, this, &MainWindow::onVfoSelectionChanged);
    connect(&m_radio, &Radio::modeChanged, this, &MainWindow::onModeChanged);
    connect(&m_radio, &Radio::sMeterChanged, m_sMeter, &QProgressBar::setValue);
    connect(&m_radio, &Radio::transmitChanged, this, [this](bool tx) {
        m_txIndicator->setEnabled(tx);
        m_sMeter->setFormat(tx ? tr("PWR %v/30") : tr("S %v/30"));
    });
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

    auto *bar = new QWidget;
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->addWidget(new QLabel(tr("Port:")));
    layout->addWidget(m_portCombo);
    layout->addWidget(refresh);
    layout->addWidget(m_baudCombo);
    layout->addWidget(m_connectButton);
    layout->addStretch();

    refreshPorts();
    return bar;
}

QWidget *MainWindow::createVfoPanel()
{
    m_vfoA = new FrequencyDisplay(36);
    m_vfoA->setLabel(QStringLiteral("A"));
    m_vfoB = new FrequencyDisplay(18);
    m_vfoB->setLabel(QStringLiteral("B"));

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

    m_sMeter = new QProgressBar;
    m_sMeter->setRange(0, 30);   // SM answer is in display dots, 0..30
    m_sMeter->setFormat(tr("S %v/30"));
    m_sMeter->setTextVisible(true);
    m_sMeter->setToolTip(tr("Meter in display dots (0–30). S-meter on receive, power on transmit."));

    auto *side = new QVBoxLayout;
    side->addWidget(m_sMeter);
    auto *statusRow = new QHBoxLayout;
    statusRow->addLayout(vfoRow);
    statusRow->addWidget(m_vfoStatus);
    statusRow->addStretch();
    statusRow->addWidget(m_txIndicator);
    side->addLayout(statusRow);
    side->addWidget(m_vfoB);

    auto *panel = new QWidget;
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_vfoA, 3);
    layout->addLayout(side, 2);
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

QWidget *MainWindow::createReceivePage()
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

    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->addWidget(levels, 3);
    layout->addWidget(filters, 2);
    return page;
}

QWidget *MainWindow::createEqualizerPage()
{
    m_rxEq = new EqualizerPanel(EqType::Rx, &m_eqPresets);
    m_txEq = new EqualizerPanel(EqType::Tx, &m_eqPresets);
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(m_rxEq);
    layout->addWidget(m_txEq);
    return page;
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
    const QString port = m_portCombo->currentData().toString();
    const qint32 baud = m_baudCombo->currentData().toInt();
    QSettings settings;
    settings.setValue(QStringLiteral("serial/port"), port);
    settings.setValue(QStringLiteral("serial/baud"), baud);

    if (port == kSimulatorPort)
        m_radio.connectTransport(std::make_unique<SimulatorTransport>());
    else
        m_radio.connectTransport(std::make_unique<SerialTransport>(SerialSettings{port, baud}));
}

void MainWindow::onStateChanged(Radio::State state)
{
    const bool connected = state == Radio::State::Connected;
    m_connectButton->setText(state == Radio::State::Disconnected ? tr("Connect") : tr("Disconnect"));
    m_portCombo->setEnabled(state == Radio::State::Disconnected);
    m_baudCombo->setEnabled(state == Radio::State::Disconnected);
    for (QWidget *widget : std::as_const(m_radioControls))
        widget->setEnabled(connected);
    m_rxEq->setConnected(connected);
    m_txEq->setConnected(connected);

    switch (state) {
    case Radio::State::Disconnected:
        m_radioInfo->setText(tr("Disconnected"));
        m_sMeter->setValue(0);
        m_txIndicator->setEnabled(false);
        onVfoSelectionChanged(Vfo::A, Vfo::A);
        break;
    case Radio::State::Connecting: m_radioInfo->setText(tr("Connecting…")); break;
    case Radio::State::Connected:
        onModeChanged(m_radio.mode(), m_radio.dataMode());
        onVfoSelectionChanged(m_radio.rxVfo(), m_radio.txVfo());
        break;
    }
}

// ------------------------------------------------------------------- updates

void MainWindow::onModeChanged(Mode mode, bool data)
{
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

void MainWindow::showMessage(const QString &text)
{
    statusBar()->showMessage(text, kMessageTimeoutMs);
}

#include "MainWindow.h"

#include "AppStyle.h"
#include "PowerButton.h"
#include "CatMonitor.h"
#include "EqualizerPanel.h"
#include "MenuPanel.h"
#include "MiniWindow.h"
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
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkInterface>
#include <QMediaDevices>
#include <QPermissions>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimeZone>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <tuple>
#include <utility>

using namespace ts590;

namespace {

const QString kSimulatorPort = QStringLiteral("__simulator__");
constexpr int kMessageTimeoutMs = 8000;
const QList<qint32> kBaudRates = {9600, 19200, 38400, 57600, 115200};

constexpr int kVfoAPointSize = 36;
constexpr int kVfoASidePointSize = 28;   // with the tabs beside, where width is short
constexpr int kCwTextPointSize = 14;   // keyboard CW entry and decoded text
constexpr int kRitXitStepHz = 10;
constexpr int kOffsetHoldMs = 500;
constexpr int kSliderMinWidth = 70;
// Tabs beside: the operating controls keep this width and the tabs take the rest.
constexpr int kSideOperateWidth = 840;
const QSize kBelowSize(980, 760);
const QSize kSideSize(1400, 500);

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

// A label that works as a button: the status chips in the top line.
class ChipLabel : public QLabel
{
public:
    ChipLabel()
    {
        setProperty("chip", true);
        setTextFormat(Qt::RichText);
        setCursor(Qt::PointingHandCursor);
    }
    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && onClick)
            onClick();
    }
};

// "● text ▾", the dot green while the thing is working.
QString chipText(const QString &text, bool on)
{
    return QStringLiteral("<span style=\"color:%1\">●</span>&nbsp;%2&nbsp;▾")
        .arg(on ? QStringLiteral("#2FBF71") : QStringLiteral("#8A93A3"), text.toHtmlEscaped());
}

// A flat panel; the look is in AppStyle's sheet ("true", or "vfo" for the
// darker display window).
QFrame *createPanel(const char *kind = "true")
{
    auto *panel = new QFrame;
    panel->setProperty("panel", QLatin1String(kind));
    return panel;
}

// A small capitals heading with a hairline running to the right edge.
QWidget *createHeading(const QString &text)
{
    auto *label = new QLabel(text.toUpper());
    label->setProperty("heading", true);
    auto *rule = new QFrame;
    rule->setProperty("rule", true);
    rule->setFixedHeight(1);
    auto *heading = new QWidget;
    auto *layout = new QHBoxLayout(heading);
    layout->setContentsMargins(0, 0, 0, 2);
    layout->setSpacing(8);
    layout->addWidget(label);
    layout->addWidget(rule, 1);
    return heading;
}

// Joins buttons into one strip for an either/or choice.
QHBoxLayout *createSegment(const QList<QAbstractButton *> &buttons)
{
    auto *layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    for (qsizetype i = 0; i < buttons.size(); ++i) {
        buttons[i]->setProperty("seg", i == 0 ? "first" : i == buttons.size() - 1 ? "last" : "mid");
        layout->addWidget(buttons[i]);
    }
    return layout;
}

// Checks one button of an exclusive group, or none.
void checkOnly(QButtonGroup *group, QAbstractButton *button)
{
    if (button) {
        button->setChecked(true);
    } else if (QAbstractButton *checked = group->checkedButton()) {
        group->setExclusive(false);
        checked->setChecked(false);
        group->setExclusive(true);
    }
}

// Rows of "name or button | slider | value | optional button", in columns
// that line up down a panel.
class RowGrid
{
public:
    RowGrid(int leadWidth, int valueWidth)
        : m_grid(new QGridLayout)
        , m_valueWidth(valueWidth)
    {
        m_grid->setContentsMargins(0, 0, 0, 0);
        m_grid->setHorizontalSpacing(8);
        m_grid->setVerticalSpacing(4);
        m_grid->setColumnMinimumWidth(0, leadWidth);
        m_grid->setColumnStretch(1, 1);
    }

    QGridLayout *layout() const { return m_grid; }

    void add(QWidget *lead, QSlider *slider, QLabel *value, QWidget *extra = nullptr)
    {
        const int row = m_grid->rowCount();
        value->setFixedWidth(m_valueWidth);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_grid->addWidget(lead, row, 0);
        m_grid->addWidget(slider, row, 1);
        m_grid->addWidget(value, row, 2);
        if (extra)
            m_grid->addWidget(extra, row, 3);
        m_grid->setRowMinimumHeight(row, kRowHeight);
    }

    // A named slider whose value is shown as the plain number.
    void addNumber(const QString &name, QSlider *slider, QWidget *extra = nullptr)
    {
        auto *value = new QLabel(QString::number(slider->value()));
        QObject::connect(slider, &QSlider::valueChanged, value, qOverload<int>(&QLabel::setNum));
        add(new QLabel(name), slider, value, extra);
    }

private:
    static constexpr int kRowHeight = 24;
    QGridLayout *m_grid;
    int m_valueWidth;
};

// Left-align labels and stretch fields so every field in a form starts and
// ends at the same x on every platform.
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

    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(createCwPage(), tr("CW"));
    m_tabs->addTab(createQrzPage(), tr("QRZ"));
    m_tabs->addTab(createEqualizerPage(), tr("Equalizer"));
    m_tabs->addTab(new MenuPanel(&m_radio), tr("Menu"));
    m_tabs->addTab(createSetupPage(), tr("Setup"));   // last: the radio chip opens it
    m_tabPanel = createPanel();
    auto *tabLayout = new QVBoxLayout(m_tabPanel);
    tabLayout->setContentsMargins(10, 2, 10, 10);
    tabLayout->addWidget(m_tabs);

    // Everything used while operating, always in view. The tabs sit below it
    // or beside it (View > Tabs Below / Tabs Beside).
    m_operate = new QWidget;
    auto *operateLayout = new QVBoxLayout(m_operate);
    operateLayout->setContentsMargins(0, 0, 0, 0);
    operateLayout->setSpacing(8);
    operateLayout->addWidget(createVfoPanel());
    operateLayout->addWidget(createBandModeRow());
    operateLayout->addWidget(createOperatePanels());
    operateLayout->addStretch();

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(8);
    layout->addWidget(createTopLine());
    m_body = new QBoxLayout(QBoxLayout::TopToBottom);
    m_body->setSpacing(8);
    m_body->addWidget(m_operate);
    m_body->addWidget(m_tabPanel, 1);
    layout->addLayout(m_body, 1);
    setCentralWidget(central);

    m_mini = new MiniWindow(kVfoAPointSize, this);
    m_meters = {m_sMeter, m_mini->meter()};
    m_radioControls << m_mini->radioControls();
    connect(m_mini, &MiniWindow::fullModeRequested, this, [this] { setMiniMode(false); });

    // A slider's own minimum is just the knob, so a narrow window squeezed
    // the sliders to nothing. Keep every slider usable.
    for (QSlider *slider : central->findChildren<QSlider *>()) {
        if (slider->orientation() == Qt::Horizontal)
            slider->setMinimumWidth(kSliderMinWidth);
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
    QAction *miniAction = viewMenu->addAction(tr("Mini Window"), this, [this] { setMiniMode(true); });
    miniAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));

    viewMenu->addSeparator();
    auto *arrangementGroup = new QActionGroup(this);
    m_belowAction = viewMenu->addAction(tr("Tabs Below"), this, [this] { setArrangement(Arrangement::Below); });
    m_sideAction = viewMenu->addAction(tr("Tabs Beside"), this, [this] { setArrangement(Arrangement::Side); });
    for (QAction *action : {m_belowAction, m_sideAction}) {
        action->setCheckable(true);
        arrangementGroup->addAction(action);
    }
    m_tabsAction = viewMenu->addAction(tr("Show Tabs"), this, [this](bool on) { setTabsVisible(on); });
    m_tabsAction->setCheckable(true);
    m_tabsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    viewMenu->addSeparator();

    // Light or dark, applied live and remembered.
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
    // ApplicationSpecificRole puts it in the application menu on macOS, below About.
    QAction *updateAction = helpMenu->addAction(tr("Check for Updates…"), this, [this] { checkForUpdates(true); });
    updateAction->setMenuRole(QAction::ApplicationSpecificRole);

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
        for (QWidget *widget : std::as_const(m_tunePowerWidgets))
            widget->setVisible(model == kModelTs590SG);
        m_monitorSlider->setMaximum(txMonitorMax(model));
        m_decoderGroup->setVisible(model == kModelTs590SG);   // the S has no decoder
        setRadioInfo(tr("%1 · firmware %2 · %3").arg(name, firmware, m_radio.link()->transport()->description()),
                     true);
    });
    connect(&m_radio, &Radio::frequencyChanged, this, [this](Vfo vfo, qint64 hz) {
        (vfo == Vfo::B ? m_vfoB : m_vfoA)->setFrequency(hz);
        (vfo == Vfo::B ? m_mini->vfoB() : m_mini->vfoA())->setFrequency(hz);
        refreshBand();
    });
    connect(&m_radio, &Radio::vfoSelectionChanged, this, &MainWindow::onVfoSelectionChanged);
    connect(&m_radio, &Radio::vfoSelectionChanged, this, &MainWindow::refreshBand);
    connect(&m_radio, &Radio::processorChanged, this, &MainWindow::refreshProcessor);
    connect(&m_radio, &Radio::modeChanged, this, &MainWindow::onModeChanged);
    for (SMeter *meter : std::as_const(m_meters)) {
        connect(&m_radio, &Radio::sMeterChanged, meter, &SMeter::setValue);
        // Follows the slider, so the marker moves while dragging too.
        connect(m_rfSlider, &QSlider::valueChanged, meter, &SMeter::setRfGain);
        connect(&m_radio, &Radio::txMetersChanged, meter, [this, meter] {
            meter->setTxMeters(m_radio.txMeter(TxMeter::Swr), m_radio.txMeter(TxMeter::Comp),
                               m_radio.txMeter(TxMeter::Alc));
        });
        connect(&m_radio, &Radio::transmitChanged, meter, &SMeter::setTransmitting);
    }
    connect(&m_radio, &Radio::transmitChanged, this, [this](bool tx) {
        m_txIndicator->setEnabled(tx);
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
    connect(&m_radio, &Radio::voxDelayChanged, this, [this](int ms) {
        if (m_voxDelaySlider->isSliderDown())
            return;
        m_applyingRadioState = true;
        m_voxDelaySlider->setValue(ms / kVoxDelayStepMs);
        m_applyingRadioState = false;
    });
    for (auto signal : {&Radio::lockChanged, &Radio::muteChanged, &Radio::fineTuningChanged})
        connect(&m_radio, signal, this, &MainWindow::refreshPanelToggles);
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

    // ---- mini window -> radio, the same as the full window's controls
    connect(m_mini->vfoA(), &FrequencyDisplay::frequencyRequested, this,
            [this](qint64 hz) { m_radio.setFrequency(Vfo::A, hz); });
    connect(m_mini->vfoB(), &FrequencyDisplay::frequencyRequested, this,
            [this](qint64 hz) { m_radio.setFrequency(Vfo::B, hz); });
    connect(m_mini->vfoButtons(), &QButtonGroup::idClicked, this, [this](int id) { m_radio.selectVfo(Vfo(id)); });
    connect(m_mini->copyButton(), &QPushButton::clicked, &m_radio, &Radio::copyVfo);
    connect(m_mini->muteButton(), &QPushButton::clicked, this, [this](bool on) {
        m_radio.setMute(on);
        refreshPanelToggles();
    });
    connect(m_mini->txButton(), &QPushButton::pressed, this, [this] { m_radio.setTransmit(true); });
    connect(m_mini->txButton(), &QPushButton::released, this, [this] { m_radio.setTransmit(false); });
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
    m_arrangement = settings.value(QStringLiteral("window/arrangement")).toInt() == int(Arrangement::Side)
                        ? Arrangement::Side
                        : Arrangement::Below;
    m_tabsVisible = settings.value(QStringLiteral("window/tabs"), true).toBool();
    applyArrangement();
    resize(kBelowSize);
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    if (!settings.contains(QStringLiteral("window/geometry")) && m_arrangement == Arrangement::Side)
        fitWindow();
    m_mini->restoreGeometry(settings.value(QStringLiteral("mini/geometry")).toByteArray());
    m_mini->setPinned(settings.value(QStringLiteral("mini/pinned"), true).toBool());
    onStateChanged(Radio::State::Disconnected);

    // Quitting from the mini window (Cmd+Q) never closes the hidden full
    // window, so release the radio and save on the way out as well.
    qApp->installEventFilter(this);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::shutDown);

    connect(&m_updates, &ts590::UpdateChecker::finished, this, &MainWindow::onUpdateChecked);
    checkForUpdates(false);
}

void MainWindow::showStartupWindow()
{
    if (QSettings().value(QStringLiteral("window/mini"), false).toBool())
        setMiniMode(true);
    else
        show();
}

void MainWindow::setMiniMode(bool mini)
{
    m_miniMode = mini;
    QWidget *shown = mini ? static_cast<QWidget *>(m_mini) : this;
    QWidget *hidden = mini ? static_cast<QWidget *>(this) : m_mini;
    // Show one before hiding the other, so there's always a window open.
    shown->show();
    shown->raise();
    shown->activateWindow();
    hidden->hide();
}

// ---------------------------------------------------------------- arrangement

void MainWindow::setArrangement(Arrangement arrangement)
{
    if (arrangement == m_arrangement)
        return;
    rememberSize();
    m_arrangement = arrangement;
    applyArrangement();
    fitWindow();
}

void MainWindow::setTabsVisible(bool visible)
{
    if (visible == m_tabsVisible) {
        applyArrangement();   // puts the button and menu item back in step
        return;
    }
    rememberSize();
    m_tabsVisible = visible;
    applyArrangement();
    fitWindow();
}

void MainWindow::applyArrangement()
{
    const bool side = m_arrangement == Arrangement::Side;
    m_body->setDirection(side ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    m_setupColumns->setDirection(side ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_cwColumns->setDirection(side ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_cwKeyer->setMaximumWidth(side ? QWIDGETSIZE_MAX : 320);
    m_cwKeyer->setSizePolicy(QSizePolicy::Preferred, side ? QSizePolicy::Maximum : QSizePolicy::Preferred);
    m_tabPanel->setVisible(m_tabsVisible);
    const int pointSize = side ? kVfoASidePointSize : kVfoAPointSize;
    m_vfoA->setPointSize(pointSize);
    m_vfoB->setPointSize(pointSize / 2);
    // Beside the tabs the operating controls keep their width; on their own
    // they fill the window.
    m_operate->setMaximumWidth(side && m_tabsVisible
                                   ? std::max(kSideOperateWidth, m_operate->minimumSizeHint().width())
                                   : QWIDGETSIZE_MAX);
    m_tabsButton->setChecked(m_tabsVisible);
    m_tabsAction->setChecked(m_tabsVisible);
    (side ? m_sideAction : m_belowAction)->setChecked(true);
}

// The window's size is remembered for each arrangement, while its tabs show.
void MainWindow::rememberSize()
{
    if (!m_tabsVisible || isMaximized() || isFullScreen() || m_miniMode)
        return;
    QSettings().setValue(m_arrangement == Arrangement::Side ? QStringLiteral("window/sizeSide")
                                                            : QStringLiteral("window/sizeBelow"),
                         size());
}

// After the arrangement or the tabs change: shrink around the operating
// controls, or go back to the size this arrangement had.
void MainWindow::fitWindow()
{
    if (isMaximized() || isFullScreen())
        return;
    QTimer::singleShot(0, this, [this] {
        centralWidget()->layout()->activate();
        layout()->activate();
        const bool side = m_arrangement == Arrangement::Side;
        const QSize minimum = minimumSizeHint();
        if (!m_tabsVisible) {
            resize(side ? minimum.width() : width(), minimum.height());
            return;
        }
        const QSize fallback = side ? kSideSize : kBelowSize;
        const QSize saved = QSettings()
                                .value(side ? QStringLiteral("window/sizeSide") : QStringLiteral("window/sizeBelow"),
                                       fallback)
                                .toSize();
        resize(saved.expandedTo(minimum));
    });
}

void MainWindow::showTab(int index)
{
    m_tabs->setCurrentIndex(index);
}

QSize MainWindow::tabMinimumSize(int index) const
{
    return m_tabs->widget(index)->minimumSizeHint();
}

QWidget *MainWindow::miniWindow() const
{
    return m_mini;
}

void MainWindow::connectSimulator()
{
    m_portCombo->setCurrentIndex(m_portCombo->findData(kSimulatorPort));
    if (m_radio.state() == Radio::State::Disconnected)
        toggleConnection();
}

void MainWindow::setRadioInfo(const QString &text, bool connected)
{
    m_radioChip->setText(chipText(text, connected));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Quit)
        m_mini->setQuitting(true);
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    shutDown();
    event->accept();
}

void MainWindow::shutDown()
{
    if (std::exchange(m_shutDown, true))
        return;
    m_tci.close();   // releases transmit if a TCI client keyed it
    m_radio.disconnectFromRadio();
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("window/arrangement"), int(m_arrangement));
    settings.setValue(QStringLiteral("window/tabs"), m_tabsVisible);
    rememberSize();
    settings.setValue(QStringLiteral("window/mini"), m_miniMode);
    settings.setValue(QStringLiteral("mini/geometry"), m_mini->saveGeometry());
    settings.setValue(QStringLiteral("mini/pinned"), m_mini->isPinned());
}

// ------------------------------------------------------------------ builders

QPushButton *MainWindow::createToggle(const QString &text, const QString &tip, void (Radio::*setter)(bool))
{
    auto *button = new QPushButton(text);
    button->setCheckable(true);
    button->setToolTip(tip);
    connect(button, &QPushButton::clicked, this, [this, setter](bool on) {
        (m_radio.*setter)(on);
        refreshPanelToggles();   // show the radio's state until it answers
    });
    m_radioControls << button;
    return button;
}

// One slim line across the top: power, what's connected, messages, and the
// window's own buttons. Port and speed are on the Setup tab.
QWidget *MainWindow::createTopLine()
{
    m_powerButton = new PowerButton;
    m_powerButton->setToolTip(tr("Radio power"));
    connect(m_powerButton, &PowerButton::clicked, this, &MainWindow::togglePower);

    auto *radioChip = new ChipLabel;
    radioChip->setToolTip(tr("The radio's connection. Click for the port and speed on the Setup tab."));
    radioChip->onClick = [this] {
        setTabsVisible(true);
        m_tabs->setCurrentIndex(m_tabs->count() - 1);
    };
    m_radioChip = radioChip;
    auto *tciChip = new ChipLabel;
    tciChip->onClick = [this] { editTciServer(); };
    m_tciChip = tciChip;

    m_updateBanner = new QFrame;
    m_updateBanner->setObjectName(QStringLiteral("updateBanner"));
    m_updateBanner->setStyleSheet(QStringLiteral(
        "#updateBanner { background: palette(highlight); border-radius: 4px; }"
        "#updateBanner QLabel, #updateBanner QToolButton { color: palette(highlighted-text); }"
        "#updateBanner QToolButton { border: none; background: transparent; padding: 0 2px; }"));
    auto *bannerLayout = new QHBoxLayout(m_updateBanner);
    bannerLayout->setContentsMargins(8, 1, 2, 1);
    bannerLayout->setSpacing(4);
    m_updateLabel = new QLabel;
    m_updateLabel->setTextFormat(Qt::RichText);
    m_updateLabel->setOpenExternalLinks(true);
    m_updateLabel->setToolTip(tr("Open the release page; download and install it from there when you're ready"));
    auto *dismiss = new QToolButton;
    dismiss->setText(QStringLiteral("✕"));
    dismiss->setToolTip(tr("Hide until the next version"));
    connect(dismiss, &QToolButton::clicked, this, [this] {
        QSettings().setValue(QStringLiteral("updates/dismissed"), m_updateVersion);
        m_updateBanner->hide();
    });
    bannerLayout->addWidget(m_updateLabel);
    bannerLayout->addWidget(dismiss);
    m_updateBanner->hide();

    // Passing messages (what the status bar used to show). Ignored width, so
    // a long one never widens the window.
    m_message = new QLabel;
    m_message->setProperty("dim", true);
    m_message->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_messageTimer.setSingleShot(true);
    m_messageTimer.setInterval(kMessageTimeoutMs);
    connect(&m_messageTimer, &QTimer::timeout, m_message, &QLabel::clear);

    m_lockButton = createToggle(tr("LOCK"), tr("Lock the radio's tuning controls"), &Radio::setLock);
    m_fineButton = createToggle(tr("FINE"), tr("Fine tuning, as the radio's FINE key"), &Radio::setFineTuning);

    m_tabsButton = new QPushButton(tr("Tabs"));
    m_tabsButton->setCheckable(true);
    m_tabsButton->setToolTip(tr("Show or hide the CW, QRZ, Equalizer, Menu and Setup tabs"));
    connect(m_tabsButton, &QPushButton::clicked, this, [this](bool on) { setTabsVisible(on); });
    auto *viewButton = new QPushButton(tr("View"));
    viewButton->setToolTip(tr("Switch between the tabs below the controls and the tabs beside them"));
    connect(viewButton, &QPushButton::clicked, this, [this] {
        setArrangement(m_arrangement == Arrangement::Below ? Arrangement::Side : Arrangement::Below);
    });
    auto *miniButton = new QPushButton(tr("Mini"));
    miniButton->setToolTip(tr("Mini window (%1)").arg(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M)
                                                           .toString(QKeySequence::NativeText)));
    connect(miniButton, &QPushButton::clicked, this, [this] { setMiniMode(true); });

    auto *line = new QWidget;
    auto *layout = new QHBoxLayout(line);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(m_powerButton);
    layout->addWidget(m_radioChip);
    layout->addWidget(m_tciChip);
    layout->addWidget(m_updateBanner);
    layout->addWidget(m_message, 1);
    layout->addWidget(m_lockButton);
    layout->addWidget(m_fineButton);
    layout->addSpacing(8);
    layout->addWidget(m_tabsButton);
    layout->addWidget(viewButton);
    layout->addWidget(miniButton);
    return line;
}

// The header: the VFO window with the controls that act on the frequency,
// and the meter with the antenna controls under it.
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
    QList<QAbstractButton *> vfoButtons;
    for (Vfo vfo : {Vfo::A, Vfo::B}) {
        auto *button = new QPushButton(vfo == Vfo::A ? QStringLiteral("A") : QStringLiteral("B"));
        button->setCheckable(true);
        button->setToolTip(tr("Receive and transmit on VFO %1").arg(button->text()));
        m_vfoButtons->addButton(button, int(vfo));
        vfoButtons << button;
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
    m_radioControls << m_splitButton << copyButton;

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
    m_clearOffsetButton = new QPushButton(tr("CLR"));
    m_clearOffsetButton->setToolTip(tr("Set the RIT/XIT offset to 0"));
    connect(m_clearOffsetButton, &QPushButton::clicked, this, [this] { m_offsetSpin->setValue(0); });

    m_vfoStatus = new QLabel;
    m_vfoStatus->setProperty("dim", true);
    // Enabled == transmitting; the disabled look reads as "off".
    m_txIndicator = new QLabel(tr(" TX "));
    m_txIndicator->setObjectName(QStringLiteral("txIndicator"));
    m_txIndicator->setAlignment(Qt::AlignCenter);
    m_txIndicator->setEnabled(false);

    auto *vfoRow = new QHBoxLayout;
    vfoRow->setSpacing(6);
    vfoRow->addLayout(createSegment(vfoButtons));
    vfoRow->addWidget(m_splitButton);
    vfoRow->addWidget(copyButton);
    vfoRow->addStretch();
    auto *offsetRow = new QHBoxLayout;
    offsetRow->setSpacing(6);
    offsetRow->addWidget(m_ritButton);
    offsetRow->addWidget(m_xitButton);
    offsetRow->addWidget(m_offsetSpin);
    offsetRow->addWidget(m_clearOffsetButton);
    auto *statusRow = new QHBoxLayout;
    statusRow->addWidget(m_vfoStatus);
    statusRow->addStretch();
    statusRow->addWidget(m_txIndicator);
    auto *buttons = new QVBoxLayout;
    buttons->setSpacing(6);
    buttons->addStretch();
    buttons->addLayout(vfoRow);
    buttons->addLayout(offsetRow);
    buttons->addLayout(statusRow);
    buttons->addStretch();

    auto *digits = new QVBoxLayout;
    digits->setSpacing(0);
    digits->addStretch();
    digits->addWidget(m_vfoA);
    digits->addWidget(m_vfoB);
    digits->addStretch();

    QFrame *vfoBox = createPanel("vfo");
    auto *boxLayout = new QHBoxLayout(vfoBox);
    boxLayout->setContentsMargins(4, 4, 10, 4);
    boxLayout->setSpacing(12);
    boxLayout->addLayout(digits, 1);
    boxLayout->addLayout(buttons);

    // Peak readout at half the size of the main VFO digits.
    m_sMeter = new SMeter(kVfoAPointSize / 2);

    // Antenna selection (AN): ANT1/ANT2, the separate RX antenna input, and
    // the DRV output (antenna out on an SG whose menu 085 is set to ANT).
    m_antennaButtons = new QButtonGroup(this);
    m_antennaButtons->setExclusive(true);
    QList<QAbstractButton *> antennaButtons;
    for (int antenna : {1, 2}) {
        auto *button = new QPushButton(QString::number(antenna));
        button->setCheckable(true);
        button->setToolTip(tr("Use the ANT %1 connector").arg(antenna));
        m_antennaButtons->addButton(button, antenna);
        antennaButtons << button;
        m_radioControls << button;
    }
    connect(m_antennaButtons, &QButtonGroup::idClicked, this, [this](int antenna) {
        m_radio.setAntenna(antenna);
        refreshAntenna();   // the highlight follows the radio, not the click
    });
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
    m_tuneCycleButton = new QPushButton(tr("AT TUNE"));
    m_tuneCycleButton->setCheckable(true);
    m_tuneCycleButton->setToolTip(tr("Run an antenna tuner cycle (transmits while tuning; click again to stop)"));
    connect(m_tuneCycleButton, &QPushButton::clicked, this, [this](bool on) {
        m_radio.setTunerTuning(on);
        refreshAntenna();   // lit only once the radio reports it's tuning
    });
    m_radioControls << m_rxAntennaButton << m_driveOutButton << m_tunerButton << m_tuneCycleButton;

    auto *antennaLabel = new QLabel(tr("ANT"));
    antennaLabel->setProperty("dim", true);
    auto *antennaRow = new QHBoxLayout;
    antennaRow->setSpacing(5);
    antennaRow->addWidget(antennaLabel);
    antennaRow->addLayout(createSegment(antennaButtons));
    antennaRow->addWidget(m_rxAntennaButton);
    antennaRow->addWidget(m_driveOutButton);
    antennaRow->addStretch();
    antennaRow->addWidget(m_tunerButton);
    antennaRow->addWidget(m_tuneCycleButton);

    QFrame *meterBox = createPanel();
    auto *meterLayout = new QVBoxLayout(meterBox);
    meterLayout->setContentsMargins(10, 8, 10, 8);
    meterLayout->setSpacing(6);
    meterLayout->addWidget(m_sMeter, 1);
    meterLayout->addLayout(antennaRow);

    auto *panel = new QWidget;
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(vfoBox, 3);
    layout->addWidget(meterBox, 2);
    m_radioControls << m_vfoA << m_vfoB;
    return panel;
}

// Bands and modes on one row, always in view.
QWidget *MainWindow::createBandModeRow()
{
    m_bandButtons = new QButtonGroup(this);
    m_bandButtons->setExclusive(true);
    QList<QAbstractButton *> bandButtons;
    for (int band = 0; band < kBandCount; ++band) {
        // "160 m" is "160" here; the general coverage band has no wavelength.
        const QString wavelength = bandWavelength(band);
        auto *button = new QPushButton(wavelength.isEmpty() ? tr("GEN") : wavelength.section(QLatin1Char(' '), 0, 0));
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setToolTip((wavelength.isEmpty() ? tr("General coverage")
                                                 : tr("%1 band (%2 MHz)").arg(wavelength, bandLabel(band)))
                           + tr(". Click the current band again to step through its band memories."));
        m_bandButtons->addButton(button, band);
        bandButtons << button;
        m_radioControls << button;
    }
    connect(m_bandButtons, &QButtonGroup::idClicked, this, [this](int band) {
        m_radio.selectBand(band);
        refreshBand();   // the highlight follows the radio's frequency, not the click
    });

    m_modeButtons = new QButtonGroup(this);
    m_modeButtons->setExclusive(true);
    QList<QAbstractButton *> modeButtons;
    for (Mode mode : kModes) {
        auto *button = new QPushButton(modeName(mode));
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_modeButtons->addButton(button, int(mode));
        modeButtons << button;
        m_radioControls << button;
    }
    connect(m_modeButtons, &QButtonGroup::idClicked, this, [this](int id) { m_radio.setMode(Mode(id)); });

    m_dataButton = new QPushButton(tr("DATA"));
    m_dataButton->setCheckable(true);
    m_dataButton->setToolTip(tr("Data mode (LSB/USB/FM/AM only)"));
    connect(m_dataButton, &QPushButton::clicked, &m_radio, &Radio::setDataMode);
    m_radioControls << m_dataButton;

    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addLayout(createSegment(bandButtons), kBandCount);
    layout->addSpacing(6);
    layout->addLayout(createSegment(modeButtons), int(kModes.size()));
    layout->addWidget(m_dataButton);
    return row;
}

QSlider *MainWindow::createLevelSlider(void (Radio::*setter)(int), int maximum)
{
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, maximum);
    slider->setPageStep(std::max(1, (maximum + 1) / 16));
    connect(slider, &QSlider::valueChanged, this, [this, setter](int value) {
        if (!m_applyingRadioState)
            (m_radio.*setter)(value);
    });
    m_radioControls << slider;
    return slider;
}

QSlider *MainWindow::createPowerSlider(void (Radio::*setter)(int))
{
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(kTxPowerMin, kTxPowerMax);
    connect(slider, &QSlider::valueChanged, this, [this, setter](int watts) {
        if (!m_applyingRadioState)
            (m_radio.*setter)(watts);
    });
    m_radioControls << slider;
    return slider;
}

// The three operating panels: Receive, Filter and Notch, Transmit.
QWidget *MainWindow::createOperatePanels()
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(createReceivePanel(), 10);
    layout->addWidget(createFilterPanel(), 9);
    layout->addWidget(createTransmitPanel(), 9);
    return row;
}

QWidget *MainWindow::createReceivePanel()
{
    m_afSlider = createLevelSlider(&Radio::setAfGain);
    m_afSlider->setToolTip(tr("AF gain: speaker volume"));
    m_sqlSlider = createLevelSlider(&Radio::setSquelch);
    m_sqlSlider->setToolTip(tr("Squelch"));
    m_rfSlider = createLevelSlider(&Radio::setRfGain);
    m_rfSlider->setToolTip(tr("RF gain"));

    const QFontMetrics metrics = fontMetrics();
    RowGrid rows(metrics.horizontalAdvance(QStringLiteral("NB1+2")) + 22,
                 metrics.horizontalAdvance(QStringLiteral("20 ms")) + 6);

    m_muteButton = createToggle(tr("MUTE"),
                                tr("Mute the received audio: the speaker and the USB audio output to the computer "
                                   "and TCI programs. Turning it off puts the USB output level back."),
                                &Radio::setMute);
    m_muteButton->setObjectName(QStringLiteral("muteButton"));   // red while muted, like SEND
    rows.addNumber(tr("AF"), m_afSlider, m_muteButton);

    // The attenuator and preamp share one button beside RF gain.
    m_attenuatorButton = new QPushButton(tr("ATT"));
    m_attenuatorButton->setCheckable(true);
    m_attenuatorButton->setToolTip(tr("Click to cycle: attenuator → preamp → off"));
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
    rows.addNumber(tr("RF"), m_rfSlider, m_attenuatorButton);
    rows.addNumber(tr("SQL"), m_sqlSlider);

    // AGC, NB and NR: the button steps through the variants like the
    // front-panel key; the slider sets the level of whichever is on.
    auto createStepRow = [&rows](QPushButton *&button, QSlider *&slider, QLabel *value) {
        button = new QPushButton;
        button->setCheckable(true);
        slider = new QSlider(Qt::Horizontal);
        slider->setPageStep(1);
        rows.add(button, slider, value);
    };

    // AGC: OFF -> SLOW -> FAST -> OFF. Slow and fast each keep their own time
    // constant, so the slider shows whichever is active.
    m_agcValue = new QLabel;
    createStepRow(m_agcButton, m_agcSlider, m_agcValue);
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

    auto *nbValue = new QLabel;
    createStepRow(m_nbButton, m_nbSlider, nbValue);
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
    createStepRow(m_nrButton, m_nrSlider, m_nrValue);
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

    QFrame *panel = createPanel();
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);
    layout->addWidget(createHeading(tr("Receive")));
    layout->addLayout(rows.layout());
    layout->addStretch();
    return panel;
}

QWidget *MainWindow::createFilterPanel()
{
    m_filterA = new QPushButton(QStringLiteral("A"));
    m_filterB = new QPushButton(QStringLiteral("B"));
    auto *slotGroup = new QButtonGroup(this);
    slotGroup->setExclusive(true);
    for (QPushButton *button : {m_filterA, m_filterB}) {
        button->setCheckable(true);
        button->setToolTip(tr("IF filter %1").arg(button->text()));
        slotGroup->addButton(button);
        m_radioControls << button;
    }
    connect(m_filterA, &QPushButton::clicked, this, [this] { m_radio.setFilterSlot(FilterSlot::A); });
    connect(m_filterB, &QPushButton::clicked, this, [this] { m_radio.setFilterSlot(FilterSlot::B); });
    auto *slotRow = new QHBoxLayout;
    slotRow->addLayout(createSegment({m_filterA, m_filterB}));
    slotRow->addStretch();

    auto *filterGrid = new QGridLayout;
    filterGrid->setContentsMargins(0, 0, 0, 0);
    filterGrid->setHorizontalSpacing(8);
    filterGrid->setVerticalSpacing(4);
    for (int i = 0; i < 2; ++i) {
        m_filterLabels[i] = new QLabel;
        m_filterCombos[i] = new QComboBox;
        connect(m_filterCombos[i], &QComboBox::activated, this, [this, i](int index) {
            const FilterControl &control = i == 0 ? m_filterLayout.first : m_filterLayout.second;
            m_radio.setFilterValue(control.command, filterRawForIndex(control, index));
        });
        filterGrid->addWidget(m_filterLabels[i], i, 0);
        filterGrid->addWidget(m_filterCombos[i], i, 1);
        m_radioControls << m_filterCombos[i];
    }
    filterGrid->setColumnStretch(1, 1);

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
        m_radio.setNotch(on ? NotchMode::Auto : NotchMode::Off, m_notchWideButton->isChecked());
    });
    m_manualNotchButton = new QPushButton(tr("MANUAL"));
    m_manualNotchButton->setCheckable(true);
    m_manualNotchButton->setToolTip(tr("Manual notch at the position set below"));
    connect(m_manualNotchButton, &QPushButton::clicked, this, [this](bool on) {
        m_radio.setNotch(on ? NotchMode::Manual : NotchMode::Off, m_notchWideButton->isChecked());
    });
    m_notchWideButton = new QPushButton(tr("WIDE"));
    m_notchWideButton->setCheckable(true);
    m_notchWideButton->setToolTip(tr("Manual notch bandwidth: wide or normal"));
    connect(m_notchWideButton, &QPushButton::clicked, this,
            [this](bool wide) { m_radio.setNotch(NotchMode::Manual, wide); });

    m_notchSlider = new QSlider(Qt::Horizontal);
    m_notchSlider->setRange(0, kNotchPositionMax);
    m_notchSlider->setPageStep(8);
    m_notchSlider->setToolTip(tr("Manual notch frequency (radio scale 0–127)"));
    connect(m_notchSlider, &QSlider::valueChanged, this, [this](int position) {
        if (!m_applyingRadioState)
            m_radio.setNotchPosition(position);
    });

    auto *notchRow = new QHBoxLayout;
    notchRow->setSpacing(6);
    notchRow->addWidget(m_beatCancelButton);
    notchRow->addLayout(createSegment({m_autoNotchButton, m_manualNotchButton}));
    notchRow->addWidget(m_notchWideButton);
    notchRow->addStretch();
    const QFontMetrics metrics = fontMetrics();
    RowGrid position(0, metrics.horizontalAdvance(QStringLiteral("127")) + 6);
    position.addNumber(tr("Position"), m_notchSlider);

    QFrame *panel = createPanel();
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);
    layout->addWidget(createHeading(tr("Filter")));
    layout->addLayout(slotRow);
    layout->addLayout(filterGrid);
    layout->addSpacing(4);
    layout->addWidget(createHeading(tr("Notch")));
    layout->addLayout(notchRow);
    layout->addLayout(position.layout());
    layout->addStretch();
    return panel;
}

QWidget *MainWindow::createTransmitPanel()
{
    m_txPowerSlider = createPowerSlider(&Radio::setTxPower);
    m_txPowerSlider->setToolTip(tr("Transmit output power (5–100 W, 5–25 W in AM)"));
    m_micGainSlider = createLevelSlider(&Radio::setMicGain, kMicGainMax);
    m_micGainSlider->setToolTip(tr("Microphone gain for SSB and AM (FM mic gain is a menu setting)"));

    const QFontMetrics metrics = fontMetrics();
    RowGrid rows(metrics.horizontalAdvance(tr("PROC OUT")) + 22, metrics.horizontalAdvance(QStringLiteral("100 W")) + 6);

    auto *powerValue = new QLabel;
    connect(m_txPowerSlider, &QSlider::valueChanged, powerValue,
            [powerValue](int watts) { powerValue->setText(tr("%1 W").arg(watts)); });
    rows.add(new QLabel(tr("Power")), m_txPowerSlider, powerValue);
    rows.addNumber(tr("Mic"), m_micGainSlider);

    m_processorButton = new QPushButton(tr("PROC"));
    m_processorButton->setCheckable(true);
    m_processorButton->setToolTip(tr("Speech processor: click to turn on, again for the IN level, "
                                     "again for the OUT level, again for off"));
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
    rows.add(m_processorButton, m_processorSlider, m_processorValue);
    m_radioControls << m_processorButton;

    m_voxButton = new QPushButton(tr("VOX"));
    m_voxButton->setCheckable(true);
    m_voxButton->setToolTip(tr("Voice-operated transmit (break-in in CW)"));
    connect(m_voxButton, &QPushButton::clicked, &m_radio, &Radio::setVox);
    m_voxGainSlider = createLevelSlider(&Radio::setVoxGain, kVoxGainMax);
    m_voxGainSlider->setToolTip(tr("VOX gain: how loud you must speak to key up"));
    auto *voxValue = new QLabel(QStringLiteral("0"));
    connect(m_voxGainSlider, &QSlider::valueChanged, voxValue, qOverload<int>(&QLabel::setNum));
    rows.add(m_voxButton, m_voxGainSlider, voxValue);

    // TX monitor: ML has no on/off of its own (0 is off), so the button
    // sends 0 or the slider's level.
    m_monitorButton = new QPushButton(tr("MON"));
    m_monitorButton->setCheckable(true);
    m_monitorButton->setToolTip(tr("TX monitor: hear your own transmit audio"));
    m_monitorSlider = new QSlider(Qt::Horizontal);
    m_monitorSlider->setPageStep(1);
    m_monitorSlider->setRange(1, txMonitorMax(kModelTs590SG));
    m_monitorSlider->setValue(5);
    m_monitorSlider->setToolTip(tr("TX monitor level"));
    auto *monitorValue = new QLabel;
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
    rows.add(m_monitorButton, m_monitorSlider, monitorValue);

    // SEND latches: red while transmitting until clicked again.
    m_sendButton = new QPushButton(tr("SEND"));
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setCheckable(true);
    m_sendButton->setToolTip(tr("Transmit until clicked again"));
    connect(m_sendButton, &QPushButton::clicked, this, [this](bool on) {
        m_tuneCarrierButton->setChecked(false);   // one kind of transmit at a time
        m_radio.setTransmit(on);
    });

    // TX TUNE latches like SEND, but keys a carrier at Tune power.
    m_tuneCarrierButton = new QPushButton(tr("TX TUNE"));
    m_tuneCarrierButton->setObjectName(QStringLiteral("tuneCarrierButton"));
    m_tuneCarrierButton->setCheckable(true);
    m_tuneCarrierButton->setToolTip(tr("Transmit a tune carrier at the Tune power level (Setup tab) until "
                                       "clicked again"));
    connect(m_tuneCarrierButton, &QPushButton::clicked, this, [this](bool on) {
        m_sendButton->setChecked(false);
        m_radio.setTuneCarrier(on);
    });

    // TX is momentary: transmits only while held down.
    m_txButton = new QPushButton(tr("TX"));
    m_txButton->setObjectName(QStringLiteral("txButton"));
    m_txButton->setToolTip(tr("Hold to transmit"));
    connect(m_txButton, &QPushButton::pressed, this, [this] { m_radio.setTransmit(true); });
    connect(m_txButton, &QPushButton::released, this, [this] { m_radio.setTransmit(false); });
    // A held button can miss its release if the app loses focus mid-press.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState appState) {
        if (appState == Qt::ApplicationActive)
            return;
        for (QPushButton *button : {m_txButton, m_mini->txButton()}) {
            if (button->isDown()) {
                button->setDown(false);
                m_radio.setTransmit(false);
            }
        }
    });
    m_radioControls << m_voxButton << m_sendButton << m_tuneCarrierButton << m_txButton;

    auto *keying = new QHBoxLayout;
    keying->setSpacing(6);
    keying->addWidget(m_sendButton);
    keying->addWidget(m_tuneCarrierButton);
    keying->addWidget(m_txButton, 1);

    QFrame *panel = createPanel();
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);
    layout->addWidget(createHeading(tr("Transmit")));
    layout->addLayout(rows.layout());
    layout->addStretch();
    layout->addSpacing(2);
    layout->addLayout(keying);
    return panel;
}

// Things set once: the connection, the TCI server, the USB audio levels, the
// transmit set-up levels and the transmit audio filter.
QWidget *MainWindow::createSetupPage()
{
    const QFontMetrics metrics = fontMetrics();
    auto section = [](const QString &title, QLayout *content) {
        auto *widget = new QWidget;
        auto *layout = new QVBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->addWidget(createHeading(title));
        layout->addLayout(content);
        return widget;
    };
    auto column = [](std::initializer_list<QWidget *> sections) {
        auto *widget = new QWidget;
        auto *layout = new QVBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        for (QWidget *item : sections)
            layout->addWidget(item);
        layout->addStretch();
        return widget;
    };

    // ---- radio connection
    m_portCombo = new QComboBox;
    m_portCombo->setMinimumContentsLength(20);
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto *refresh = new QPushButton(QStringLiteral("⟳"));
    refresh->setToolTip(tr("Look for serial ports again"));
    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    m_baudCombo = new QComboBox;
    for (qint32 baud : kBaudRates)
        m_baudCombo->addItem(tr("%1 bps").arg(baud), baud);
    m_baudCombo->setToolTip(tr("Must match radio menu 068 (USB port speed)"));
    const qint32 savedBaud = QSettings().value(QStringLiteral("serial/baud"), 115200).toInt();
    m_baudCombo->setCurrentIndex(std::max(0, m_baudCombo->findData(savedBaud)));
    m_connectButton = new QPushButton(tr("Connect"));
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);

    auto *connection = new QGridLayout;
    connection->setHorizontalSpacing(8);
    connection->setVerticalSpacing(4);
    connection->addWidget(new QLabel(tr("Port")), 0, 0);
    connection->addWidget(m_portCombo, 0, 1);
    connection->addWidget(refresh, 0, 2);
    connection->addWidget(new QLabel(tr("Speed")), 1, 0);
    auto *speedRow = new QHBoxLayout;
    speedRow->addWidget(m_baudCombo);
    speedRow->addStretch();
    speedRow->addWidget(m_connectButton);
    connection->addLayout(speedRow, 1, 1, 1, 2);
    connection->setColumnStretch(1, 1);

    // ---- TCI
    m_tciSetupLabel = new QLabel;
    m_tciSetupLabel->setProperty("dim", true);
    m_tciSetupLabel->setWordWrap(true);
    auto *tciButton = new QPushButton(tr("Settings…"));
    tciButton->setToolTip(tr("Let programs such as WSJT-X, fldigi and TCIdigi share the radio"));
    connect(tciButton, &QPushButton::clicked, this, &MainWindow::editTciServer);
    auto *tciRow = new QHBoxLayout;
    tciRow->addWidget(m_tciSetupLabel, 1);
    tciRow->addWidget(tciButton);

    // ---- computer audio
    const int valueWidth = metrics.horizontalAdvance(tr("%1 ms").arg(3000)) + 6;
    const int nameWidth = metrics.horizontalAdvance(tr("Tune power")) + 6;
    m_usbInSlider = createLevelSlider(&Radio::setUsbAudioIn, kUsbAudioLevelMax);
    m_usbInSlider->setToolTip(tr("USB audio input level (menu 071): audio from the computer to the transmitter"));
    m_usbOutSlider = createLevelSlider(&Radio::setUsbAudioOut, kUsbAudioLevelMax);
    m_usbOutSlider->setToolTip(tr("USB audio output level (menu 072): received audio to the computer"));
    RowGrid audio(nameWidth, valueWidth);
    audio.addNumber(tr("USB in"), m_usbInSlider);
    audio.addNumber(tr("USB out"), m_usbOutSlider);

    // ---- transmit set-up
    m_tunePowerSlider = createPowerSlider(&Radio::setTunePower);
    m_tunePowerSlider->setToolTip(tr("Output power used for TX TUNE"));
    m_carrierSlider = createLevelSlider(&Radio::setCarrierLevel, kCarrierLevelMax);
    m_carrierSlider->setToolTip(tr("Carrier level for CW, FSK and AM"));
    m_voxDelaySlider = new QSlider(Qt::Horizontal);
    m_voxDelaySlider->setRange(0, kVoxDelayMaxMs / kVoxDelayStepMs);
    m_voxDelaySlider->setToolTip(tr("VOX delay: how long the radio stays in transmit after you stop speaking"));
    connect(m_voxDelaySlider, &QSlider::valueChanged, this, [this](int steps) {
        if (!m_applyingRadioState)
            m_radio.setVoxDelay(steps * kVoxDelayStepMs);
    });
    m_radioControls << m_voxDelaySlider;
    RowGrid transmit(nameWidth, valueWidth);
    auto *tuneName = new QLabel(tr("Tune power"));
    auto *tuneValue = new QLabel;
    connect(m_tunePowerSlider, &QSlider::valueChanged, tuneValue,
            [tuneValue](int watts) { tuneValue->setText(tr("%1 W").arg(watts)); });
    transmit.add(tuneName, m_tunePowerSlider, tuneValue);
    m_tunePowerWidgets = {tuneName, m_tunePowerSlider, tuneValue};   // the TS-590S has no tune power
    transmit.addNumber(tr("Carrier"), m_carrierSlider);
    auto *voxDelayValue = new QLabel(tr("%1 ms").arg(0));
    connect(m_voxDelaySlider, &QSlider::valueChanged, voxDelayValue,
            [voxDelayValue](int steps) { voxDelayValue->setText(tr("%1 ms").arg(steps * kVoxDelayStepMs)); });
    transmit.add(new QLabel(tr("VOX delay")), m_voxDelaySlider, voxDelayValue);

    // ---- transmit audio filter (menus 031–034)
    auto *txFilter = new QGridLayout;
    txFilter->setHorizontalSpacing(8);
    txFilter->setVerticalSpacing(4);
    txFilter->addWidget(new QLabel(tr("SSB / AM")), 0, 0);
    txFilter->addWidget(new QLabel(tr("SSB data")), 1, 0);
    for (int i = 0; i < kTxFilterCount; ++i) {
        const auto filter = TxFilter(i);
        auto *combo = new QComboBox;
        for (int hz : txFilterValuesHz(filter))
            combo->addItem(tr("%1 Hz").arg(hz));
        combo->setCurrentIndex(-1);
        combo->setToolTip(i % 2 == 0 ? tr("Low cut") : tr("High cut"));
        connect(combo, &QComboBox::activated, this, [this, filter](int index) { m_radio.setTxFilter(filter, index); });
        // TxFilter order: low/high for SSB, then low/high for SSB-DATA.
        txFilter->addWidget(combo, i / 2, 1 + 2 * (i % 2));
        m_txFilterCombos[i] = combo;
        m_radioControls << combo;
    }
    for (int row = 0; row < 2; ++row) {
        auto *to = new QLabel(tr("to"));
        to->setProperty("dim", true);
        txFilter->addWidget(to, row, 2);
    }
    txFilter->setColumnStretch(1, 1);
    txFilter->setColumnStretch(3, 1);

    // Three columns side by side, or stacked when the tabs are beside the
    // operating controls.
    auto *page = new QWidget;
    m_setupColumns = new QBoxLayout(QBoxLayout::LeftToRight, page);
    m_setupColumns->setContentsMargins(0, 4, 16, 0);   // clear of the scroll bar
    m_setupColumns->setSpacing(18);
    m_setupColumns->addWidget(column({section(tr("Radio connection"), connection),
                                      section(tr("Programs sharing the radio (TCI)"), tciRow)}), 1);
    m_setupColumns->addWidget(column({section(tr("Computer audio"), audio.layout()),
                                      section(tr("Transmit set-up"), transmit.layout())}), 1);
    m_setupColumns->addWidget(column({section(tr("Transmit audio filter"), txFilter)}), 1);

    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    // Let the panel behind show through.
    scroll->viewport()->setAutoFillBackground(false);
    page->setAutoFillBackground(false);

    refreshPorts();
    return scroll;
}

QWidget *MainWindow::createCwPage()
{
    const QFontMetrics metrics = fontMetrics();
    RowGrid keyerRows(metrics.horizontalAdvance(tr("BREAK-IN")) + 22,
                      metrics.horizontalAdvance(QStringLiteral("1000 ms")) + 6);

    m_keyerSpeedSlider = createLevelSlider(&Radio::setKeyerSpeed, kKeyerSpeedMax);
    m_keyerSpeedSlider->setMinimum(kKeyerSpeedMin);
    m_keyerSpeedSlider->setPageStep(5);
    m_keyerSpeedSlider->setToolTip(tr("Electronic keyer speed (also used for keyboard CW)"));
    auto *speedValue = new QLabel;
    connect(m_keyerSpeedSlider, &QSlider::valueChanged, speedValue,
            [speedValue](int wpm) { speedValue->setText(tr("%1 WPM").arg(wpm)); });
    keyerRows.add(new QLabel(tr("Speed")), m_keyerSpeedSlider, speedValue);

    m_pitchSlider = createLevelSlider(&Radio::setCwPitch, kCwPitchIndexMax);
    m_pitchSlider->setToolTip(tr("CW pitch and sidetone frequency (a menu setting)"));
    auto *pitchValue = new QLabel;
    connect(m_pitchSlider, &QSlider::valueChanged, pitchValue,
            [pitchValue](int index) { pitchValue->setText(tr("%1 Hz").arg(cwPitchHz(index))); });
    keyerRows.add(new QLabel(tr("Pitch")), m_pitchSlider, pitchValue);

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
    keyerRows.add(m_breakInButton, m_breakInDelaySlider, delayValue);

    m_cwTuneButton = new QPushButton(tr("CW TUNE"));
    m_cwTuneButton->setCheckable(true);
    m_cwTuneButton->setToolTip(tr("Zero in on the CW signal being received (CW mode only)"));
    connect(m_cwTuneButton, &QPushButton::clicked, this, [this](bool on) {
        m_radio.setCwTune(on);
        refreshKeyer();   // lit only once the radio reports it's tuning
    });
    auto *tuneRow = new QHBoxLayout;
    tuneRow->addWidget(m_cwTuneButton);
    tuneRow->addStretch();

    auto *keyer = new QWidget;
    auto *keyerLayout = new QVBoxLayout(keyer);
    keyerLayout->setContentsMargins(0, 0, 0, 0);
    keyerLayout->setSpacing(4);
    keyerLayout->addWidget(createHeading(tr("Keyer")));
    keyerLayout->addLayout(keyerRows.layout());
    keyerLayout->addLayout(tuneRow);
    keyerLayout->addStretch();
    m_cwKeyer = keyer;

    // Keyboard CW: Enter (or SEND) queues the line; the radio keys it at the
    // keyer speed, transmitting if break-in is on.
    m_cwEdit = new QLineEdit;
    // Same face and size as the decoded text above it.
    QFont cwFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    cwFont.setPointSize(kCwTextPointSize);
    m_cwEdit->setFont(cwFont);
    m_cwEdit->setTextMargins(4, 2, 4, 2);
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
    m_cwQueueLabel->setProperty("dim", true);
    auto *entryRow = new QHBoxLayout;
    entryRow->setSpacing(6);
    entryRow->addWidget(m_cwEdit, 1);
    entryRow->addWidget(m_cwSendButton);
    entryRow->addWidget(m_cwStopButton);

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
    m_decoderSlider->setMaximumWidth(140);
    m_decoderSlider->setToolTip(tr("Decoder threshold: raise it if noise decodes as characters"));
    auto *thresholdValue = new QLabel;
    thresholdValue->setFixedWidth(metrics.horizontalAdvance(QStringLiteral("00")) + 6);
    thresholdValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
    auto *thresholdLabel = new QLabel(tr("Threshold"));
    thresholdLabel->setProperty("dim", true);

    m_decoderGroup = new QWidget;
    auto *decoderLayout = new QVBoxLayout(m_decoderGroup);
    decoderLayout->setContentsMargins(0, 0, 0, 0);
    decoderLayout->setSpacing(4);
    auto *decoderRow = new QHBoxLayout;
    decoderRow->setSpacing(6);
    decoderRow->addWidget(createHeading(tr("Decoder")), 1);
    decoderRow->addWidget(m_decoderButton);
    decoderRow->addWidget(thresholdLabel);
    decoderRow->addWidget(m_decoderSlider, 1);
    decoderRow->addWidget(thresholdValue);
    decoderRow->addWidget(clearButton);
    decoderLayout->addLayout(decoderRow);
    decoderLayout->addWidget(m_decoderText, 1);

    auto *right = new QVBoxLayout;
    right->setSpacing(6);
    right->addWidget(m_decoderGroup, 1);
    right->addLayout(entryRow);
    right->addWidget(m_cwQueueLabel);
    right->addStretch(0);   // takes the space when the TS-590S hides the decoder

    // The keyer beside the decoder, or above it when the tabs are beside the
    // operating controls.
    auto *page = new QWidget;
    m_cwColumns = new QBoxLayout(QBoxLayout::LeftToRight, page);
    m_cwColumns->setContentsMargins(0, 4, 0, 0);
    m_cwColumns->setSpacing(14);
    m_cwColumns->addWidget(keyer, 2);
    m_cwColumns->addLayout(right, 5);
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
    // Two short rows, so the page also fits beside the operating controls.
    auto *qsoRow = new QHBoxLayout;
    qsoRow->addWidget(new QLabel(tr("Mode")));
    qsoRow->addWidget(m_qrzMode);
    qsoRow->addSpacing(8);
    qsoRow->addWidget(new QLabel(tr("RST sent")));
    qsoRow->addWidget(m_qrzRstSent);
    qsoRow->addWidget(new QLabel(tr("rcvd")));
    qsoRow->addWidget(m_qrzRstRcvd);
    qsoRow->addStretch();
    auto *logRow = new QHBoxLayout;
    logRow->addWidget(m_qrzComment, 1);
    logRow->addWidget(m_qrzLogButton);

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
    layout->setContentsMargins(0, 4, 0, 0);
    layout->addLayout(lookupRow);
    layout->addWidget(m_qrzStatus);
    layout->addLayout(results, 1);
    layout->addLayout(qsoRow);
    layout->addLayout(logRow);
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
        setRadioInfo(state == Radio::State::PoweredOff ? tr("Radio is switched off") : tr("Not connected"), false);
        for (SMeter *meter : std::as_const(m_meters))
            meter->reset();
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
        refreshPanelToggles();
        refreshSquelchOpen();
        refreshKeyer();
        refreshDecoder();
        break;
    case Radio::State::Connecting: setRadioInfo(tr("Connecting…"), false); break;
    case Radio::State::Connected:
        // The slider may already hold this value, so it won't signal a change.
        for (SMeter *meter : std::as_const(m_meters))
            meter->setRfGain(m_rfSlider->value());
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
        refreshPanelToggles();
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
    checkOnly(m_modeButtons, m_modeButtons->button(int(mode)));
    m_dataButton->setChecked(data);
    m_dataButton->setEnabled(m_radio.state() == Radio::State::Connected && modeSupportsData(mode));
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
    for (auto [a, b, buttons] : {std::tuple{m_vfoA, m_vfoB, m_vfoButtons},
                                 std::tuple{m_mini->vfoA(), m_mini->vfoB(), m_mini->vfoButtons()}}) {
        a->setRole(role(Vfo::A));
        b->setRole(role(Vfo::B));
        if (QAbstractButton *button = connected ? buttons->button(int(rx)) : nullptr) {
            button->setChecked(true);
        } else if (QAbstractButton *checked = buttons->checkedButton()) {
            // Memory mode or disconnected: neither VFO is selected.
            buttons->setExclusive(false);
            checked->setChecked(false);
            buttons->setExclusive(true);
        }
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
    m_notchWideButton->setEnabled(manual);
    // Outside manual the radio always reports "normal"; keep the user's last
    // choice so re-enabling manual restores it.
    if (manual)
        m_notchWideButton->setChecked(m_radio.notchWide());
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
    checkOnly(m_antennaButtons, connected ? m_antennaButtons->button(m_radio.antenna()) : nullptr);
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

void MainWindow::refreshPanelToggles()
{
    const bool connected = m_radio.state() == Radio::State::Connected;
    m_lockButton->setChecked(connected && m_radio.locked());
    m_muteButton->setChecked(connected && m_radio.muted());
    m_mini->muteButton()->setChecked(m_muteButton->isChecked());
    m_fineButton->setChecked(connected && m_radio.fineTuning());
}

void MainWindow::refreshSquelchOpen()
{
    // With the squelch at 0 it's always open; only show it when it's doing
    // something.
    const bool open = m_radio.state() == Radio::State::Connected && m_sqlSlider->value() > 0 && m_radio.busy();
    for (SMeter *meter : std::as_const(m_meters))
        meter->setSquelchOpen(open);
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
    const bool listening = m_tci.isListening();
    const int clients = m_tci.clientCount();
    const QString programs = clients == 1 ? tr("1 program") : tr("%1 programs").arg(clients);
    m_tciChip->setVisible(listening);
    m_tciChip->setText(chipText(tr("TCI · %1").arg(programs), clients > 0));
    m_tciChip->setToolTip(tr("Programs sharing the radio over TCI, at %1 port %2. Click for the settings.")
                              .arg(m_tci.address().toString())
                              .arg(m_tci.port()));
    m_tciSetupLabel->setText(listening ? tr("On · %1 port %2 · %3 connected")
                                             .arg(m_tci.address().toString())
                                             .arg(m_tci.port())
                                             .arg(programs)
                                       : tr("Off"));
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

void MainWindow::checkForUpdates(bool userAsked)
{
    m_updateUserAsked = m_updateUserAsked || userAsked;
    m_updates.check();
}

void MainWindow::onUpdateChecked(bool ok, const QString &version, const QString &page)
{
    const bool userAsked = std::exchange(m_updateUserAsked, false);
    const QString current = QCoreApplication::applicationVersion();
    const bool newer = ok && ts590::UpdateChecker::isNewer(version, current);
    if (newer) {
        m_updateVersion = version;
        m_updateLabel->setText(tr("<a href=\"%1\">Version %2 is available</a>").arg(page.toHtmlEscaped(), version.toHtmlEscaped()));
        // Links take the Link colour, not the style sheet's; match the banner text.
        QPalette palette = m_updateLabel->palette();
        palette.setColor(QPalette::Link, palette.color(QPalette::HighlightedText));
        m_updateLabel->setPalette(palette);
        // Asking from the menu brings back a note the close button hid.
        m_updateBanner->setVisible(userAsked
                                   || QSettings().value(QStringLiteral("updates/dismissed")).toString() != version);
    }
    if (!userAsked)
        return;

    QMessageBox box(this);
    box.setTextFormat(Qt::RichText);
    if (!ok) {
        box.setIcon(QMessageBox::Warning);
        box.setText(tr("Couldn't check for updates"));
        box.setInformativeText(tr("You have version %1. GitHub didn't answer; check the internet connection, "
                                  "or look at the <a href=\"%2\">Releases page</a> yourself.")
                                   .arg(current, QString::fromLatin1(ts590::UpdateChecker::kReleasesPage)));
    } else if (!newer) {
        box.setIcon(QMessageBox::Information);
        box.setText(tr("590Ctrl is up to date"));
        box.setInformativeText(tr("Version %1 is the latest.").arg(current));
    } else {
        box.setIcon(QMessageBox::Information);
        box.setText(tr("590Ctrl %1 is available").arg(version.toHtmlEscaped()));
        box.setInformativeText(tr("You have version %1. The <a href=\"%2\">%3 release page</a> has the new version "
                                  "and what's changed. Download and install it from there when you're ready; "
                                  "your settings are kept.")
                                   .arg(current, page.toHtmlEscaped(), version.toHtmlEscaped()));
    }
    box.exec();
}

void MainWindow::showMessage(const QString &text)
{
    m_message->setText(text);
    m_message->setToolTip(text);
    m_messageTimer.start();
}

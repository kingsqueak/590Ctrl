#include "EqualizerPanel.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QVBoxLayout>

using namespace ts590;

namespace {

constexpr auto kPresetFormat = "590ctrl-eq";
constexpr int kPresetVersion = 1;

QString bandLabel(int band)
{
    const int hz = eqBandFrequencyHz(band);
    return hz < 1000 ? QString::number(hz) : QString::number(hz / 1000.0, 'f', 1) + QLatin1Char('k');
}

QString dbLabel(int db)
{
    return db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db);
}

}   // namespace

EqualizerPanel::EqualizerPanel(EqType type, QWidget *parent)
    : QGroupBox(type == EqType::Rx ? tr("RX Equalizer") : tr("TX Equalizer"), parent)
    , m_type(type)
{
    m_curveCombo = new QComboBox;
    for (int curve = int(EqCurve::Off); curve <= int(EqCurve::User); ++curve)
        m_curveCombo->addItem(eqCurveName(type, EqCurve(curve)), curve);
    connect(m_curveCombo, &QComboBox::activated, this, [this](int index) {
        emit curveSelected(EqCurve(m_curveCombo->itemData(index).toInt()));
    });

    m_hint = new QLabel;
    m_hint->setWordWrap(true);
    m_hint->setForegroundRole(QPalette::PlaceholderText);

    auto *header = new QHBoxLayout;
    header->addWidget(new QLabel(tr("Curve:")));
    header->addWidget(m_curveCombo);
    header->addWidget(m_hint, 1);

    auto *bands = new QGridLayout;
    bands->setHorizontalSpacing(2);
    for (int band = 0; band < kEqBandCount; ++band) {
        auto *value = new QLabel(QStringLiteral("0"));
        value->setAlignment(Qt::AlignCenter);
        value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("-24")) + 4);

        auto *slider = new QSlider(Qt::Vertical);
        slider->setRange(kEqDbMin, kEqDbMax);
        slider->setPageStep(3);
        slider->setTickPosition(QSlider::TicksBothSides);
        slider->setTickInterval(6);
        slider->setMinimumHeight(160);
        slider->setToolTip(tr("%1 Hz").arg(eqBandFrequencyHz(band)));
        connect(slider, &QSlider::valueChanged, this, [this, band] { onSliderChanged(band); });

        auto *freq = new QLabel(bandLabel(band));
        freq->setAlignment(Qt::AlignCenter);
        freq->setForegroundRole(QPalette::PlaceholderText);

        bands->addWidget(value, 0, band);
        bands->addWidget(slider, 1, band, Qt::AlignHCenter);
        bands->addWidget(freq, 2, band);
        m_sliders.append(slider);
        m_valueLabels.append(value);
    }

    m_flatButton = new QPushButton(tr("Flat"));
    m_flatButton->setToolTip(tr("Set every band to 0 dB"));
    connect(m_flatButton, &QPushButton::clicked, this, [this] {
        showLevels(flatEqLevels(), true);
        emit levelsEdited(currentLevels());
    });
    m_reloadButton = new QPushButton(tr("Reload"));
    m_reloadButton->setToolTip(tr("Read the curve and levels back from the radio"));
    connect(m_reloadButton, &QPushButton::clicked, this, &EqualizerPanel::reloadRequested);
    m_loadButton = new QPushButton(tr("Load…"));
    connect(m_loadButton, &QPushButton::clicked, this, &EqualizerPanel::loadPreset);
    m_saveButton = new QPushButton(tr("Save…"));
    connect(m_saveButton, &QPushButton::clicked, this, &EqualizerPanel::savePreset);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_flatButton);
    buttons->addWidget(m_reloadButton);
    buttons->addStretch();
    buttons->addWidget(m_loadButton);
    buttons->addWidget(m_saveButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(header);
    layout->addLayout(bands);
    layout->addLayout(buttons);

    updateEnabledState();
}

void EqualizerPanel::setConnected(bool connected)
{
    m_connected = connected;
    if (!connected) {
        m_curve.reset();
        m_haveLevels = false;
    }
    updateEnabledState();
}

void EqualizerPanel::setEqMode(EqMode mode)
{
    m_mode = mode;
    updateEnabledState();
}

void EqualizerPanel::setCurve(std::optional<EqCurve> curve)
{
    m_curve = curve;
    if (curve)
        m_curveCombo->setCurrentIndex(m_curveCombo->findData(int(*curve)));
    updateEnabledState();
}

void EqualizerPanel::setLevels(const std::optional<EqLevels> &levels)
{
    m_haveLevels = levels.has_value();
    if (levels)
        showLevels(*levels, false);
    updateEnabledState();
}

EqLevels EqualizerPanel::currentLevels() const
{
    EqLevels levels;
    for (int band = 0; band < kEqBandCount; ++band)
        levels[band] = eqDbToRaw(m_sliders[band]->value());
    return levels;
}

void EqualizerPanel::showLevels(const EqLevels &levels, bool force)
{
    for (int band = 0; band < kEqBandCount; ++band) {
        QSlider *slider = m_sliders[band];
        // Don't fight the user: an echo of an older value must not move the
        // slider they're holding.
        if (slider->isSliderDown() && !force)
            continue;
        const QSignalBlocker blocker(slider);
        slider->setValue(eqRawToDb(levels[band]));
        m_valueLabels[band]->setText(dbLabel(slider->value()));
    }
}

void EqualizerPanel::onSliderChanged(int band)
{
    m_valueLabels[band]->setText(dbLabel(m_sliders[band]->value()));
    emit levelsEdited(currentLevels());
}

void EqualizerPanel::updateEnabledState()
{
    const bool selectable = eqCurveSelectable(m_type, m_mode);
    const bool editable = m_connected && m_curve && *m_curve != EqCurve::Off && m_haveLevels;

    m_curveCombo->setEnabled(m_connected && m_curve && selectable);
    for (QSlider *slider : std::as_const(m_sliders))
        slider->setEnabled(editable);
    m_flatButton->setEnabled(editable);
    m_loadButton->setEnabled(editable);
    m_reloadButton->setEnabled(m_connected);
    m_saveButton->setEnabled(m_haveLevels);

    const QString mode = eqModeName(m_mode);
    QString hint;
    if (!m_connected)
        hint = tr("Not connected");
    else if (!selectable)
        hint = tr("TX EQ is always OFF in %1").arg(mode);
    else if (!m_curve)
        hint = tr("Reading %1 curve…").arg(mode);
    else if (*m_curve == EqCurve::Off)
        hint = tr("EQ is OFF for %1. Select a curve to edit levels.").arg(mode);
    else if (*m_curve == EqCurve::User)
        hint = tr("Editing the User curve. Changes are stored in the radio.");
    else
        hint = tr("Preset curve for %1. Levels are kept only on U (User).").arg(mode);
    m_hint->setText(hint);
}

void EqualizerPanel::savePreset()
{
    QSettings settings;
    const QString dirKey = QStringLiteral("eq/presetDir");
    const QString dir = settings.value(dirKey, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString suggested = dir + (m_type == EqType::Rx ? QStringLiteral("/rx-eq.json") : QStringLiteral("/tx-eq.json"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Save EQ Preset"), suggested, tr("EQ presets (*.json)"));
    if (path.isEmpty())
        return;

    QJsonArray bandsHz;
    QJsonArray levelsDb;
    const EqLevels levels = currentLevels();
    for (int band = 0; band < kEqBandCount; ++band) {
        bandsHz.append(eqBandFrequencyHz(band));
        levelsDb.append(eqRawToDb(levels[band]));
    }
    const QJsonObject preset{
        {QStringLiteral("format"), QString::fromLatin1(kPresetFormat)},
        {QStringLiteral("version"), kPresetVersion},
        {QStringLiteral("type"), m_type == EqType::Rx ? QStringLiteral("rx") : QStringLiteral("tx")},
        {QStringLiteral("bands_hz"), bandsHz},
        {QStringLiteral("levels_db"), levelsDb},
    };

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(QJsonDocument(preset).toJson()) < 0) {
        QMessageBox::warning(this, tr("Save EQ Preset"), tr("Could not write %1:\n%2").arg(path, file.errorString()));
        return;
    }
    settings.setValue(dirKey, QFileInfo(path).absolutePath());
}

void EqualizerPanel::loadPreset()
{
    QSettings settings;
    const QString dirKey = QStringLiteral("eq/presetDir");
    const QString dir = settings.value(dirKey, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Load EQ Preset"), dir, tr("EQ presets (*.json)"));
    if (path.isEmpty())
        return;
    settings.setValue(dirKey, QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Load EQ Preset"), tr("Could not read %1:\n%2").arg(path, file.errorString()));
        return;
    }
    QJsonParseError error;
    const QJsonObject preset = QJsonDocument::fromJson(file.readAll(), &error).object();
    const QJsonArray levelsDb = preset.value(QStringLiteral("levels_db")).toArray();
    if (error.error != QJsonParseError::NoError || preset.value(QStringLiteral("format")).toString() != QLatin1String(kPresetFormat)
        || levelsDb.size() != kEqBandCount) {
        QMessageBox::warning(this, tr("Load EQ Preset"), tr("%1 is not a 590Ctrl EQ preset.").arg(path));
        return;
    }

    EqLevels levels;
    for (int band = 0; band < kEqBandCount; ++band)
        levels[band] = eqDbToRaw(levelsDb[band].toInt());
    showLevels(levels, true);
    emit levelsEdited(currentLevels());
}

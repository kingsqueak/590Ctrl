#include "EqualizerPanel.h"

#include "EqPresetLibrary.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ts590;

namespace {

const QString kExportDirKey = QStringLiteral("eq/presetDir");

QString bandLabel(int band)
{
    const int hz = eqBandFrequencyHz(band);
    return hz < 1000 ? QString::number(hz) : QString::number(hz / 1000.0, 'f', 1) + QLatin1Char('k');
}

QString dbLabel(int db)
{
    return db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db);
}

QString exportDirectory()
{
    return QSettings()
        .value(kExportDirKey, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .toString();
}

}   // namespace

EqualizerPanel::EqualizerPanel(EqType type, EqPresetLibrary *library, QWidget *parent)
    : QWidget(parent)
    , m_type(type)
    , m_library(library)
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
        slider->setMinimumHeight(140);
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

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(header);
    layout->addLayout(bands);
    layout->addLayout(createPresetRow());
    layout->addLayout(createAutoLoadRow());

    connect(library, &EqPresetLibrary::presetsChanged, this, [this](EqType changed) {
        if (changed == m_type)
            refreshPresets();
    });
    connect(library, &EqPresetLibrary::bindingsChanged, this, [this](EqType changed) {
        if (changed == m_type)
            refreshBinding();
    });
    refreshPresets();
    updateEnabledState();
}

QLayout *EqualizerPanel::createPresetRow()
{
    m_flatButton = new QPushButton(tr("Flat"));
    m_flatButton->setToolTip(tr("Set every band to 0 dB"));
    connect(m_flatButton, &QPushButton::clicked, this, [this] {
        showLevels(flatEqLevels(), true);
        emit levelsEdited(currentLevels());
    });
    m_reloadButton = new QPushButton(tr("Reload"));
    m_reloadButton->setToolTip(tr("Read the curve and levels back from the radio"));
    connect(m_reloadButton, &QPushButton::clicked, this, &EqualizerPanel::reloadRequested);

    m_presetCombo = new QComboBox;
    m_presetCombo->setMinimumContentsLength(16);
    m_presetCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_presetCombo->setPlaceholderText(tr("No presets"));
    connect(m_presetCombo, &QComboBox::currentIndexChanged, this, &EqualizerPanel::updateEnabledState);

    m_applyButton = new QPushButton(tr("Apply"));
    m_applyButton->setToolTip(tr("Write the selected preset to the radio"));
    connect(m_applyButton, &QPushButton::clicked, this, &EqualizerPanel::applyPreset);
    m_saveAsButton = new QPushButton(tr("Save As…"));
    m_saveAsButton->setToolTip(tr("Save the current levels as a named preset"));
    connect(m_saveAsButton, &QPushButton::clicked, this, &EqualizerPanel::savePresetAs);
    m_deleteButton = new QPushButton(tr("Delete"));
    connect(m_deleteButton, &QPushButton::clicked, this, &EqualizerPanel::deletePreset);

    auto *menu = new QMenu(this);
    menu->addAction(tr("Import…"), this, &EqualizerPanel::importPreset);
    m_exportAction = menu->addAction(tr("Export…"), this, &EqualizerPanel::exportPreset);
    m_moreButton = new QToolButton;
    m_moreButton->setText(tr("More"));
    m_moreButton->setMenu(menu);
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    // Leave room for the menu arrow, which some styles draw over the text.
    m_moreButton->setMinimumWidth(m_moreButton->fontMetrics().horizontalAdvance(m_moreButton->text()) + 28);

    auto *row = new QHBoxLayout;
    row->addWidget(m_flatButton);
    row->addWidget(m_reloadButton);
    row->addStretch();
    row->addWidget(new QLabel(tr("Preset:")));
    row->addWidget(m_presetCombo);
    row->addWidget(m_applyButton);
    row->addWidget(m_saveAsButton);
    row->addWidget(m_deleteButton);
    row->addWidget(m_moreButton);
    return row;
}

QLayout *EqualizerPanel::createAutoLoadRow()
{
    m_autoLoadLabel = new QLabel;
    m_autoLoadCombo = new QComboBox;
    m_autoLoadCombo->setMinimumContentsLength(16);
    m_autoLoadCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_autoLoadCombo->setToolTip(tr("The radio has one User curve shared by all modes. When this mode's curve is "
                                   "U, the chosen preset is written into it automatically on entering the mode."));
    connect(m_autoLoadCombo, &QComboBox::activated, this, [this](int index) {
        m_library->setBinding(m_type, m_mode, m_autoLoadCombo->itemData(index).toString());
    });

    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(m_autoLoadLabel);
    row->addWidget(m_autoLoadCombo);
    return row;
}

// ---------------------------------------------------------------- radio state

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
    if (mode != m_mode) {
        m_mode = mode;
        refreshBinding();
    }
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
    const bool hasPreset = !selectedPreset().isEmpty();

    m_curveCombo->setEnabled(m_connected && m_curve && selectable);
    for (QSlider *slider : std::as_const(m_sliders))
        slider->setEnabled(editable);
    m_flatButton->setEnabled(editable);
    m_reloadButton->setEnabled(m_connected);
    m_applyButton->setEnabled(editable && hasPreset);
    m_saveAsButton->setEnabled(m_haveLevels);
    m_deleteButton->setEnabled(hasPreset);
    m_exportAction->setEnabled(hasPreset);
    m_autoLoadCombo->setEnabled(m_connected && selectable);
    m_autoLoadLabel->setEnabled(m_connected && selectable);

    const QString mode = eqModeName(m_mode);
    m_autoLoadLabel->setText(tr("Auto-load into U in %1:").arg(mode));
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
        hint = tr("Editing the User curve. It is stored in the radio and shared by every mode set to U.");
    else
        hint = tr("Preset curve for %1. Levels are kept only on U (User).").arg(mode);
    m_hint->setText(hint);
}

// -------------------------------------------------------------------- presets

QString EqualizerPanel::selectedPreset() const
{
    return m_presetCombo->currentIndex() >= 0 ? m_presetCombo->currentText() : QString();
}

void EqualizerPanel::selectPreset(const QString &name)
{
    m_presetCombo->setCurrentIndex(m_presetCombo->findText(name));
}

void EqualizerPanel::refreshPresets()
{
    const QString selected = selectedPreset();
    const QStringList names = m_library->names(m_type);
    {
        const QSignalBlocker blocker(m_presetCombo);
        m_presetCombo->clear();
        m_presetCombo->addItems(names);
        m_presetCombo->setCurrentIndex(names.isEmpty() ? -1 : std::max(0, int(names.indexOf(selected))));
    }
    refreshBinding();
    updateEnabledState();
}

void EqualizerPanel::refreshBinding()
{
    const QSignalBlocker blocker(m_autoLoadCombo);
    m_autoLoadCombo->clear();
    m_autoLoadCombo->addItem(tr("(none)"), QString());
    for (const QString &name : m_library->names(m_type))
        m_autoLoadCombo->addItem(name, name);
    m_autoLoadCombo->setCurrentIndex(std::max(0, m_autoLoadCombo->findData(m_library->binding(m_type, m_mode))));
}

void EqualizerPanel::applyPreset()
{
    const auto levels = m_library->levels(m_type, selectedPreset());
    if (!levels)
        return;
    showLevels(*levels, true);
    emit levelsEdited(currentLevels());
}

void EqualizerPanel::savePresetAs()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save EQ Preset"), tr("Preset name:"), QLineEdit::Normal,
                                               selectedPreset(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (m_library->contains(m_type, name)
        && QMessageBox::question(this, tr("Save EQ Preset"), tr("Replace the existing preset “%1”?").arg(name))
               != QMessageBox::Yes)
        return;
    QString error;
    if (!m_library->save(m_type, name, currentLevels(), &error)) {
        QMessageBox::warning(this, tr("Save EQ Preset"), error);
        return;
    }
    selectPreset(name);
}

void EqualizerPanel::deletePreset()
{
    const QString name = selectedPreset();
    if (name.isEmpty()
        || QMessageBox::question(this, tr("Delete EQ Preset"), tr("Delete the preset “%1”?").arg(name))
               != QMessageBox::Yes)
        return;
    QString error;
    if (!m_library->remove(m_type, name, &error))
        QMessageBox::warning(this, tr("Delete EQ Preset"), error);
}

void EqualizerPanel::importPreset()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import EQ Preset"), exportDirectory(),
                                                      tr("EQ presets (*.json)"));
    if (path.isEmpty())
        return;
    QSettings().setValue(kExportDirKey, QFileInfo(path).absolutePath());
    QString error;
    const auto name = m_library->importFile(m_type, path, &error);
    if (!name) {
        QMessageBox::warning(this, tr("Import EQ Preset"), error);
        return;
    }
    selectPreset(*name);
}

void EqualizerPanel::exportPreset()
{
    const QString name = selectedPreset();
    if (name.isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Export EQ Preset"),
                                                      exportDirectory() + QLatin1Char('/') + name + QStringLiteral(".json"),
                                                      tr("EQ presets (*.json)"));
    if (path.isEmpty())
        return;
    QSettings().setValue(kExportDirKey, QFileInfo(path).absolutePath());
    QString error;
    if (!m_library->exportFile(m_type, name, path, &error))
        QMessageBox::warning(this, tr("Export EQ Preset"), error);
}

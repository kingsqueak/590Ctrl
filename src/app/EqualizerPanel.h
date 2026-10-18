#pragma once

#include "CatCodec.h"

#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QToolButton;

namespace ts590 {
class EqPresetLibrary;
}

// 18-band graphic editor for the RX or TX equalizer, the curve selector for
// the current mode family, and the preset library: named curves that can be
// applied to the radio and bound to a mode family for automatic loading into
// the radio's single User curve.
class EqualizerPanel : public QWidget
{
    Q_OBJECT

public:
    EqualizerPanel(ts590::EqType type, ts590::EqPresetLibrary *library, QWidget *parent = nullptr);

    void setConnected(bool connected);
    void setEqMode(ts590::EqMode mode);
    void setCurve(std::optional<ts590::EqCurve> curve);
    void setLevels(const std::optional<ts590::EqLevels> &levels);

signals:
    void curveSelected(ts590::EqCurve curve);
    void levelsEdited(const ts590::EqLevels &levels);
    void reloadRequested();

private:
    QLayout *createPresetRow();
    QLayout *createAutoLoadRow();
    ts590::EqLevels currentLevels() const;
    void showLevels(const ts590::EqLevels &levels, bool force);
    void onSliderChanged(int band);
    void updateEnabledState();
    void refreshPresets();
    void refreshBinding();
    QString selectedPreset() const;
    void selectPreset(const QString &name);

    void applyPreset();
    void savePresetAs();
    void deletePreset();
    void importPreset();
    void exportPreset();

    ts590::EqType m_type;
    ts590::EqPresetLibrary *m_library;
    ts590::EqMode m_mode = ts590::EqMode::Ssb;
    std::optional<ts590::EqCurve> m_curve;
    bool m_connected = false;
    bool m_haveLevels = false;

    QComboBox *m_curveCombo = nullptr;
    QLabel *m_hint = nullptr;
    QList<QSlider *> m_sliders;
    QList<QLabel *> m_valueLabels;
    QPushButton *m_flatButton = nullptr;
    QPushButton *m_reloadButton = nullptr;

    QComboBox *m_presetCombo = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_saveAsButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QToolButton *m_moreButton = nullptr;
    QAction *m_exportAction = nullptr;

    QLabel *m_autoLoadLabel = nullptr;
    QComboBox *m_autoLoadCombo = nullptr;
};

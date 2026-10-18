#pragma once

#include "CatCodec.h"

#include <QGroupBox>

#include <optional>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

// 18-band graphic editor for the RX or TX equalizer, plus the curve selector
// for the current mode family. Levels can be saved to / loaded from JSON
// presets so a User curve can be kept and re-programmed.
class EqualizerPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit EqualizerPanel(ts590::EqType type, QWidget *parent = nullptr);

    void setConnected(bool connected);
    void setEqMode(ts590::EqMode mode);
    void setCurve(std::optional<ts590::EqCurve> curve);
    void setLevels(const std::optional<ts590::EqLevels> &levels);

signals:
    void curveSelected(ts590::EqCurve curve);
    void levelsEdited(const ts590::EqLevels &levels);
    void reloadRequested();

private:
    ts590::EqLevels currentLevels() const;
    void showLevels(const ts590::EqLevels &levels, bool force);
    void onSliderChanged(int band);
    void updateEnabledState();
    void savePreset();
    void loadPreset();

    ts590::EqType m_type;
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
    QPushButton *m_loadButton = nullptr;
    QPushButton *m_saveButton = nullptr;
};

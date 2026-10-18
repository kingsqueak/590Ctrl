#pragma once

#include "CatCodec.h"

#include <QObject>

namespace ts590 {

class EqPresetLibrary;
class Radio;

// The radio has a single User curve per EQ type, shared by every mode family
// set to U. To give each mode its own curve anyway, this writes the preset
// bound to the current mode family into U whenever that family's curve is U
// (on mode changes, and when U gets selected).
class EqAutoLoader : public QObject
{
    Q_OBJECT

public:
    EqAutoLoader(Radio *radio, EqPresetLibrary *library, QObject *parent = nullptr);

signals:
    void applied(ts590::EqType type, ts590::EqMode mode, const QString &preset);

private:
    void apply(EqType type);

    Radio *m_radio;
    EqPresetLibrary *m_library;
};

}   // namespace ts590

#include "EqAutoLoader.h"

#include "EqPresetLibrary.h"
#include "Radio.h"

namespace ts590 {

EqAutoLoader::EqAutoLoader(Radio *radio, EqPresetLibrary *library, QObject *parent)
    : QObject(parent)
    , m_radio(radio)
    , m_library(library)
{
    // Radio emits eqCurveChanged for both types on every mode change, and
    // again when a curve is read or selected.
    connect(radio, &Radio::eqCurveChanged, this, &EqAutoLoader::apply);
    // Binding the current mode should take effect right away.
    connect(library, &EqPresetLibrary::bindingsChanged, this, &EqAutoLoader::apply);
}

void EqAutoLoader::apply(EqType type)
{
    if (m_radio->state() != Radio::State::Connected || m_radio->eqCurve(type) != EqCurve::User)
        return;
    const EqMode mode = m_radio->eqMode();
    const QString name = m_library->binding(type, mode);
    const auto levels = name.isEmpty() ? std::nullopt : m_library->levels(type, name);
    if (!levels || m_radio->eqLevels(type) == levels)
        return;
    m_radio->setEqLevels(type, *levels);
    // A read of the old levels may already be queued ahead of the write.
    // Read again after it so the UI ends up showing what the radio holds.
    m_radio->refreshEqLevels(type);
    emit applied(type, mode, name);
}

}   // namespace ts590

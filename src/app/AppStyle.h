#pragma once

#include <QString>
#include <QStringList>

// Widget style and light/dark appearance, chosen under View and remembered.

QStringList availableStyles();
QString currentStyleName();
// Applies a QStyleFactory style. Styles without native accent highlighting
// (Fusion, Windows) are wrapped so checked buttons, slider fills and
// selections use the system accent color, as the macOS style does.
void applyStyle(const QString &name);

enum class Appearance { System, Light, Dark };
Appearance currentAppearance();
void applyAppearance(Appearance appearance);

// Applies the saved style and appearance; call before creating windows.
void applySavedAppearanceAndStyle();
void saveStyle(const QString &name);
void saveAppearance(Appearance appearance);

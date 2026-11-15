#pragma once

// The app's look: the Fusion style with 590Ctrl's own colours and flat
// controls, in a light and a dark version chosen under View > Appearance.

enum class Appearance { System, Light, Dark };
Appearance currentAppearance();
void applyAppearance(Appearance appearance);
void saveAppearance(Appearance appearance);
bool darkAppearance();   // what is showing now

// Applies the style and the saved appearance; call before creating windows.
void applySavedAppearance();

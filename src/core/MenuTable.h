#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace ts590 {

// One EX menu item, as listed in the PC control command reference.
struct MenuItem {
    enum class Kind {
        Choice,     // P5 is an index into choices
        Text,       // ASCII text, up to maxLength characters
        ReadOnly,   // shown, never written (firmware version)
        Function,   // key assignment code 000..255, 255 = OFF; see the manual
    };

    int number = 0;
    QString name;
    Kind kind = Kind::Choice;
    QStringList choices;
    int maxLength = 0;
    // Shown but not written: changing it would cut this app's own connection.
    bool locked = false;
};

constexpr int kMenuFunctionOff = 255;

// The S (000..087) and SG (000..099) number their menus differently.
const QList<MenuItem> &menuItemsFor(int modelId);
const MenuItem *findMenuItem(int modelId, int number);

// Display text for a raw P5 value; the raw value itself if it doesn't decode.
QString menuValueText(const MenuItem &item, const QByteArray &raw);
// Index (Choice) or code (Function) in a raw P5 value; -1 if it doesn't decode.
int menuValueIndex(const MenuItem &item, const QByteArray &raw);
// P5 for an index or code, zero-padded to `width` digits: the radio answers
// with a fixed width per menu, so pass the width of a value it sent.
QByteArray menuIndexValue(int index, int width);
// Digits used when no answer has been seen yet.
int defaultMenuValueWidth(const MenuItem &item);

}   // namespace ts590

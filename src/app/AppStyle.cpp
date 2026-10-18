#include "AppStyle.h"

#include <QApplication>
#include <QPainter>
#include <QProxyStyle>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOptionButton>

namespace {

const QString kStyleKey = QStringLiteral("ui/style");
const QString kAppearanceKey = QStringLiteral("ui/appearance");

// Fusion draws a checked push button as merely "sunken" and fills sliders
// with the muted selection color. This makes it use the accent blue for both,
// matching what the macOS style does natively.
class AccentStyle : public QProxyStyle
{
public:
    explicit AccentStyle(QStyle *base)
        : QProxyStyle(base)
    {
    }

    void polish(QPalette &palette) override
    {
        QProxyStyle::polish(palette);
        for (auto group : {QPalette::Active, QPalette::Inactive}) {
            palette.setColor(group, QPalette::Highlight, palette.color(group, QPalette::Accent));
            palette.setColor(group, QPalette::HighlightedText, Qt::white);
        }
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget) const override
    {
        const bool checked = (option->state & State_On) && (option->state & State_Enabled);
        if (element == PE_PanelButtonCommand && checked) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            QColor fill = option->palette.color(QPalette::Accent);
            if (option->state & State_Sunken)
                fill = fill.darker(115);
            painter->setBrush(fill);
            painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            painter->restore();
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override
    {
        if (element == CE_PushButtonLabel && (option->state & State_On) && (option->state & State_Enabled)) {
            if (const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option)) {
                QStyleOptionButton copy = *button;
                copy.palette.setColor(QPalette::ButtonText, Qt::white);
                QProxyStyle::drawControl(element, &copy, painter, widget);
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

bool hasNativeAccents(const QString &name)
{
    return name.compare(QLatin1String("macOS"), Qt::CaseInsensitive) == 0;
}

}   // namespace

QStringList availableStyles()
{
    return QStyleFactory::keys();
}

QString currentStyleName()
{
    // The proxy reports its base style's name.
    return QApplication::style()->name();
}

void applyStyle(const QString &name)
{
    QStyle *style = QStyleFactory::create(name);
    if (!style)
        return;
    QApplication::setStyle(hasNativeAccents(name) ? style : new AccentStyle(style));
}

Appearance currentAppearance()
{
    const int saved = QSettings().value(kAppearanceKey, int(Appearance::System)).toInt();
    return saved >= int(Appearance::System) && saved <= int(Appearance::Dark) ? Appearance(saved) : Appearance::System;
}

void applyAppearance(Appearance appearance)
{
    // Unknown hands the choice back to the operating system.
    QGuiApplication::styleHints()->setColorScheme(appearance == Appearance::Light  ? Qt::ColorScheme::Light
                                                  : appearance == Appearance::Dark ? Qt::ColorScheme::Dark
                                                                                   : Qt::ColorScheme::Unknown);
}

void applySavedAppearanceAndStyle()
{
    if (const Appearance appearance = currentAppearance(); appearance != Appearance::System)
        applyAppearance(appearance);
    const QString style = QSettings().value(kStyleKey).toString();
    if (!style.isEmpty() && availableStyles().contains(style, Qt::CaseInsensitive))
        applyStyle(style);
}

void saveStyle(const QString &name)
{
    QSettings().setValue(kStyleKey, name);
}

void saveAppearance(Appearance appearance)
{
    QSettings settings;
    if (appearance == Appearance::System)
        settings.remove(kAppearanceKey);
    else
        settings.setValue(kAppearanceKey, int(appearance));
}

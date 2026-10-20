#include "AppStyle.h"

#include <QApplication>
#include <QPainter>
#include <QProxyStyle>
#include <QSettings>
#include <QSlider>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOptionButton>
#include <QStyleOptionSlider>

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

// The macOS style draws every slider through one shared native NSSlider and
// takes the groove from its cached layout, so after a resize, or while
// another slider moves, grooves were drawn at a stale width, away from the
// knob, or not at all. This draws sliders itself in the same look: a thin
// groove filled with the accent color up to a white knob, and tick dots.
class MacSliderStyle : public QProxyStyle
{
public:
    explicit MacSliderStyle(QStyle *base)
        : QProxyStyle(base)
    {
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                            const QWidget *widget) const override
    {
        const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (control != CC_Slider || !slider) {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }
        const bool horizontal = slider->orientation == Qt::Horizontal;
        const bool enabled = slider->state & State_Enabled;
        const QRectF groove = subControlRect(CC_Slider, slider, SC_SliderGroove, widget);
        const QRectF handle = subControlRect(CC_Slider, slider, SC_SliderHandle, widget);
        const QPalette &palette = slider->palette;
        QColor track = palette.color(QPalette::WindowText);
        track.setAlphaF(0.18);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);

        if (slider->subControls & SC_SliderTickmarks)
            drawTicks(slider, groove, handle, track, painter);

        const QRectF bar = horizontal ? QRectF(groove.left(), groove.center().y() - kTrack / 2, groove.width(), kTrack)
                                      : QRectF(groove.center().x() - kTrack / 2, groove.top(), kTrack, groove.height());
        painter->setBrush(track);
        painter->drawRoundedRect(bar, kTrack / 2, kTrack / 2);

        // Fill from the minimum end to the knob's centre.
        QRectF fill = bar;
        if (horizontal) {
            if (slider->upsideDown)
                fill.setLeft(handle.center().x());
            else
                fill.setRight(handle.center().x());
        } else if (slider->upsideDown) {
            fill.setBottom(handle.center().y());
        } else {
            fill.setTop(handle.center().y());
        }
        if (enabled) {
            painter->setBrush(palette.color(QPalette::Accent));
            painter->drawRoundedRect(fill, kTrack / 2, kTrack / 2);
        }

        const QRectF knob = handle.adjusted(1, 1, -1, -1);
        painter->setPen(QPen(QColor(0, 0, 0, enabled ? 70 : 35), 0.5));
        painter->setBrush(enabled ? QColor(Qt::white) : palette.color(QPalette::Disabled, QPalette::Button));
        painter->drawRoundedRect(knob, knob.height() / 2, knob.height() / 2);
        painter->restore();
    }

    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sub,
                         const QWidget *widget) const override
    {
        const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (control != CC_Slider || !slider)
            return QProxyStyle::subControlRect(control, option, sub, widget);

        const bool horizontal = slider->orientation == Qt::Horizontal;
        const QRect r = slider->rect;
        // Leave room for the ticks on the side(s) they are drawn.
        const bool before = slider->tickPosition & QSlider::TicksAbove;
        const bool after = slider->tickPosition & QSlider::TicksBelow;
        const int across = horizontal ? r.height() : r.width();
        const int centre = (across - (before ? kTickSpace : 0) - (after ? kTickSpace : 0)) / 2
                           + (before ? kTickSpace : 0);
        const int length = horizontal ? r.width() : r.height();

        switch (sub) {
        case SC_SliderGroove:
            return horizontal ? QRect(r.x(), r.y() + centre - kKnobShort / 2, r.width(), kKnobShort)
                              : QRect(r.x() + centre - kKnobShort / 2, r.y(), kKnobShort, r.height());
        case SC_SliderHandle: {
            const int pos = sliderPositionFromValue(slider->minimum, slider->maximum, slider->sliderPosition,
                                                    length - kKnobLong, slider->upsideDown);
            return horizontal ? QRect(r.x() + pos, r.y() + centre - kKnobShort / 2, kKnobLong, kKnobShort)
                              : QRect(r.x() + centre - kKnobShort / 2, r.y() + pos, kKnobShort, kKnobLong);
        }
        default:
            return QProxyStyle::subControlRect(control, option, sub, widget);
        }
    }

    SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex *option, const QPoint &pos,
                                     const QWidget *widget) const override
    {
        if (control != CC_Slider)
            return QProxyStyle::hitTestComplexControl(control, option, pos, widget);
        if (subControlRect(control, option, SC_SliderHandle, widget).contains(pos))
            return SC_SliderHandle;
        return option->rect.contains(pos) ? SC_SliderGroove : SC_None;
    }

private:
    static constexpr qreal kTrack = 4;
    static constexpr int kKnobLong = 22;
    static constexpr int kKnobShort = 14;
    static constexpr int kTickSpace = 6;

    static void drawTicks(const QStyleOptionSlider *slider, const QRectF &groove, const QRectF &handle,
                          const QColor &color, QPainter *painter)
    {
        int interval = slider->tickInterval > 0 ? slider->tickInterval : slider->pageStep;
        if (interval <= 0)
            interval = 1;
        const bool horizontal = slider->orientation == Qt::Horizontal;
        const qreal span = (horizontal ? groove.width() : groove.height()) - kKnobLong;
        const qreal start = (horizontal ? groove.left() : groove.top()) + kKnobLong / 2.0;
        painter->setBrush(color);
        for (int v = slider->minimum; v <= slider->maximum; v += interval) {
            const qreal at = start + sliderPositionFromValue(slider->minimum, slider->maximum, v, int(span),
                                                             slider->upsideDown);
            for (const QSlider::TickPosition side : {QSlider::TicksAbove, QSlider::TicksBelow}) {
                if (!(slider->tickPosition & side))
                    continue;
                const qreal off = (horizontal ? handle.height() : handle.width()) / 2 + kTickSpace / 2.0;
                const QPointF centre = horizontal
                    ? QPointF(at, handle.center().y() + (side == QSlider::TicksAbove ? -off : off))
                    : QPointF(handle.center().x() + (side == QSlider::TicksAbove ? -off : off), at);
                painter->drawEllipse(centre, 1, 1);
            }
        }
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
    if (hasNativeAccents(name))
        QApplication::setStyle(new MacSliderStyle(style));
    else
        QApplication::setStyle(new AccentStyle(style));
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
    // Applied even when nothing is saved, so the default macOS style gets
    // its slider fix too.
    QString style = QSettings().value(kStyleKey).toString();
    if (style.isEmpty() || !availableStyles().contains(style, Qt::CaseInsensitive))
        style = currentStyleName();
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

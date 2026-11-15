#include "AppStyle.h"

#include <QApplication>
#include <QPainter>
#include <QProxyStyle>
#include <QSettings>
#include <QSlider>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOptionSlider>

namespace {

const QString kAppearanceKey = QStringLiteral("ui/appearance");

// Sliders in the app's look on every platform: a thin groove filled with the
// accent color up to a round white knob, and tick dots. (It began as a fix
// for the macOS style, which drew grooves at a stale width after a resize.)
class SliderStyle : public QProxyStyle
{
public:
    explicit SliderStyle(QStyle *base)
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
        painter->setPen(QPen(QColor(0, 0, 0, enabled ? 110 : 45), 0.5));
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
    static constexpr int kKnobLong = 14;
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

// The app's own colours, the same family as SimpleHPSDR's.
struct Theme
{
    const char *window, *panel, *control, *line, *text, *dim, *accent, *base, *red, *green, *disabled, *mid;
};
constexpr Theme kDark{"#0F1115", "#171A21", "#222733", "#2C3240", "#E6E8EB", "#8A93A3",
                      "#3B82F6", "#0A0C10", "#E5484D", "#2FBF71", "#5B6370", "#4A5262"};
constexpr Theme kLight{"#EEF0F3", "#FFFFFF", "#E9ECF1", "#D0D5DD", "#1B1F27", "#667085",
                       "#2563EB", "#FFFFFF", "#D93A3F", "#1E9E5A", "#A0A7B4", "#B4BBC6"};

// The choice under View > Appearance; System follows the operating system.
Appearance g_appearance = Appearance::System;

bool darkScheme()
{
    if (g_appearance != Appearance::System)
        return g_appearance == Appearance::Dark;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QPalette themePalette(const Theme &t)
{
    QPalette p;
    const QColor window(t.window), panel(t.panel), control(t.control), line(t.line), text(t.text), dim(t.dim),
        accent(t.accent), base(t.base), disabled(t.disabled);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, panel);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, control);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Accent, accent);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::ToolTipBase, panel);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Light, control);
    p.setColor(QPalette::Midlight, control);
    p.setColor(QPalette::Mid, QColor(t.mid));
    p.setColor(QPalette::Dark, line);
    p.setColor(QPalette::Shadow, line);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    return p;
}

QString themeSheet(const Theme &t)
{
    QString sheet = QStringLiteral(R"(
QMainWindow, QDialog { background: @window; }
QToolTip { background: @panel; color: @text; border: 1px solid @line; }

QFrame[panel="true"] { background: @panel; border: 1px solid @line; border-radius: 6px; }
QFrame[panel="vfo"] { background: @base; border: 1px solid @line; border-radius: 6px; }
QFrame[rule="true"] { background: @line; border: none; max-height: 1px; min-height: 1px; }
QLabel[heading="true"] { color: @dim; font-size: 10px; font-weight: bold; }
QLabel[dim="true"] { color: @dim; }
QLabel[chip="true"] { color: @dim; background: @panel; border: 1px solid @line; border-radius: 11px;
    padding: 2px 9px; }

QPushButton, QToolButton { background: @control; color: @text; border: 1px solid @line; border-radius: 4px;
    padding: 3px 9px; min-height: 16px; }
QPushButton:hover, QToolButton:hover { border-color: @dim; }
QPushButton:pressed, QToolButton:pressed { background: @line; }
QPushButton:default { border-color: @accent; }
QPushButton:checked, QToolButton:checked { background: @accent; border-color: @accent; color: white; }
QPushButton:disabled, QToolButton:disabled { color: @disabled; }
QPushButton:checked:disabled { background: @line; border-color: @line; color: @dim; }
QPushButton[seg] { padding: 3px 5px; }
QPushButton[seg="first"] { border-top-right-radius: 0; border-bottom-right-radius: 0; }
QPushButton[seg="mid"] { border-radius: 0; border-left: none; }
QPushButton[seg="last"] { border-top-left-radius: 0; border-bottom-left-radius: 0; border-left: none; }

#txButton { color: @red; border: 1px solid @red; background: transparent; font-weight: bold; padding: 6px 9px; }
#txButton:pressed { background: @red; color: white; }
#txButton:disabled { color: @disabled; border-color: @line; }
#sendButton, #tuneCarrierButton { padding: 6px 9px; }
#sendButton:checked, #tuneCarrierButton:checked, #muteButton:checked { background: @red; border-color: @red;
    color: white; }
#txIndicator { border-radius: 4px; padding: 2px 6px; font-weight: bold; color: white; background: @red; }
#txIndicator:disabled { color: @disabled; background: transparent; border: 1px solid @line; }

QLineEdit, QPlainTextEdit, QTableView { background: @base; color: @text; border: 1px solid @line;
    border-radius: 4px; selection-background-color: @accent; selection-color: white; }
QLineEdit { padding: 2px 4px; }
QLineEdit:focus, QPlainTextEdit:focus { border-color: @accent; }
QLineEdit:disabled { color: @disabled; }
QAbstractSpinBox > QLineEdit, QComboBox > QLineEdit, QTableView QLineEdit { border: none; border-radius: 0;
    padding: 0; background: transparent; }
QTableView { alternate-background-color: @panel; gridline-color: @line; }
QHeaderView::section { background: @control; color: @dim; border: none; border-right: 1px solid @line;
    border-bottom: 1px solid @line; padding: 2px 6px; }

QTabWidget::pane { border: none; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: @dim; padding: 5px 12px; border: none;
    border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:hover { color: @text; }
)");
    const std::pair<const char *, const char *> colors[] = {
        {"@window", t.window}, {"@panel", t.panel}, {"@control", t.control}, {"@line", t.line},
        {"@text", t.text},     {"@dim", t.dim},     {"@accent", t.accent},   {"@base", t.base},
        {"@red", t.red},       {"@disabled", t.disabled}};
    for (const auto &[name, value] : colors)
        sheet.replace(QLatin1String(name), QLatin1String(value));
    return sheet;
}

void applyTheme()
{
    const Theme &theme = darkScheme() ? kDark : kLight;
    QApplication::setPalette(themePalette(theme));
    qApp->setStyleSheet(themeSheet(theme));
}

}   // namespace

Appearance currentAppearance()
{
    const int saved = QSettings().value(kAppearanceKey, int(Appearance::System)).toInt();
    return saved >= int(Appearance::System) && saved <= int(Appearance::Dark) ? Appearance(saved) : Appearance::System;
}

void applyAppearance(Appearance appearance)
{
    g_appearance = appearance;
    // Also tell the platform, for the title bar and native dialogs; Unknown
    // hands the choice back to the operating system.
    QGuiApplication::styleHints()->setColorScheme(appearance == Appearance::Light  ? Qt::ColorScheme::Light
                                                  : appearance == Appearance::Dark ? Qt::ColorScheme::Dark
                                                                                   : Qt::ColorScheme::Unknown);
    applyTheme();
}

void applySavedAppearance()
{
    // Fusion on every platform, so the flat look is the same everywhere.
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(new SliderStyle(fusion));
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] { applyTheme(); });
    applyAppearance(currentAppearance());
}

bool darkAppearance()
{
    return darkScheme();
}

void saveAppearance(Appearance appearance)
{
    QSettings settings;
    if (appearance == Appearance::System)
        settings.remove(kAppearanceKey);
    else
        settings.setValue(kAppearanceKey, int(appearance));
}

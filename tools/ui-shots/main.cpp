// ui-shots <output folder>
//
// Renders 590Ctrl's main window to PNG files: both arrangements, each tab,
// light and dark. It runs on the built-in simulator with settings kept in the
// output folder, so it never touches a radio or the real settings. Run it
// with QT_QPA_PLATFORM=offscreen so no window appears.

#include "AppStyle.h"
#include "CwSpectrum.h"
#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QSettings>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>

#include <cmath>
#include <numbers>
#include <vector>

namespace {

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

}   // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc < 2) {
        qWarning("usage: ui-shots <output folder>");
        return 2;
    }
    const QDir out(QString::fromLocal8Bit(argv[1]));
    QDir().mkpath(out.absolutePath());

    // Nothing of the real app's: its own names, settings in the output folder.
    QStandardPaths::setTestModeEnabled(true);
    QApplication::setOrganizationName(QStringLiteral("590Ctrl-ui-shots"));
    QApplication::setApplicationName(QStringLiteral("590Ctrl-ui-shots"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, out.absoluteFilePath(QStringLiteral("settings")));
    QDir(out.absoluteFilePath(QStringLiteral("settings"))).removeRecursively();

    applySavedAppearance();

    struct Tab { const char *name; int index; };
    const Tab tabs[] = {{"cw", 0}, {"qrz", 1}, {"equalizer", 2}, {"menu", 3}, {"setup", 4}};

    for (const bool dark : {true, false}) {
        applyAppearance(dark ? Appearance::Dark : Appearance::Light);
        MainWindow window;
        window.show();
        window.connectSimulator();
        wait(1500);
        if (dark) {
            for (const Tab &tab : tabs) {
                const QSize size = window.tabMinimumSize(tab.index);
                qInfo("tab %s needs %dx%d", tab.name, size.width(), size.height());
            }
        }
        const QString scheme = dark ? QStringLiteral("dark") : QStringLiteral("light");
        auto shot = [&](const QString &name) {
            wait(150);
            const QString file = out.absoluteFilePath(QStringLiteral("%1-%2.png").arg(name, scheme));
            window.grab().save(file);
            qInfo("%s  %dx%d", qPrintable(file), window.width(), window.height());
        };
        for (const auto arrangement : {MainWindow::Arrangement::Below, MainWindow::Arrangement::Side}) {
            const QString prefix = arrangement == MainWindow::Arrangement::Below ? QStringLiteral("below")
                                                                                 : QStringLiteral("side");
            window.setArrangement(arrangement);
            window.setTabsVisible(true);
            for (const Tab &tab : tabs) {
                // The dark set has every tab; the light set one of each arrangement.
                if (!dark && tab.index != 0)
                    continue;
                window.showTab(tab.index);
                shot(prefix + QLatin1Char('-') + QLatin1String(tab.name));
            }
            window.setTabsVisible(false);
            shot(prefix + QStringLiteral("-no-tabs"));
        }
        wait(150);
        window.miniWindow()->grab().save(out.absoluteFilePath(QStringLiteral("mini-%1.png").arg(scheme)));
        window.showHelp();
        wait(300);
        if (QWidget *help = window.findChild<QTextBrowser *>())
            help->grab().save(out.absoluteFilePath(QStringLiteral("help-%1.png").arg(scheme)));
        window.close();

        // The CW tab's tuning display with a signal, which the simulator has none of:
        // noise through a 500 Hz filter and a tone 40 Hz above a 600 Hz pitch.
        CwSpectrum spectrum;
        spectrum.resize(300, 150);
        spectrum.setPitchHz(600);
        spectrum.start();
        std::vector<float> audio(48'000);
        quint32 seed = 1;
        double low = 0, band = 0;
        for (size_t i = 0; i < audio.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            const double noise = double(seed >> 8) / (1 << 24) - 0.5;
            // A state-variable band-pass around 600 Hz.
            const double f = 2 * std::sin(std::numbers::pi * 600 / 48'000), q = 0.8;
            const double high = noise - low - q * band;
            band += f * high;
            low += f * band;
            audio[i] = float(0.02 * band + 0.05 * std::sin(2 * std::numbers::pi * 640 * double(i) / 48'000));
        }
        spectrum.addSamples(audio.data(), qsizetype(audio.size()));
        spectrum.grab().save(out.absoluteFilePath(QStringLiteral("cw-spectrum-%1.png").arg(scheme)));
    }
    return 0;
}

#include "MainWindow.h"

#include "AppStyle.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("590Ctrl"));
    QApplication::setApplicationName(QStringLiteral("590Ctrl"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/590Ctrl.png")));

    // The app's look, light or dark as chosen under View > Appearance.
    applySavedAppearance();

    MainWindow window;
    window.showStartupWindow();
    return app.exec();
}

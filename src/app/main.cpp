#include "MainWindow.h"

#include "AppStyle.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("590Ctrl"));
    QApplication::setApplicationName(QStringLiteral("590Ctrl"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    // Choices from View > Style and View > Appearance.
    applySavedAppearanceAndStyle();

    MainWindow window;
    window.show();
    return app.exec();
}

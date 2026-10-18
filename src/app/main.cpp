#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("590Ctrl"));
    QApplication::setApplicationName(QStringLiteral("590Ctrl"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    MainWindow window;
    window.show();
    return app.exec();
}

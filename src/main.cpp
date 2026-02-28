#include <QApplication>
#include "mainwindow.h"
#include "settings/appsettings.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AstrobinCSV"));
    app.setOrganizationName(QStringLiteral("AstrobinCSV"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));

    MainWindow w;
    w.show();
    return app.exec();
}

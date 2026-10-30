#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QStandardPaths>
#include <QDir>

#include "app/MainWindow.h"
#include "app/Theme.h"
#include "core/Settings.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Arceus AI");
    QApplication::setOrganizationName("Arceus");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setWindowIcon(QIcon(":/icon.svg"));

    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

    Theme::apply(&app);

    arceus::Settings::instance().load();

    MainWindow w;
    w.show();
    return app.exec();
}

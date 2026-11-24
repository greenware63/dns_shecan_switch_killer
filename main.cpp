#include <QApplication>
#include <QFile>
#include <QFont>
#include <QSettings>
#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Shecan Kill Switch");
    app.setOrganizationName("ShecanKS");
    app.setQuitOnLastWindowClosed(true);

    QFont f("Segoe UI Variable", 10);
    app.setFont(f);

    QSettings settings("ShecanKS", "Shecan Kill Switch");
    bool isDark = settings.value("ui/darkMode", true).toBool();
    QFile qss(isDark ? ":/style.qss" : ":/style_light.qss");
    if (qss.open(QFile::ReadOnly | QFile::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    MainWindow w;
    w.show();
    return app.exec();
}
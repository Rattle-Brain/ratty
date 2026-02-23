#include <QApplication>
#include <QIcon>
#include "ui/main_window.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Set application metadata
    QCoreApplication::setOrganizationName("Ratty");
    QCoreApplication::setApplicationName("Ratty Terminal");
    QCoreApplication::setApplicationVersion("0.1.0");

    // Set application icon
    app.setWindowIcon(QIcon("resources/images/ratty-logo.ico"));

    MainWindow window;
    window.show();

    return app.exec();
}

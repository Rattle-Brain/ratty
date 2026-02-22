#include <QApplication>
#include "ui/main_window.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Set application metadata
    QCoreApplication::setOrganizationName("Ratty");
    QCoreApplication::setApplicationName("Ratty Terminal");
    QCoreApplication::setApplicationVersion("0.1.0");

    MainWindow window;
    window.show();

    return app.exec();
}

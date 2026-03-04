#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>
#include <QDebug>
#include "ui/main_window.h"
#include "config/config.h"

int main(int argc, char *argv[]) {
    // Set default OpenGL format before creating QApplication
    // This ensures all OpenGL widgets use OpenGL 3.3 Core Profile
    QSurfaceFormat defaultFormat;
    defaultFormat.setVersion(3, 3);
    defaultFormat.setProfile(QSurfaceFormat::CoreProfile);
    defaultFormat.setDepthBufferSize(24);
    defaultFormat.setStencilBufferSize(8);
    defaultFormat.setSamples(4);  // 4x MSAA
    QSurfaceFormat::setDefaultFormat(defaultFormat);

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon("resources/images/ratty-logo.ico"));

    // Set application metadata
    QCoreApplication::setOrganizationName("Ratty");
    QCoreApplication::setApplicationName("Ratty Terminal");
    QCoreApplication::setApplicationVersion("0.1.1");

    // Load configuration ONCE at startup - before creating any windows/widgets
    // This ensures all components can safely use Config::instance() throughout
    qDebug() << "Loading configuration...";
    Config::instance().load();
    qDebug() << "Configuration loaded successfully";

    // Create and show main window (setupUi handles all window configuration)
    MainWindow window;
    window.show();

    return app.exec();
}

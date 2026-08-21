/*
 * Ratty - a GPU-accelerated terminal emulator
 */

#include "config/config.h"
#include "ui/main_window.h"
#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    /*
     * The surface format has to be set before QApplication exists, because the
     * platform integration reads it when it creates the first GL context.
     *
     * No multisampling and no depth/stencil buffer: everything drawn is
     * axis-aligned, alpha-blended 2D. MSAA cannot improve a glyph quad's edges
     * (there are none - the shape lives in the texture's alpha) and only costs
     * a resolve blit that very slightly blurs the result.
     */
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    format.setSamples(0);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);

    /*
     * On macOS Qt swaps Control and Meta by default: Qt::ControlModifier means
     * the Command key and Qt::MetaModifier means the physical Control key. For a
     * terminal that is exactly backwards -- it makes Command+C send SIGINT while
     * physical Ctrl+C does nothing -- and it would make a "cmd+t" keybinding fire
     * on Ctrl+T. Turning the swap off makes both modifiers mean the same thing on
     * every platform: Ctrl is Ctrl, and Meta ("cmd") is Command.
     */
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta, true);

    /*
     * Share one GL context between every pane, and the reason is latency rather
     * than memory.
     *
     * QOpenGLWidget destroys and recreates its context whenever it is
     * reparented -- which is exactly what splitting a pane does -- *unless*
     * contexts are shared: Qt's own handler for the internal window-change
     * event skips the teardown when this attribute is set. Without it, every
     * split ran initializeGL() again on both the new pane and the one being
     * split, and each of those rebuilt the renderer, re-resolved the whole font
     * chain through fc-match and repopulated the glyph atlas from nothing. That
     * was ~450 ms of blocking work per split, most of it spent waiting on
     * fontconfig subprocesses, and it is what made opening a split feel slow.
     *
     * With the context preserved, a split is pure widget surgery.
     */
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, true);

    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("Ratty"));
    QCoreApplication::setApplicationName(QStringLiteral("Ratty"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    /* From the resource bundle, not a path relative to the working directory. */
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/ratty-logo.png")));

    /* Load once, up front: every widget reads Config::instance() during
     * construction. */
    Config::instance().load();

    MainWindow window;
    window.show();

    return app.exec();
}

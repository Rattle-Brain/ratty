/*
 * Mouse input has to survive the shared canvas.
 *
 * Panes draw through one TerminalCanvas stacked over them, and a native GL
 * window sits above its sibling widgets -- so the canvas, not the pane, is what
 * the platform delivers mouse events to. It hands them back by hit-testing the
 * widget underneath, and everything mouse-driven depends on that working:
 * picking a pane by clicking it, and dragging a divider to resize a split.
 *
 * Neither is covered anywhere else, and both fail silently rather than loudly
 * -- a divider that no longer drags just feels stuck. Hence this suite.
 *
 * Skipped, not failed, when no GL context can be created, so a headless CI run
 * stays green.
 */

#include "check.h"
#include "config/config.h"
#include "ui/main_window.h"
#include "ui/split_container.h"
#include "ui/terminal_widget.h"
#include <QApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSplitter>
#include <QTabBar>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWindow>

namespace {

bool openGlAvailable() {
    QOpenGLContext context;
    context.setFormat(QSurfaceFormat::defaultFormat());
    if (!context.create()) return false;

    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!surface.isValid()) return false;

    return context.makeCurrent(&surface);
}

void settle(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

void sendShortcut(QWidget& window, Qt::KeyboardModifiers modifiers, int key) {
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(&window, &press);
}

/* The canvas window of the only window in play. */
QWindow* canvasWindow() {
    for (QWindow* window : QGuiApplication::allWindows()) {
        if (window->inherits("TerminalCanvas")) return window;
    }
    return nullptr;
}

/*
 * Deliver a mouse event to the canvas, which is where the platform delivers
 * one: over the page area the canvas is the topmost thing there is.
 */
void sendToCanvas(QWindow* canvas, QEvent::Type type, const QPointF& global,
                  Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPointF local = canvas->mapFromGlobal(global.toPoint());
    QMouseEvent event(type, local, global, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(canvas, &event);
}

void testMouseReachesTheWidgetsUnderneath() {
    check::section("mouse events reach the widgets under the canvas");

    MainWindow window;
    window.resize(1200, 700);
    window.show();
    settle(1800);

    /* Ctrl+Shift+W is split_horizontal in the shipped defaults. */
    sendShortcut(window, Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_W);
    settle(1400);

    QWindow* canvas = canvasWindow();
    check::that(canvas != nullptr, "the window has a shared canvas");
    if (!canvas) return;

    SplitContainer* root = window.currentRoot();
    check::that(root != nullptr, "and a pane tree");
    if (!root) return;

    QSplitter* splitter = root->findChild<QSplitter*>();
    check::that(splitter != nullptr, "with a splitter in it");
    if (!splitter || splitter->count() < 2) return;

    /* --- dragging a divider ------------------------------------------- */
    const QList<int> before = splitter->sizes();
    QWidget* handle = splitter->handle(1);
    check::that(handle != nullptr, "and a divider between the two panes");
    if (!handle) return;

    const QPointF start =
        handle->mapToGlobal(QPoint(handle->width() / 2, handle->height() / 2));
    const QPointF end = start + QPointF(150, 0);
    sendToCanvas(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    sendToCanvas(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    sendToCanvas(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    settle(300);

    const QList<int> after = splitter->sizes();
    check::that(after[0] > before[0] + 100,
                "dragging the divider through the canvas resizes the split ("
                    + std::to_string(before[0]) + " -> " + std::to_string(after[0]) + ")");

    /* --- clicking a pane ---------------------------------------------- */
    SplitContainer* last = nullptr;
    root->forEachLeaf([&](SplitContainer* leaf) { last = leaf; });
    check::that(last != nullptr && last->terminal() != nullptr, "there is a pane to click");
    if (!last || !last->terminal()) return;

    TerminalWidget* terminal = last->terminal();
    const QPointF centre =
        terminal->mapToGlobal(QPoint(terminal->width() / 2, terminal->height() / 2));
    sendToCanvas(canvas, QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton);
    sendToCanvas(canvas, QEvent::MouseButtonRelease, centre, Qt::LeftButton, Qt::NoButton);
    settle(300);

    /*
     * focusWidget(), not hasFocus(). Both say the click was routed to the pane
     * and the pane claimed focus, but hasFocus() additionally requires the
     * *window* to be active -- and under a bare X server with no window manager
     * nothing ever activates a window, so it is false there however well the
     * forwarding works.
     */
    check::that(terminal->window()->focusWidget() == terminal,
                "clicking a pane through the canvas focuses it");
}

/*
 * The canvas covers the page, and must not creep over the tab bar.
 *
 * A native child window is drawn above its siblings whatever the widget stack
 * says, so a canvas sized to the whole tab widget rather than to its page would
 * hide the tab bar completely -- and the bar only appears once a second tab
 * exists, so the mistake would not show until then.
 */
void testTabBarStaysVisible() {
    check::section("the canvas leaves the tab bar alone");

    MainWindow window;
    window.resize(1100, 640);
    window.show();
    settle(1600);

    /* cmd+t is new_tab in the shipped macOS defaults; the bar appears with the
     * second tab, which is what shrinks the page area. Qt maps cmd to
     * MetaModifier here -- see AA_MacDontSwapCtrlAndMeta in main(). */
    sendShortcut(window, Qt::MetaModifier, Qt::Key_T);
    settle(1500);

    check::equal(window.tabCount(), 2, "a second tab was opened");

    QTabBar* bar = window.findChild<QTabBar*>();
    check::that(bar != nullptr && bar->isVisible(), "and the tab bar is showing");

    QWindow* canvas = canvasWindow();
    check::that(canvas != nullptr, "the canvas exists");
    if (!bar || !canvas || !bar->isVisible()) return;

    const QRect barArea(bar->mapToGlobal(QPoint(0, 0)), bar->size());
    const QRect canvasArea(canvas->mapToGlobal(QPoint(0, 0)), canvas->size());
    check::that(!canvasArea.intersects(barArea),
                "and the canvas does not overlap it");

    /* The page a canvas covers must still be the one on screen. */
    QWidget* page = window.currentRoot();
    check::that(page != nullptr, "the current tab has a page");
    if (!page) return;
    const QRect pageArea(page->mapToGlobal(QPoint(0, 0)), page->size());
    check::that(canvasArea.contains(pageArea.center()),
                "and it does cover the page that is showing");
}

} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, true);

    QApplication app(argc, argv);

    if (!openGlAvailable()) {
        std::printf("test_canvas_input: skipped (no OpenGL context available)\n");
        return 0;
    }

    Config::instance().load();
    testMouseReachesTheWidgetsUnderneath();
    testTabBarStaysVisible();
    return check::report("test_canvas_input");
}

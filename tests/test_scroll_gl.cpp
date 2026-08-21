/*
 * Scrollback through the widget, which needs a real OpenGL context.
 *
 * `test_terminal` covers the model and `test_mouse` the wire format, but neither
 * can reach the part a user actually touches: a wheel event arriving at a
 * TerminalWidget. The widget only has a session and a grid layout once
 * initializeGL() has run, which needs a context -- the same reason
 * `test_splits_gl` exists.
 *
 * Skipped, not failed, when no context can be created, so a headless CI run
 * stays green while a developer machine still exercises it.
 */

#include "check.h"
#include "config/config.h"
#include "ui/split_container.h"
#include "ui/terminal_widget.h"
#include <QApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>

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

/* Type a whole string in one event. InputHandler falls through to the event's
 * text() for anything that is not a named key, so this is exactly what a run of
 * ordinary keystrokes produces. */
void type(QWidget* target, const QString& text) {
    QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QApplication::sendEvent(target, &event);
}

void pressReturn(QWidget* target) {
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QApplication::sendEvent(target, &event);
}

/* `angleDelta` is in eighths of a degree; 120 is one notch of a conventional
 * wheel, and a trackpad sends fractions of it. Positive is a scroll back through
 * the history. */
void wheel(QWidget* target, int angleDelta, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QWheelEvent event(QPointF(20, 20), target->mapToGlobal(QPointF(20, 20)),
                      QPoint(0, angleDelta), QPoint(0, angleDelta),
                      Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &event);
}

/*
 * A shown pane with a shell on it, or nullptr.
 *
 * The window has to be real -- QOpenGLWidget cannot create a context otherwise --
 * which means it is on screen and the windowing system will happily deliver the
 * developer's own trackpad scrolling to it. That made this suite fail with
 * offsets nobody asked for, so the pane refuses native mouse input;
 * QApplication::sendEvent() bypasses hit-testing and still reaches it.
 */
TerminalWidget* preparePane(SplitContainer* root) {
    root->resize(900, 500);
    root->show();
    settle(1200);

    TerminalWidget* pane = root->terminal();
    if (!pane || pane->shellPid() <= 0) return nullptr;

    pane->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    return pane;
}

/* Enough output to scroll several screenfuls off the top. */
void produceScrollback(TerminalWidget* pane) {
    type(pane, QStringLiteral("seq 1 400"));
    pressReturn(pane);
    settle(1500);
}

void testWheelMovesTheView() {
    check::section("the wheel moves the scrollback view");

    SplitContainer* root = SplitContainer::createLeaf(nullptr);
    TerminalWidget* pane = preparePane(root);
    if (!pane) {
        check::that(false, "the pane started a shell");
        delete root;
        return;
    }

    produceScrollback(pane);

    const int history = pane->historySize();
    check::that(history > 100, "the command's output went into the scrollback");
    check::equal(pane->viewOffset(), 0, "and the view is still live");

    const int step = Config::instance().scrollMultiplier();

    wheel(pane, 120);
    check::equal(pane->viewOffset(), step, "one notch back moves one wheel step");

    wheel(pane, 120);
    check::equal(pane->viewOffset(), 2 * step, "another notch moves another");

    wheel(pane, -120);
    check::equal(pane->viewOffset(), step, "and a notch forward comes back");

    wheel(pane, -1200);
    check::equal(pane->viewOffset(), 0, "scrolling past the live screen stops there");

    /*
     * Trackpads and high-resolution wheels send fractions of a notch. Taking
     * each event on its own would either ignore them or scroll a full step per
     * event; the remainder is carried instead.
     */
    wheel(pane, 40);
    check::equal(pane->viewOffset(), 0, "a third of a notch alone moves nothing");
    wheel(pane, 40);
    wheel(pane, 40);
    check::equal(pane->viewOffset(), step, "three of them add up to one notch");

    /* Typing returns to the live screen. Asserted before the event loop runs
     * again, so this is the keystroke doing it and not the shell's echo. */
    type(pane, QStringLiteral("x"));
    check::equal(pane->viewOffset(), 0, "typing snaps the view back to the live screen");

    settle(200);
    check::equal(pane->viewOffset(), 0, "and it stays there once the echo arrives");

    delete root;
    settle(50);
}

void testPageAndClear() {
    check::section("page scrolling and clearing the scrollback");

    SplitContainer* root = SplitContainer::createLeaf(nullptr);
    TerminalWidget* pane = preparePane(root);
    if (!pane) {
        check::that(false, "the pane started a shell");
        delete root;
        return;
    }

    produceScrollback(pane);

    /* A page is a screenful less a row, so one line of context carries over. */
    pane->scrollPages(1);
    const int afterOnePage = pane->viewOffset();
    check::that(afterOnePage > Config::instance().scrollMultiplier(),
                "a page is more than a wheel notch");

    pane->scrollPages(1);
    check::equal(pane->viewOffset(), 2 * afterOnePage, "two pages is twice as far");

    pane->scrollToTop();
    check::equal(pane->viewOffset(), pane->historySize(),
                 "scrolling to the top stops at the oldest line kept");

    pane->scrollToBottom();
    check::equal(pane->viewOffset(), 0, "and back to the live screen");

    pane->clearScrollback();
    check::equal(pane->historySize(), 0, "clearing discards the history");
    check::equal(pane->viewOffset(), 0, "leaving the view on the live screen");
    wheel(pane, 120);
    check::equal(pane->viewOffset(), 0, "with nothing left to scroll to");

    delete root;
    settle(50);
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
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta, true);
    /* The same as main(): with contexts shared, reparenting a pane no longer
     * tears its GL context down, and these suites have to exercise what
     * actually ships rather than a configuration nothing runs in. */
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, true);

    QApplication app(argc, argv);

    if (!openGlAvailable()) {
        std::printf("test_scroll_gl: skipped - no OpenGL 3.3 context on this machine\n");
        return 0;
    }

    /* Never read the developer's own configuration. */
    QTemporaryDir home;
    if (!home.isValid()) {
        std::printf("could not create a temporary HOME\n");
        return 1;
    }
    qputenv("HOME", home.path().toUtf8());
    Config::instance().load();

    testWheelMovesTheView();
    testPageAndClear();
    return check::report("test_scroll_gl");
}

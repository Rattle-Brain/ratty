/*
 * Selection and search through the widget, which needs a real OpenGL context.
 *
 * `test_selection` covers the model and `test_search` the matching, but neither
 * can reach what a user actually does: press, drag and release over a pane, and
 * find the text on the clipboard afterwards. That path only exists once the
 * pane has a session and a grid layout, which needs a context -- the same
 * reason `test_scroll_gl` and `test_splits_gl` exist.
 *
 * Skipped, not failed, when no context can be created, so a headless CI run
 * stays green while a developer machine still exercises it.
 */

#include "check.h"
#include "config/config.h"
#include "render/terminal_renderer.h"
#include "ui/split_container.h"
#include "ui/terminal_canvas.h"
#include "ui/terminal_widget.h"
#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QTimer>
#include <cmath>

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

void type(QWidget* target, const QString& text) {
    QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QApplication::sendEvent(target, &event);
}

void pressKey(QWidget* target, int key, const QString& text = QString(),
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    QApplication::sendEvent(target, &event);
}

void pressReturn(QWidget* target) {
    pressKey(target, Qt::Key_Return, QStringLiteral("\r"));
}

/*
 * The same layout the pane computes for itself, from the same inputs: the
 * canvas's font metrics, the pane's size in physical pixels and the configured
 * padding. Duplicated here rather than exposed, because a test that has to be
 * told where a cell is would be testing an accessor rather than the maths.
 */
TerminalRenderer::Layout layoutFor(const TerminalWidget* pane) {
    TerminalCanvas* canvas = TerminalCanvas::forWidget(pane);
    GLRenderer* renderer = canvas ? canvas->renderer() : nullptr;
    if (!renderer || !renderer->hasFont()) return TerminalRenderer::Layout{};

    const double scale = pane->devicePixelRatioF() > 0 ? pane->devicePixelRatioF() : 1.0;
    return TerminalRenderer::computeLayout(
        renderer->fontMetrics(),
        static_cast<int>(std::lround(pane->width() * scale)),
        static_cast<int>(std::lround(pane->height() * scale)),
        static_cast<int>(std::lround(Config::instance().windowPadding() * scale)));
}

/* The middle of a cell, in the logical pixels Qt delivers mouse events in. */
QPointF cellCentre(const TerminalWidget* pane, const TerminalRenderer::Layout& layout,
                   int row, int col) {
    const double scale = pane->devicePixelRatioF() > 0 ? pane->devicePixelRatioF() : 1.0;
    return QPointF((layout.originX + (col + 0.5) * layout.cellWidth) / scale,
                   (layout.originY + (row + 0.5) * layout.cellHeight) / scale);
}

void sendMouse(TerminalWidget* pane, QEvent::Type type, const QPointF& local,
               Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, local, pane->mapToGlobal(local), button, buttons, modifiers);
    QApplication::sendEvent(pane, &event);
}

/* Press, drag, release: one selection. */
void drag(TerminalWidget* pane, const QPointF& from, const QPointF& to,
          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    sendMouse(pane, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
    sendMouse(pane, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, modifiers);
    sendMouse(pane, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
}

void clickTimes(TerminalWidget* pane, const QPointF& at, int times) {
    for (int i = 0; i < times; ++i) {
        /* Qt sends the second click of a double-click as its own type, and the
         * pane counts clicks itself; both arrive here as presses, which is what
         * countClick() is written against. */
        sendMouse(pane, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        sendMouse(pane, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    }
}

TerminalWidget* preparePane(SplitContainer* root) {
    root->resize(900, 500);
    root->show();
    settle(1200);

    TerminalWidget* pane = root->terminal();
    if (!pane || pane->shellPid() <= 0) return nullptr;

    /* The window is real and on screen, so the developer's own mouse would
     * otherwise reach it; sendEvent() bypasses hit-testing. */
    pane->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    return pane;
}

/*
 * Put a known line at the top of the screen. The escape sequence clears and
 * homes first, so where the text lands does not depend on what the shell's
 * prompt looks like.
 */
void writeKnownLine(TerminalWidget* pane) {
    type(pane, QStringLiteral("printf '\\033[2J\\033[Halpha beta gamma\\n'"));
    pressReturn(pane);
    settle(1200);
}

QString clipboardText() {
    const QClipboard* clipboard = QApplication::clipboard();
    return clipboard ? clipboard->text(QClipboard::Clipboard) : QString();
}

void clearClipboard() {
    if (QClipboard* clipboard = QApplication::clipboard()) {
        clipboard->setText(QStringLiteral("<none>"), QClipboard::Clipboard);
    }
}

void testDragSelectsAndCopies() {
    check::section("press, drag, release, copy");

    SplitContainer* root = SplitContainer::createLeaf(nullptr);
    TerminalWidget* pane = preparePane(root);
    if (!pane) {
        check::that(false, "the pane started a shell");
        delete root;
        return;
    }

    const TerminalRenderer::Layout layout = layoutFor(pane);
    if (!layout.isValid()) {
        check::that(false, "the pane has a grid layout");
        delete root;
        return;
    }

    writeKnownLine(pane);
    check::that(!pane->hasSelection(), "nothing is selected to begin with");

    /* "alpha" is columns 0 to 4 of the top row. */
    clearClipboard();
    drag(pane, cellCentre(pane, layout, 0, 0), cellCentre(pane, layout, 0, 4));
    check::that(pane->hasSelection(), "a drag selects");
    pane->copySelection();
    check::equal(clipboardText().toStdString(), std::string("alpha"),
                 "and the selected text reaches the clipboard");

    /* A plain click clears it again. */
    clickTimes(pane, cellCentre(pane, layout, 0, 12), 1);
    check::that(!pane->hasSelection(), "a click that does not move clears the selection");

    /* Double click takes a word, triple click the line. */
    clearClipboard();
    clickTimes(pane, cellCentre(pane, layout, 0, 7), 2);
    pane->copySelection();
    check::equal(clipboardText().toStdString(), std::string("beta"),
                 "a double click selects the word under it");

    clearClipboard();
    clickTimes(pane, cellCentre(pane, layout, 0, 2), 3);
    pane->copySelection();
    check::equal(clipboardText().toStdString(), std::string("alpha beta gamma"),
                 "a third click takes the whole line, without its padding");

    /* Alt is the rectangular modifier: two columns of one row here, which is
     * enough to show the mode reached the selection. */
    clearClipboard();
    drag(pane, cellCentre(pane, layout, 0, 6), cellCentre(pane, layout, 0, 9),
         Qt::AltModifier);
    pane->copySelection();
    check::equal(clipboardText().toStdString(), std::string("beta"),
                 "an Alt drag selects a rectangle");

    /* Typing drops the selection, as it does in every terminal. */
    type(pane, QStringLiteral("x"));
    check::that(!pane->hasSelection(), "typing clears the selection");
    pressKey(pane, Qt::Key_Backspace, QStringLiteral("\b"));
    settle(200);

    delete root;
}

void testSearchFindsAndSelects() {
    check::section("scrollback search");

    SplitContainer* root = SplitContainer::createLeaf(nullptr);
    TerminalWidget* pane = preparePane(root);
    if (!pane) {
        check::that(false, "the pane started a shell");
        delete root;
        return;
    }

    type(pane, QStringLiteral("seq 1 400"));
    pressReturn(pane);
    settle(1800);
    check::that(pane->historySize() > 100, "there is a scrollback to search");
    check::equal(pane->viewOffset(), 0, "and the view is live");

    pane->beginSearch();
    check::that(pane->searchActive(), "the prompt is open");

    /* "42" appears in 42, 142, 242 and 342; the newest is what a search jumps
     * to first, and it is far enough back to move the view. */
    type(pane, QStringLiteral("4"));
    type(pane, QStringLiteral("2"));
    settle(200);
    check::that(pane->viewOffset() > 0, "the match scrolled into view");
    check::that(pane->hasSelection(), "and is selected");

    clearClipboard();
    pane->copySelection();
    check::equal(clipboardText().toStdString(), std::string("42"),
                 "so it can be copied straight out of the search");

    /* Escape closes the prompt and leaves the match selected. */
    const int found = pane->viewOffset();
    pressKey(pane, Qt::Key_Escape);
    check::that(!pane->searchActive(), "escape closes the prompt");
    check::that(pane->hasSelection(), "leaving the match selected");
    check::equal(pane->viewOffset(), found, "and the view where it was");

    /* find_previous steps to an older match, which is further back. */
    pane->findPrevious();
    settle(100);
    check::that(pane->viewOffset() > found, "find_previous moves further back");

    /* Typing returns to the live screen, which is also the way out of a search. */
    type(pane, QStringLiteral("x"));
    settle(200);
    check::equal(pane->viewOffset(), 0, "typing snaps the view back to the live screen");
    pressKey(pane, Qt::Key_Backspace, QStringLiteral("\b"));
    settle(200);

    delete root;
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
        std::printf("test_select_gl: skipped (no OpenGL context available)\n");
        return 0;
    }

    Config::instance().load();
    testDragSelectsAndCopies();
    testSearchFindsAndSelects();
    return check::report("test_select_gl");
}

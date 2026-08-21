/*
 * Split tests that need a real OpenGL context.
 *
 * `test_splits` covers the tree surgery, but it runs offscreen where
 * QOpenGLWidget cannot create a context -- so it structurally could not catch
 * the bug this suite exists for: reparenting a QOpenGLWidget destroys its
 * context and calls initializeGL() a second time. Anything built there that is
 * not a GL resource gets rebuilt too, and TerminalWidget was rebuilding its
 * session, which killed the running shell on every split.
 *
 * Contexts are shared now (Qt::AA_ShareOpenGLContexts, set in main() both here
 * and in the application), which is what stops the teardown happening at all --
 * and is why a split is fast. The assertions below hold either way, and are kept
 * because the guard in initializeGL() that they cover is still the thing
 * standing between a reparent and a dead shell.
 *
 * Skipped, not failed, when no context can be created, so a headless CI run
 * stays green while a developer machine still exercises it.
 */

#include "check.h"
#include "config/config.h"
#include "ui/main_window.h"
#include "ui/split_container.h"
#include "ui/terminal_widget.h"
#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QMouseEvent>
#include <QDir>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

/* True when this machine can actually give us an OpenGL 3.3 core context. */
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

/*
 * Distinct colours in a pane, capped. A pane that is drawing nothing is one flat
 * colour; a pane with a prompt on it is many.
 */
int distinctColours(const QImage& image, int cap = 40) {
    std::vector<QRgb> seen;
    for (int y = 0; y < image.height(); y += 3) {
        for (int x = 0; x < image.width(); x += 3) {
            const QRgb pixel = image.pixel(x, y);
            if (std::find(seen.begin(), seen.end(), pixel) == seen.end()) {
                seen.push_back(pixel);
                if (static_cast<int>(seen.size()) >= cap) return cap;
            }
        }
    }
    return static_cast<int>(seen.size());
}

std::vector<TerminalWidget*> panesOf(SplitContainer* root) {
    std::vector<TerminalWidget*> panes;
    root->forEachLeaf([&](SplitContainer* leaf) {
        if (leaf->terminal()) panes.push_back(leaf->terminal());
    });
    return panes;
}

/* A left-click in the middle of a pane, the way a user picks one. */
void clickPane(SplitContainer* pane) {
    if (!pane || !pane->terminal()) return;
    TerminalWidget* terminal = pane->terminal();
    const QPointF centre(terminal->width() / 2.0, terminal->height() / 2.0);
    QMouseEvent press(QEvent::MouseButtonPress, centre, terminal->mapToGlobal(centre),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, centre, terminal->mapToGlobal(centre),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(terminal, &press);
    QApplication::sendEvent(terminal, &release);
}

/* Let the event loop run and shells produce a prompt. */
void settle(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

void testSplitKeepsTheRunningShell() {
    check::section("splitting a pane keeps its shell and keeps drawing");

    SplitContainer* root = SplitContainer::createLeaf(nullptr);
    root->resize(900, 500);
    root->show();
    settle(1200);

    TerminalWidget* original = root->terminal();
    const pid_t pidBefore = original->shellPid();
    check::that(pidBefore > 0, "the pane started a shell");
    check::that(distinctColours(original->grabFramebuffer()) > 3,
                "and is drawing something before the split");

    SplitContainer* newRoot = root->splitHorizontal();
    check::that(newRoot != nullptr, "the split produced a root");
    if (!newRoot) return;
    newRoot->resize(900, 500);
    newRoot->show();
    settle(1200);

    /*
     * The heart of it. Reparenting destroyed and recreated the GL context, so
     * initializeGL() ran again; the pty and the shell have nothing to do with
     * that context and must survive it.
     */
    check::equal(static_cast<int>(original->shellPid()), static_cast<int>(pidBefore),
                 "the original pane kept the same shell after the split");

    const std::vector<TerminalWidget*> panes = panesOf(newRoot);
    check::equal(static_cast<int>(panes.size()), 2, "there are two panes");

    for (size_t index = 0; index < panes.size(); ++index) {
        const std::string label = "pane " + std::to_string(index + 1);
        TerminalWidget* pane = panes[index];

        check::that(pane->shellPid() > 0, label + " has a running shell");
        /* A blank pane is the reported symptom, so this is the assertion that
         * matters most: the renderer was rebuilt on the new context and works. */
        check::that(distinctColours(pane->grabFramebuffer()) > 3,
                    label + " is drawing text rather than a blank surface");
    }

    if (panes.size() == 2) {
        check::that(panes[0]->shellPid() != panes[1]->shellPid(),
                    "the two panes run different shells");
    }

    delete newRoot;
    settle(50);
}

void testSecondSplitAndClose() {
    check::section("a second split, and closing back down");

    SplitContainer* root = SplitContainer::createLeaf(nullptr);
    root->resize(1000, 600);
    root->show();
    settle(1000);

    const pid_t firstPid = root->terminal()->shellPid();

    SplitContainer* afterFirst = root->splitHorizontal();
    if (!afterFirst) { check::that(false, "first split failed"); return; }
    afterFirst->resize(1000, 600);
    afterFirst->show();
    settle(900);

    /* Split again: the panes get reparented a second time. */
    std::vector<TerminalWidget*> panes = panesOf(afterFirst);
    SplitContainer* target = nullptr;
    afterFirst->forEachLeaf([&](SplitContainer* leaf) { if (!target) target = leaf; });
    SplitContainer* afterSecond = target ? target->splitVertical() : nullptr;
    check::that(afterSecond != nullptr, "the second split succeeded");
    if (!afterSecond) return;
    afterSecond->resize(1000, 600);
    afterSecond->show();
    settle(900);

    panes = panesOf(afterSecond);
    check::equal(static_cast<int>(panes.size()), 3, "there are three panes");

    std::vector<pid_t> pids;
    for (TerminalWidget* pane : panes) {
        pids.push_back(pane->shellPid());
        check::that(pane->shellPid() > 0, "every pane still has a shell");
        check::that(distinctColours(pane->grabFramebuffer()) > 3,
                    "every pane is still drawing");
    }
    std::sort(pids.begin(), pids.end());
    check::that(std::adjacent_find(pids.begin(), pids.end()) == pids.end(),
                "all three shells are distinct");
    check::that(std::find(pids.begin(), pids.end(), firstPid) != pids.end(),
                "the very first shell is still among them");

    /* Closing also reparents the survivor, so it must survive that too. */
    SplitContainer* toClose = nullptr;
    afterSecond->forEachLeaf([&](SplitContainer* leaf) { if (!toClose) toClose = leaf; });
    const pid_t closingPid = toClose ? toClose->terminal()->shellPid() : -1;

    SplitContainer* afterClose = toClose ? toClose->closePane() : nullptr;
    check::that(afterClose != nullptr, "closing a pane produced a root");
    if (!afterClose) return;
    afterClose->resize(1000, 600);
    afterClose->show();
    settle(900);

    panes = panesOf(afterClose);
    check::equal(static_cast<int>(panes.size()), 2, "two panes remain");
    for (TerminalWidget* pane : panes) {
        check::that(pane->shellPid() > 0, "a surviving pane kept its shell");
        check::that(pane->shellPid() != closingPid,
                    "and it is not the shell that was closed");
        check::that(distinctColours(pane->grabFramebuffer()) > 3,
                    "a surviving pane is still drawing");
    }

    delete afterClose;
    settle(50);
}

/*
 * The same operations again, but driven through MainWindow with real key events.
 *
 * This is the path the application actually takes, and it is not the same as
 * calling SplitContainer directly: in the app a pane lives inside a QTabWidget,
 * so promoting a new root reparents the old page out of the tab widget's stacked
 * layout -- and QTabWidget answers a page leaving by removing its tab. Testing
 * SplitContainer on its own could never see that, which is why splits worked in
 * the earlier test and blanked the window in the real program.
 */

int paneCount(MainWindow& window) {
    SplitContainer* root = window.currentRoot();
    if (!root) return 0;
    int count = 0;
    root->forEachLeaf([&](SplitContainer*) { ++count; });
    return count;
}

std::vector<TerminalWidget*> windowPanes(MainWindow& window) {
    std::vector<TerminalWidget*> panes;
    if (SplitContainer* root = window.currentRoot()) {
        root->forEachLeaf([&](SplitContainer* leaf) {
            if (leaf->terminal()) panes.push_back(leaf->terminal());
        });
    }
    return panes;
}

/* Deliver a shortcut the way the window receives it from the user. */
void sendShortcut(MainWindow& window, Qt::KeyboardModifiers modifiers, Qt::Key key) {
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(&window, &press);
}

void testSplittingThroughTheWindow() {
    check::section("splitting through MainWindow keeps the window populated");

    MainWindow window;
    window.resize(1000, 620);
    window.show();
    settle(1400);

    check::equal(window.tabCount(), 1, "one tab to start with");
    check::equal(paneCount(window), 1, "holding one pane");

    const std::vector<TerminalWidget*> before = windowPanes(window);
    const pid_t firstPid = before.empty() ? -1 : before.front()->shellPid();
    check::that(firstPid > 0, "with a running shell");

    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

    /* Ctrl+Shift+W is split_horizontal in the shipped defaults. */
    sendShortcut(window, ctrlShift, Qt::Key_W);
    settle(1400);

    /*
     * The failure this guards against: the tab count dropping to zero, leaving
     * the tab widget with no page and the window blank while the shell kept
     * running behind it.
     */
    check::equal(window.tabCount(), 1, "the tab survived the split");
    check::that(window.currentRoot() != nullptr, "the tab still has a page");
    check::equal(paneCount(window), 2, "and the page holds two panes");

    std::vector<TerminalWidget*> panes = windowPanes(window);
    check::equal(static_cast<int>(panes.size()), 2, "two terminals exist");

    bool foundOriginal = false;
    for (TerminalWidget* pane : panes) {
        check::that(pane->isVisible(), "each pane is visible");
        check::that(pane->width() > 0 && pane->height() > 0, "each pane has a size");
        check::that(pane->shellPid() > 0, "each pane has a shell");
        check::that(distinctColours(pane->grabFramebuffer()) > 3,
                    "each pane is drawing rather than blank");
        if (pane->shellPid() == firstPid) foundOriginal = true;
    }
    check::that(foundOriginal, "the original shell is still one of them");

    /* Split again, this time top/bottom. */
    sendShortcut(window, ctrlShift, Qt::Key_V);
    settle(1300);
    check::equal(window.tabCount(), 1, "the tab survived a second split");
    check::equal(paneCount(window), 3, "three panes now");
    for (TerminalWidget* pane : windowPanes(window)) {
        check::that(pane->shellPid() > 0, "every pane still has a shell");
        check::that(distinctColours(pane->grabFramebuffer()) > 3,
                    "every pane is still drawing");
    }

    /* Ctrl+Shift+C closes the focused pane. */
    sendShortcut(window, ctrlShift, Qt::Key_C);
    settle(1300);
    check::equal(window.tabCount(), 1, "the tab survived a close");
    check::equal(paneCount(window), 2, "two panes remain");
    for (TerminalWidget* pane : windowPanes(window)) {
        check::that(pane->shellPid() > 0, "a survivor still has its shell");
        check::that(distinctColours(pane->grabFramebuffer()) > 3,
                    "a survivor is still drawing");
    }

    /* Closing back down to one must leave a working single pane. */
    sendShortcut(window, ctrlShift, Qt::Key_C);
    settle(1300);
    check::equal(window.tabCount(), 1, "still one tab");
    check::equal(paneCount(window), 1, "back to a single pane");
    const std::vector<TerminalWidget*> last = windowPanes(window);
    if (!last.empty()) {
        check::that(last.front()->shellPid() > 0, "which has a shell");
        check::that(distinctColours(last.front()->grabFramebuffer()) > 3,
                    "and is drawing");
    }
}

void testSplittingASecondTab() {
    check::section("splits in a second tab");

    MainWindow window;
    window.resize(1000, 620);
    window.show();
    settle(1300);

    const auto meta = Qt::MetaModifier;
    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

    sendShortcut(window, meta, Qt::Key_T);      // new tab
    settle(1300);
    check::equal(window.tabCount(), 2, "a second tab opened");

    sendShortcut(window, ctrlShift, Qt::Key_W); // split it
    settle(1300);
    check::equal(window.tabCount(), 2, "both tabs survived the split");
    check::equal(paneCount(window), 2, "the current tab has two panes");

    /* The first tab must be untouched by surgery in the second. */
    SplitContainer* first = window.rootAt(0);
    check::that(first != nullptr, "the first tab still has a page");
    if (first) {
        int firstTabPanes = 0;
        first->forEachLeaf([&](SplitContainer*) { ++firstTabPanes; });
        check::equal(firstTabPanes, 1, "and still holds exactly one pane");
    }

    for (TerminalWidget* pane : windowPanes(window)) {
        check::that(pane->shellPid() > 0, "each pane in tab 2 has a shell");
        check::that(distinctColours(pane->grabFramebuffer()) > 3,
                    "each pane in tab 2 is drawing");
    }
}

/*
 * Where the caret goes. Splitting used to leave it on the pane the user had just
 * split away from, and closing a split left it nowhere at all -- both because
 * installTabRoot() reparents the tree into the tab widget's stacked layout,
 * which clears Qt focus after the surgery has carefully set it.
 */
void testFocusFollowsSplitsAndCloses() {
    check::section("focus follows a new split, and returns to where it came from");

    MainWindow window;
    window.resize(1000, 620);
    window.show();
    window.activateWindow();
    settle(1400);

    /*
     * Two things are asserted for each step, because either one alone can be
     * right while the pane is still unusable: the marker (RaTTY's own record of
     * the current pane, which every action falls back to) and the window's focus
     * widget (where a keystroke would actually land).
     *
     * focusWidget() rather than hasFocus(): the latter also requires the window
     * to be *active*, which an automated run cannot insist on, while the focus
     * widget is set either way.
     */
    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

    SplitContainer* first = window.currentRoot();
    check::that(first != nullptr && first->isLeaf(), "one pane to start with");
    if (!first) return;

    sendShortcut(window, ctrlShift, Qt::Key_W);      // split left/right
    settle(1300);

    SplitContainer* root = window.currentRoot();
    SplitContainer* second = root ? root->child2() : nullptr;
    check::that(second != nullptr && second->isLeaf(), "a second pane appeared");
    check::that(root && root->findMarkedPane() == second, "the new pane is the current one");
    check::that(second && window.focusWidget() == second->terminal(),
                "and a keystroke would land in it");

    sendShortcut(window, ctrlShift, Qt::Key_V);      // split that one top/bottom
    settle(1300);

    root = window.currentRoot();
    SplitContainer* third = root ? root->findMarkedPane() : nullptr;
    check::that(third != nullptr && third != first && third != second,
                "the second split also focused the pane it created");
    check::that(third && window.focusWidget() == third->terminal(),
                "and keystrokes moved with it");

    sendShortcut(window, ctrlShift, Qt::Key_C);      // close the focused pane
    settle(1300);

    root = window.currentRoot();
    check::equal(paneCount(window), 2, "two panes remain");
    check::that(root && root->findMarkedPane() == second,
                "closing it handed focus back to the pane it was opened from");
    check::that(second && window.focusWidget() == second->terminal(),
                "and that pane really has the caret -- no click needed");

    /* Clicking a pane is the other way focus moves, and it has to be recorded
     * as such: closing the pane clicked *into* must come back here. */
    clickPane(first);
    settle(300);
    check::that(root && root->findMarkedPane() == first, "clicking a pane makes it current");
    check::that(window.focusWidget() == first->terminal(), "and moves the caret to it");
}

/*
 * The distinguishing case for a focus *history*, as opposed to just picking a
 * survivor: the promoted sibling's leftmost leaf and the pane the user actually
 * came from are two different panes.
 */
void testFocusReturnsToThePaneLastUsed() {
    check::section("closing a pane returns to the last one used, not the nearest");

    MainWindow window;
    window.resize(1000, 620);
    window.show();
    settle(1400);

    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

    SplitContainer* a = window.currentRoot();
    check::that(a != nullptr && a->isLeaf(), "one pane to start with");
    if (!a) return;

    sendShortcut(window, ctrlShift, Qt::Key_W);   /* a | b */
    settle(1300);
    clickPane(a);                                 /* go back to a */
    settle(300);
    sendShortcut(window, ctrlShift, Qt::Key_W);   /* (a | c) | b */
    settle(1300);

    SplitContainer* root = window.currentRoot();
    SplitContainer* left = root ? root->child1() : nullptr;
    SplitContainer* b = root ? root->child2() : nullptr;
    SplitContainer* c = left ? left->child2() : nullptr;
    check::equal(paneCount(window), 3, "three panes");
    check::that(left && b && c && left->child1() == a, "the tree is (a | c) | b");
    if (!c || !b) return;

    /*
     * Work in c, then step over to b and close it. Closing b promotes the
     * (a | c) container, and the tree's own answer for "which pane now" is its
     * leftmost leaf -- a. The pane the user came from is c.
     */
    clickPane(c);
    settle(300);
    clickPane(b);
    settle(300);
    sendShortcut(window, ctrlShift, Qt::Key_C);
    settle(1300);

    root = window.currentRoot();
    check::equal(paneCount(window), 2, "two panes remain");
    check::that(root == left, "the surviving container was promoted to the tab");
    check::that(root && root->findMarkedPane() == c,
                "focus went to the pane last used, not the leftmost one");
    check::that(window.focusWidget() == c->terminal(), "and the caret is in it");
}

/*
 * Composed input. `~` on a Spanish keyboard is a dead key, as is every accent,
 * and none of them produce a key event carrying text -- the platform input
 * method composes the result and delivers it as a QInputMethodEvent, but only
 * to a widget that has asked for one. TerminalWidget had never asked, so the
 * tilde could not be typed at all.
 *
 * Whether the *platform* composes is beyond a test's reach; that the wiring is
 * there, and that a committed string reaches the shell, is not.
 */
void testComposedInputReachesTheShell() {
    check::section("composed input is wired up and reaches the shell");

    MainWindow window;
    window.resize(1000, 620);
    window.show();
    settle(1500);

    SplitContainer* pane = window.currentRoot();
    TerminalWidget* terminal = pane ? pane->terminal() : nullptr;
    check::that(terminal != nullptr, "a pane with a terminal");
    if (!terminal) return;

    check::that(terminal->testAttribute(Qt::WA_InputMethodEnabled),
                "the terminal accepts input methods");
    check::that(terminal->inputMethodQuery(Qt::ImEnabled).toBool(),
                "and says so when asked");

    /* Where a candidate window goes. The layout is in physical pixels and Qt
     * expects logical ones, so the wrong units park it in a corner. */
    const QRectF caret = terminal->inputMethodQuery(Qt::ImCursorRectangle).toRectF();
    check::that(caret.width() > 0 && caret.height() > 0, "the caret rectangle has a size");
    check::that(QRectF(terminal->rect()).contains(caret),
                "and sits inside the pane, in logical pixels");

    /*
     * End to end: split first, so there is a second pane to survive it, then
     * commit `exit` into this one the way a composition would. Nothing but the
     * shell can act on that, so the pane closing is proof the bytes arrived.
     */
    sendShortcut(window, Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_W);
    settle(1500);
    check::equal(paneCount(window), 2, "two panes before the composition");

    SplitContainer* victim = window.currentRoot() ? window.currentRoot()->child2() : nullptr;
    check::that(victim != nullptr, "the new pane is the current one");
    if (!victim || !victim->terminal()) return;

    QInputMethodEvent composed;
    composed.setCommitString(QStringLiteral("exit\r"));
    QApplication::sendEvent(victim->terminal(), &composed);
    settle(2000);

    check::equal(paneCount(window), 1,
                 "the composed string reached the shell, which exited");
}

} // namespace

/*
 * The font on screen has to match the display it is being drawn on.
 *
 * Dragging the window to a monitor with a different device pixel ratio changes
 * how many physical pixels a point is worth. Until the font is re-rasterized,
 * the atlas still holds glyphs sized for the old ratio and they are drawn one
 * texel per physical pixel -- so at 12 pt they came out around 24 logical
 * pixels on a 1x display after leaving a 2x one, while the configured size was
 * still 12. Pressing the shrink shortcut then jumped straight to a correct 11,
 * which is the visible symptom: what is shown and what is configured had
 * drifted apart.
 *
 * A real ratio change cannot be staged in-process, but the mechanism that fixes
 * it can: applyFontScale()'s inputs are the ratio, the logical DPI and the
 * configured size, and paintGL() now rebuilds the font whenever any of them has
 * moved since the last rasterization. Moving the configured size behind the
 * widget's back exercises exactly that recovery path.
 */
void testFontFollowsItsDisplay() {
    check::section("a stale font scale is rebuilt before the next frame");

    SplitContainer* root = SplitContainer::createLeaf(nullptr);
    root->resize(900, 500);
    root->show();
    settle(900);

    TerminalWidget* terminal = root->terminal();

    /* The cursor rectangle is one cell, reported in logical pixels -- the only
     * public view onto the grid's on-screen scale. */
    const auto cellSize = [terminal]() {
        return terminal->inputMethodQuery(Qt::ImCursorRectangle).toRectF().size();
    };

    const QSizeF before = cellSize();
    check::that(before.height() > 1.0, "a cell has a measurable height to begin with");

    const int originalSize = Config::instance().fontSize();

    /*
     * Deliberately *not* reloadFont(): that is the path the font-size shortcut
     * takes, and it was never the broken one. This leaves the widget holding a
     * font rasterized for a size the configuration no longer says, which is the
     * same inconsistency a display change produces.
     */
    Config::instance().setFontSize(originalSize * 2);
    terminal->grabFramebuffer();   // forces a paintGL()
    settle(120);

    const QSizeF after = cellSize();
    check::that(after.height() > before.height() * 1.5,
                "doubling the configured size grows the cell without a reload");

    /* And back, so the rest of the suite is unaffected. */
    Config::instance().setFontSize(originalSize);
    terminal->grabFramebuffer();
    settle(120);

    const QSizeF restored = cellSize();
    check::that(std::abs(restored.height() - before.height()) < 1.5,
                "and restoring it returns the cell to its original size");

    root->deleteLater();
    settle(120);
}

/*
 * Tab belongs to the shell.
 *
 * Qt's own QWidget::event() takes Key_Tab and Key_Backtab for focus traversal
 * *before* keyPressEvent() ever sees them, so in a split window Tab moved the
 * caret to the next pane instead of asking the shell to complete. With a single
 * pane there is nothing to move to, focusNextPrevChild() answers false and Tab
 * falls through -- which is why this only ever showed up once a pane was split.
 */
void testTabReachesTheShellRatherThanTheNextPane() {
    check::section("Tab goes to the shell, not to the next pane");

    MainWindow window;
    window.resize(1000, 620);
    window.show();
    window.activateWindow();
    settle(1400);

    const auto ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;
    sendShortcut(window, ctrlShift, Qt::Key_W);      // split left/right
    settle(1300);

    SplitContainer* root = window.currentRoot();
    SplitContainer* second = root ? root->child2() : nullptr;
    check::that(second != nullptr && second->terminal() != nullptr,
                "two panes, with the new one current");
    if (!second || !second->terminal()) return;

    TerminalWidget* focused = second->terminal();
    check::that(window.focusWidget() == focused, "the new pane holds the caret");

    /* Exactly what the platform delivers: a key event to the focus widget. */
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
    QCoreApplication::sendEvent(focused, &tab);
    settle(200);
    check::that(window.focusWidget() == focused, "Tab left the caret where it was");

    /* Shift+Tab arrives as Key_Backtab and must not walk backwards either. */
    QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
    QCoreApplication::sendEvent(focused, &backtab);
    settle(200);
    check::that(window.focusWidget() == focused, "and neither did Shift+Tab");
}

/*
 * Where a new pane's shell starts, end to end.
 *
 * This exercises the whole chain rather than the settings: PTY chdir()s before
 * exec, and workingDirectory() asks the operating system where the shell got to
 * -- /proc on Linux, proc_pidinfo on macOS -- which is what lets a split open
 * where the pane it came from is.
 */
void testPanesStartInTheRequestedDirectory() {
    check::section("a pane starts where it was told, and a split can follow it");

    /* Two real directories inside the sandbox, resolved through QDir so that a
     * symlinked /tmp (which macOS has) does not make the comparison fail. */
    QTemporaryDir scratch;
    check::that(scratch.isValid(), "a scratch directory to start shells in");
    if (!scratch.isValid()) return;

    const QString first = QFileInfo(scratch.path()).canonicalFilePath();
    const QString second = first + QStringLiteral("/nested");
    check::that(QDir().mkpath(second), "and a second one below it");

    SplitContainer* root = SplitContainer::createLeaf(nullptr, first);
    root->resize(900, 500);
    root->show();
    settle(1500);

    TerminalWidget* original = root->terminal();
    check::that(original != nullptr && original->shellPid() > 0, "the pane started a shell");
    if (!original) return;

    const QString reported = QFileInfo(original->workingDirectory()).canonicalFilePath();
    check::equal(reported.toStdString(), first.toStdString(),
                 "and that shell is in the directory it was given");

    /* A split told to inherit lands in the same place. */
    SplitContainer* inheritingPane = nullptr;
    SplitContainer* afterInherit =
        root->splitHorizontal(0.5f, &inheritingPane, original->workingDirectory());
    check::that(afterInherit != nullptr && inheritingPane != nullptr, "the split happened");
    if (!afterInherit || !inheritingPane) return;
    afterInherit->resize(900, 500);
    afterInherit->show();
    settle(1500);

    TerminalWidget* inherited = inheritingPane->terminal();
    check::that(inherited != nullptr && inherited->shellPid() > 0, "the new pane has a shell");
    if (inherited) {
        check::equal(QFileInfo(inherited->workingDirectory()).canonicalFilePath().toStdString(),
                     first.toStdString(),
                     "which inherited the directory of the pane it came from");
    }

    /* And a split told somewhere else goes there instead, which is the same code
     * path a configured path or `home` takes. */
    SplitContainer* elsewherePane = nullptr;
    SplitContainer* afterElsewhere =
        inheritingPane->splitVertical(0.5f, &elsewherePane, second);
    check::that(afterElsewhere != nullptr && elsewherePane != nullptr,
                "a second split happened");
    if (afterElsewhere && elsewherePane) {
        afterElsewhere->resize(900, 500);
        afterElsewhere->show();
        settle(1500);
        TerminalWidget* elsewhere = elsewherePane->terminal();
        if (elsewhere) {
            check::equal(
                QFileInfo(elsewhere->workingDirectory()).canonicalFilePath().toStdString(),
                second.toStdString(),
                "and a split pointed elsewhere starts there instead");
        }
    }

    (afterElsewhere ? afterElsewhere : afterInherit)->deleteLater();
    settle(300);
}

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
        std::printf("test_splits_gl: skipped - no OpenGL 3.3 context on this machine\n");
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

    testSplitKeepsTheRunningShell();
    testSecondSplitAndClose();
    testSplittingThroughTheWindow();
    testSplittingASecondTab();
    testFocusFollowsSplitsAndCloses();
    testFocusReturnsToThePaneLastUsed();
    testComposedInputReachesTheShell();
    testFontFollowsItsDisplay();
    testTabReachesTheShellRatherThanTheNextPane();
    testPanesStartInTheRequestedDirectory();
    return check::report("test_splits_gl");
}

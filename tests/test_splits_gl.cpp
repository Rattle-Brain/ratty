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
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
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

    /* Ctrl+Shift+W is split_vertical in the shipped defaults. */
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

    /* Split again, this time left/right. */
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
    return check::report("test_splits_gl");
}

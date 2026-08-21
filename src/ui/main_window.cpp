/*
 * MainWindow - top-level window implementation
 */

#include "main_window.h"
#include "terminal_canvas.h"
#include <QVBoxLayout>
#include "split_container.h"
#include "tab_bar.h"
#include "terminal_widget.h"
#include <QApplication>
#include <QDebug>
#include <QKeyCombination>
#include <QKeyEvent>
#include <QTabBar>
#include <algorithm>
#include <QTabWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi();
    addTab();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    const Config& config = Config::instance();

    setWindowTitle(QStringLiteral("Ratty"));
    resize(config.windowWidth(), config.windowHeight());
    setWindowOpacity(config.windowOpacity());
    if (config.startFullscreen()) {
        setWindowState(Qt::WindowFullScreen);
    }

    auto* tabs = new TabWidget(this);
    tabWidget_ = tabs;
    tabWidget_->setDocumentMode(true);
    /* The close affordance and the tab painting are the custom bar's job; Qt's
     * own closable-tab buttons are platform-styled widgets that do not fit a bar
     * this thin. */
    tabWidget_->setTabsClosable(false);

    tabBar_ = tabs->rattyTabBar();
    connect(tabBar_, &TabBar::tabCloseClicked, this, &MainWindow::closeTab);
    /* Switching tabs has to land on the pane that tab was left on, or the user
     * has to click to get a cursor back. */
    connect(tabWidget_, &QTabWidget::currentChanged, this, &MainWindow::onCurrentTabChanged);

    /*
     * The tab widget goes inside a plain host, and the shared GPU surface is
     * added as its *sibling*, stacked over the page area.
     *
     * A sibling rather than a child of the tab widget on purpose: the canvas
     * hands mouse events back to whatever widget is underneath, and it finds
     * that widget by asking the tab widget what is at the point. Were the
     * canvas one of its children, it would find itself.
     */
    auto* host = new QWidget(this);
    auto* hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);
    hostLayout->addWidget(tabWidget_);
    setCentralWidget(host);

    /*
     * No canvas on a platform that cannot give us a GL context. Panes then draw
     * nothing, which is what the offscreen test runs expect, rather than
     * failing inside Qt's paint machinery.
     */
    if (TerminalCanvas::isSupported()) {
        canvas_ = new TerminalCanvas();
        canvas_->setPageProvider(tabWidget_);
        canvas_->createContainer(host);
    }

    applyTabBarConfiguration();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (canvas_) canvas_->syncGeometry();
}

void MainWindow::applyTabBarConfiguration() {
    if (!tabWidget_ || !tabBar_) return;

    const Config& config = Config::instance();

    tabWidget_->setTabPosition(config.tabBarPosition() == TabBarPosition::Bottom
                                   ? QTabWidget::South
                                   : QTabWidget::North);
    tabBar_->applyConfiguration();
    updateTabBarVisibility();
}

void MainWindow::updateTabBarVisibility() {
    if (!tabBar_) return;

    /* A single-terminal window should look like a terminal, not like a tabbed
     * document, so the default hides the bar until there is a second tab. */
    bool visible = true;
    switch (Config::instance().tabBarVisibility()) {
    case TabBarVisibility::Always:       visible = true; break;
    case TabBarVisibility::MultipleTabs: visible = tabCount() > 1; break;
    case TabBarVisibility::Never:        visible = false; break;
    }
    tabBar_->setVisible(visible);
}

QString MainWindow::currentPaneDirectory() const {
    /*
     * Asked of the pane the new one is being opened from, and only used when the
     * setting says to inherit. Empty is a perfectly good answer -- there may be
     * no pane yet, or the shell may have exited -- and StartDirectory::resolve()
     * falls back to $HOME for it.
     */
    const TerminalWidget* terminal = focusedTerminal();
    return terminal ? terminal->workingDirectory() : QString();
}

void MainWindow::addTab() {
    if (tabCount() >= MaxTabs) {
        qWarning() << "MainWindow: tab limit reached (" << MaxTabs << ")";
        return;
    }

    /* A new tab is a fresh piece of work, so by default it starts at $HOME
     * rather than wherever the current pane wandered off to. */
    const QString startDirectory =
        Config::instance().newTabDirectory().resolve(currentPaneDirectory());

    SplitContainer* root = SplitContainer::createLeaf(nullptr, startDirectory);
    connectRoot(root);

    const int index = tabWidget_->addTab(root, QStringLiteral("Terminal"));
    tabWidget_->setCurrentIndex(index);
    updateTabBarVisibility();
    giveFocusTo(root);
}

void MainWindow::connectRoot(SplitContainer* root) {
    if (!root) return;

    /* Both handlers are member functions rather than lambdas: Qt rejects
     * Qt::UniqueConnection for functors, and installTabRoot() may reconnect the
     * same root more than once. */
    connect(root, &SplitContainer::paneSessionEnded,
            this, &MainWindow::onPaneSessionEnded, Qt::UniqueConnection);
    connect(root, &SplitContainer::paneTitleChanged,
            this, &MainWindow::onPaneTitleChanged, Qt::UniqueConnection);
    connect(root, &SplitContainer::paneFocused,
            this, &MainWindow::onPaneFocused, Qt::UniqueConnection);
}

void MainWindow::onPaneTitleChanged(const QString& title) {
    auto* root = qobject_cast<SplitContainer*>(sender());
    if (!root || title.isEmpty()) return;

    const int index = tabWidget_->indexOf(root);
    if (index < 0) return;

    tabWidget_->setTabText(index, title);
    if (index == tabWidget_->currentIndex()) {
        setWindowTitle(title + QStringLiteral(" - Ratty"));
    }
}

QString MainWindow::tabLabel(int index) const {
    if (!tabWidget_ || index < 0 || index >= tabWidget_->count()) {
        return QStringLiteral("Terminal");
    }
    const QString label = tabWidget_->tabText(index);
    return label.isEmpty() ? QStringLiteral("Terminal") : label;
}

void MainWindow::installTabRoot(int index, SplitContainer* root, const QString& label) {
    if (!tabWidget_ || !root) return;

    /* Already the page: splitting a nested pane leaves the root untouched. */
    if (const int existing = tabWidget_->indexOf(root); existing >= 0) {
        tabWidget_->setCurrentIndex(existing);
        updateTabBarVisibility();
        return;
    }

    /*
     * The tab may well be gone by now, and that is not an error.
     *
     * When the pane being split *is* the tab's page, the tree surgery reparents
     * it under a new container -- which takes it out of the tab widget's stacked
     * layout, and QTabWidget answers a page leaving by removing its tab. So the
     * count can legitimately have dropped to zero between the split and this
     * call. Guarding on `index >= tabCount()` and returning, as this used to,
     * left the tab widget with no page at all: a blank window, with the shell
     * still running behind it.
     */
    if (index >= 0 && index < tabWidget_->count()) {
        auto* occupant = qobject_cast<SplitContainer*>(tabWidget_->widget(index));
        /* A node that now has a parent has been absorbed into the new tree, so
         * its tab slot is stale. Anything else belongs to another tab. */
        if (occupant && occupant->parentNode() != nullptr) {
            tabWidget_->removeTab(index);
        }
    }

    const int at = std::clamp(index, 0, tabWidget_->count());
    tabWidget_->insertTab(at, root, label);
    connectRoot(root);
    tabWidget_->setCurrentIndex(at);
    updateTabBarVisibility();
}

void MainWindow::closeTab(int index) {
    if (index < 0 || index >= tabCount()) return;

    QWidget* page = tabWidget_->widget(index);
    tabWidget_->removeTab(index);
    if (page) page->deleteLater();

    /* Closing the last tab closes the window, which is what every terminal
     * does; leaving an empty frame behind would be a dead end. */
    if (tabCount() == 0) {
        close();
        return;
    }
    updateTabBarVisibility();
}

int MainWindow::tabCount() const {
    return tabWidget_ ? tabWidget_->count() : 0;
}

SplitContainer* MainWindow::currentRoot() const {
    return tabWidget_ ? qobject_cast<SplitContainer*>(tabWidget_->currentWidget()) : nullptr;
}

SplitContainer* MainWindow::rootAt(int index) const {
    return tabWidget_ ? qobject_cast<SplitContainer*>(tabWidget_->widget(index)) : nullptr;
}

int MainWindow::indexOfRootContaining(const SplitContainer* node) const {
    for (int i = 0; i < tabCount(); ++i) {
        if (SplitContainer* root = rootAt(i); root && root->contains(node)) {
            return i;
        }
    }
    return -1;
}

SplitContainer* MainWindow::focusedPane() const {
    SplitContainer* root = currentRoot();
    if (!root) return nullptr;

    if (SplitContainer* focused = root->findFocused()) return focused;

    /*
     * Nothing has Qt focus: the window may have just been restored, or a
     * reparenting may have dropped it. The tree's own marker outlives both, so
     * it is the better answer than "the first leaf" -- a shortcut aimed at the
     * pane the user is looking at would otherwise act on a different one.
     */
    if (SplitContainer* marked = root->findMarkedPane()) return marked;

    SplitContainer* leaf = root;
    while (leaf && !leaf->isLeaf()) leaf = leaf->child1();
    return leaf;
}

void MainWindow::giveFocusTo(SplitContainer* pane) {
    if (!pane || !pane->isLeaf()) return;

    rememberFocus(pane);
    pane->focusPane();
}

void MainWindow::rememberFocus(SplitContainer* pane) {
    if (!pane || !pane->isLeaf()) return;

    /* Prune as we go: a pane can be destroyed without this list hearing. */
    focusHistory_.removeIf([pane](const QPointer<SplitContainer>& entry) {
        return entry.isNull() || entry == pane;
    });
    focusHistory_.prepend(pane);
}

void MainWindow::forgetPane(const SplitContainer* pane) {
    focusHistory_.removeIf([pane](const QPointer<SplitContainer>& entry) {
        return entry.isNull() || entry == pane;
    });
}

void MainWindow::restoreFocusIn(SplitContainer* root) {
    if (!root) return;

    /*
     * Decide first and act afterwards. focusPane() reaches back here through
     * paneFocused -> rememberFocus(), which rewrites the very list being read.
     */
    SplitContainer* target = nullptr;
    for (const QPointer<SplitContainer>& entry : focusHistory_) {
        if (entry && entry->isLeaf() && root->contains(entry)) {
            target = entry;
            break;
        }
    }

    if (!target) target = root->findMarkedPane();
    if (!target) {
        target = root;
        while (target && !target->isLeaf()) target = target->child1();
    }
    giveFocusTo(target);
}

void MainWindow::onPaneFocused(SplitContainer* pane) {
    rememberFocus(pane);
}

void MainWindow::onCurrentTabChanged(int index) {
    restoreFocusIn(rootAt(index));
    /* A different page is showing, so the surface has to cover it and redraw
     * the panes it holds. The panes of the tab just left are still registered
     * but no longer visible, so they simply stop being drawn. */
    if (canvas_) {
        canvas_->syncGeometry();
        canvas_->update();
    }
}

TerminalWidget* MainWindow::focusedTerminal() const {
    SplitContainer* pane = focusedPane();
    return pane ? pane->terminal() : nullptr;
}

void MainWindow::onPaneSessionEnded(SplitContainer* pane) {
    if (!pane) return;

    const int index = indexOfRootContaining(pane);
    if (index < 0) return;
    const QString label = tabLabel(index);

    forgetPane(pane);

    /* A pane with no parent is the whole tab. */
    if (SplitContainer* newRoot = pane->closePane()) {
        installTabRoot(index, newRoot, label);
        restoreFocusIn(newRoot);
    } else {
        closeTab(index);
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (handleAction(Config::instance().lookupAction(event))) {
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

bool MainWindow::handleAction(Action action) {
    switch (action) {
    case ACTION_NEW_TAB:            addTab(); return true;
    case ACTION_CLOSE_TAB:          closeTab(tabWidget_->currentIndex()); return true;
    case ACTION_NEXT_TAB:
        if (tabCount() > 1) {
            tabWidget_->setCurrentIndex((tabWidget_->currentIndex() + 1) % tabCount());
        }
        return true;
    case ACTION_PREV_TAB:
        if (tabCount() > 1) {
            tabWidget_->setCurrentIndex(
                (tabWidget_->currentIndex() - 1 + tabCount()) % tabCount());
        }
        return true;

    case ACTION_GOTO_TAB_1:
    case ACTION_GOTO_TAB_2:
    case ACTION_GOTO_TAB_3:
    case ACTION_GOTO_TAB_4:
    case ACTION_GOTO_TAB_5:
    case ACTION_GOTO_TAB_6:
    case ACTION_GOTO_TAB_7:
    case ACTION_GOTO_TAB_8:
    case ACTION_GOTO_TAB_9:
        goToTab(action - ACTION_GOTO_TAB_1 + 1);
        return true;

    case ACTION_SPLIT_HORIZONTAL:   splitFocusedPane(true); return true;
    case ACTION_SPLIT_VERTICAL:     splitFocusedPane(false); return true;
    case ACTION_CLOSE_SPLIT:        closeFocusedPane(); return true;

    case ACTION_FOCUS_LEFT:         focusNeighbour(Qt::Horizontal, false); return true;
    case ACTION_FOCUS_RIGHT:        focusNeighbour(Qt::Horizontal, true); return true;
    case ACTION_FOCUS_UP:           focusNeighbour(Qt::Vertical, false); return true;
    case ACTION_FOCUS_DOWN:         focusNeighbour(Qt::Vertical, true); return true;

    case ACTION_QUIT:               close(); return true;
    case ACTION_FULLSCREEN:
        if (isFullScreen()) showNormal();
        else showFullScreen();
        return true;

    case ACTION_COPY:
        if (TerminalWidget* terminal = focusedTerminal()) terminal->copySelection();
        return true;
    case ACTION_PASTE:
        if (TerminalWidget* terminal = focusedTerminal()) terminal->paste();
        return true;

    case ACTION_INCREASE_FONT_SIZE: changeFontSize(+1); return true;
    case ACTION_DECREASE_FONT_SIZE: changeFontSize(-1); return true;
    case ACTION_RESET_FONT_SIZE:    changeFontSize(0); return true;

    case ACTION_SCROLL_UP:
        if (TerminalWidget* terminal = focusedTerminal()) terminal->scrollPages(+1);
        return true;
    case ACTION_SCROLL_DOWN:
        if (TerminalWidget* terminal = focusedTerminal()) terminal->scrollPages(-1);
        return true;
    case ACTION_CLEAR_SCROLLBACK:
        if (TerminalWidget* terminal = focusedTerminal()) terminal->clearScrollback();
        return true;

    case ACTION_RELOAD_CONFIG:      reloadConfiguration(); return true;

    case ACTION_NONE:
        break;
    }
    return false;
}

void MainWindow::reloadConfiguration() {
    Config& config = Config::instance();

    /*
     * load() starts from applyBuiltInDefaults(), so it is idempotent: every
     * layer -- built-in, theme, user -- is rebuilt from scratch rather than
     * merged on top of what is already there. That is what makes calling it a
     * second time safe, and it means a setting the user has *deleted* since the
     * last load correctly reverts to its default.
     */
    config.load();

    /* Opacity is a live window property; size and fullscreen are not touched. */
    setWindowOpacity(config.windowOpacity());
    applyTabBarConfiguration();

    for (int i = 0; i < tabCount(); ++i) {
        if (SplitContainer* root = rootAt(i)) {
            root->applyConfiguration();
        }
    }

    /*
     * Said out loud, because a reload that changed nothing visible is otherwise
     * indistinguishable from one that did not happen -- a mistyped key, or a
     * config file with a YAML error that the loader has already warned about.
     */
    qInfo() << "Config: reloaded";
}

void MainWindow::goToTab(int oneBasedIndex) {
    const int index = oneBasedIndex - 1;
    if (index >= 0 && index < tabCount()) {
        tabWidget_->setCurrentIndex(index);
    }
}

void MainWindow::splitFocusedPane(bool horizontal) {
    SplitContainer* pane = focusedPane();
    if (!pane) return;

    /* Read the label first: the surgery below can remove the tab. */
    const int index = tabWidget_->currentIndex();
    const QString label = tabLabel(index);

    /*
     * Read before the surgery: once the tree has been rearranged the pane is
     * still alive, but asking first keeps the ordering obvious. By default a
     * split inherits this pane's directory, which is the whole point of one.
     */
    const QString startDirectory = Config::instance().newSplitDirectory().resolve(
        pane->terminal() ? pane->terminal()->workingDirectory() : QString());

    SplitContainer* newPane = nullptr;
    SplitContainer* newRoot =
        horizontal ? pane->splitHorizontal(0.5f, &newPane, startDirectory)
                   : pane->splitVertical(0.5f, &newPane, startDirectory);
    if (!newRoot) return;

    installTabRoot(index, newRoot, label);

    /*
     * Only now. installTabRoot() reparents the tree into the tab widget's
     * stacked layout, and that clears Qt focus -- which is why splitting used
     * to leave the caret on the pane the user had just split away from.
     */
    giveFocusTo(newPane);
}

void MainWindow::closeFocusedPane() {
    SplitContainer* pane = focusedPane();
    if (!pane) return;

    const int index = tabWidget_->currentIndex();
    const QString label = tabLabel(index);

    /* The pane on its way out must not be a candidate for inheriting focus. */
    forgetPane(pane);

    if (SplitContainer* newRoot = pane->closePane()) {
        installTabRoot(index, newRoot, label);
        restoreFocusIn(newRoot);
    } else {
        closeTab(index);
    }
}

void MainWindow::focusNeighbour(Qt::Orientation orientation, bool forward) {
    SplitContainer* pane = focusedPane();
    if (!pane) return;

    if (SplitContainer* target = pane->findInDirection(orientation, forward)) {
        giveFocusTo(target);
    }
}

void MainWindow::changeFontSize(int delta) {
    Config& config = Config::instance();

    const int newSize = (delta == 0) ? Config::DEFAULT_FONT_SIZE
                                     : config.fontSize() + delta;
    if (newSize == config.fontSize()) return;

    config.setFontSize(newSize);

    /* The tab bar sizes itself from the terminal font, so it has to follow. */
    applyTabBarConfiguration();

    /* Every pane in every tab shares the font, so all of them must re-rasterize
     * and recompute their grid size. */
    for (int i = 0; i < tabCount(); ++i) {
        if (SplitContainer* root = rootAt(i)) {
            root->forEachLeaf([](SplitContainer* leaf) {
                if (leaf->terminal()) leaf->terminal()->reloadFont();
            });
        }
    }
}

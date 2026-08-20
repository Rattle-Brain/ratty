/*
 * MainWindow - top-level window implementation
 */

#include "main_window.h"
#include "split_container.h"
#include "terminal_widget.h"
#include <QApplication>
#include <QDebug>
#include <QKeyCombination>
#include <QKeyEvent>
#include <QTabBar>
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

    tabWidget_ = new QTabWidget(this);
    tabWidget_->setTabsClosable(true);
    tabWidget_->setMovable(true);
    tabWidget_->setDocumentMode(true);
    /* One pane means no tab bar: a single-terminal window should look like a
     * terminal, not like a tabbed document. */
    tabWidget_->tabBar()->setAutoHide(true);
    setCentralWidget(tabWidget_);

    connect(tabWidget_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
}

void MainWindow::addTab() {
    if (tabCount() >= MaxTabs) {
        qWarning() << "MainWindow: tab limit reached (" << MaxTabs << ")";
        return;
    }

    SplitContainer* root = SplitContainer::createLeaf(nullptr);
    connectRoot(root);

    const int index = tabWidget_->addTab(root, QStringLiteral("Terminal"));
    tabWidget_->setCurrentIndex(index);
    root->focusPane();
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

void MainWindow::installTabRoot(int index, SplitContainer* root) {
    if (index < 0 || index >= tabCount() || !root) return;
    if (tabWidget_->widget(index) == root) return;

    const QString label = tabWidget_->tabText(index);
    const bool wasCurrent = (tabWidget_->currentIndex() == index);

    /*
     * removeTab() only detaches; the old root has already been scheduled for
     * deletion by the tree surgery that produced `root`, so there is nothing to
     * delete here.
     */
    tabWidget_->removeTab(index);
    tabWidget_->insertTab(index, root, label);
    connectRoot(root);

    if (wasCurrent) {
        tabWidget_->setCurrentIndex(index);
    }
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
    }
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

    /* Nothing has keyboard focus (the window may have just been restored);
     * fall back to the first leaf so shortcuts still do something sensible. */
    SplitContainer* leaf = root;
    while (leaf && !leaf->isLeaf()) leaf = leaf->child1();
    return leaf;
}

void MainWindow::onPaneSessionEnded(SplitContainer* pane) {
    if (!pane) return;

    const int index = indexOfRootContaining(pane);
    if (index < 0) return;

    /* A pane with no parent is the whole tab. */
    if (SplitContainer* newRoot = pane->closePane()) {
        installTabRoot(index, newRoot);
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
        if (SplitContainer* pane = focusedPane(); pane && pane->terminal()) {
            pane->terminal()->copySelection();
        }
        return true;
    case ACTION_PASTE:
        if (SplitContainer* pane = focusedPane(); pane && pane->terminal()) {
            pane->terminal()->paste();
        }
        return true;

    case ACTION_INCREASE_FONT_SIZE: changeFontSize(+1); return true;
    case ACTION_DECREASE_FONT_SIZE: changeFontSize(-1); return true;
    case ACTION_RESET_FONT_SIZE:    changeFontSize(0); return true;

    case ACTION_SCROLL_UP:
    case ACTION_SCROLL_DOWN:
    case ACTION_CLEAR_SCROLLBACK:
        /* Bound so they do not leak into the shell, but inert until the
         * scrollback buffer exists. See todo-ratty.md. */
        return true;

    case ACTION_NONE:
        break;
    }
    return false;
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

    const int index = tabWidget_->currentIndex();
    SplitContainer* newRoot = horizontal ? pane->splitHorizontal() : pane->splitVertical();
    if (newRoot) {
        installTabRoot(index, newRoot);
    }
}

void MainWindow::closeFocusedPane() {
    SplitContainer* pane = focusedPane();
    if (!pane) return;

    const int index = tabWidget_->currentIndex();
    if (SplitContainer* newRoot = pane->closePane()) {
        installTabRoot(index, newRoot);
    } else {
        closeTab(index);
    }
}

void MainWindow::focusNeighbour(Qt::Orientation orientation, bool forward) {
    SplitContainer* pane = focusedPane();
    if (!pane) return;

    if (SplitContainer* target = pane->findInDirection(orientation, forward)) {
        target->focusPane();
    }
}

void MainWindow::changeFontSize(int delta) {
    Config& config = Config::instance();

    const int newSize = (delta == 0) ? Config::DEFAULT_FONT_SIZE
                                     : config.fontSize() + delta;
    if (newSize == config.fontSize()) return;

    config.setFontSize(newSize);

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

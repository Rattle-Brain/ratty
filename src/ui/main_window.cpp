/*
 * MainWindow - Top-level application window implementation
 */

#include "main_window.h"
#include "split_container.h"
#include "terminal_widget.h"
#include "../config/config.h"
#include <QKeyEvent>
#include <QCloseEvent>
#include <QMessageBox>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , tab_widget_(nullptr)
{
    // Config is already loaded in main.cpp before this is created
    setupUi();
    setupActions();

    // Create initial tab
    addTab();
}

MainWindow::~MainWindow() {
}

void MainWindow::setupUi() {
    setWindowTitle("Ratty Terminal");

    // Apply all window configuration from config
    Config& config = Config::instance();
    resize(config.windowWidth(), config.windowHeight());
    setWindowOpacity(config.windowOpacity());

    // Apply fullscreen setting
    if (config.startFullscreen()) {
        setWindowState(Qt::WindowFullScreen);
    }

    // Create tab widget
    tab_widget_ = new QTabWidget(this);
    tab_widget_->setTabsClosable(true);
    tab_widget_->setMovable(true);
    tab_widget_->setDocumentMode(true);
    setCentralWidget(tab_widget_);

    // Connect signals
    connect(tab_widget_, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);
}

void MainWindow::setupActions() {
    // Actions will be implemented in Phase 5 with Config system
}

void MainWindow::addTab(const QString& title) {
    if (tab_widget_->count() >= WINDOW_MAX_TABS) {
        qWarning() << "Maximum tab limit reached:" << WINDOW_MAX_TABS;
        return;
    }

    // Create a root split container with a single terminal (leaf node)
    // The split tree is only created when the user actually splits
    SplitContainer* splitRoot = SplitContainer::createLeaf(this);
    splitRoot->setFocused(true);

    // Connect session ended signal
    connect(splitRoot, &SplitContainer::sessionEnded,
            this, &MainWindow::onSplitSessionEnded);

    int index = tab_widget_->addTab(splitRoot, title);
    tab_widget_->setCurrentIndex(index);

    qDebug() << "Added tab" << index << ":" << title;
}

void MainWindow::closeTab(int index) {
    if (index < 0 || index >= tab_widget_->count()) {
        return;
    }

    // Don't close the last tab
    if (tab_widget_->count() == 1) {
        qDebug() << "Cannot close last tab";
        return;
    }

    QWidget* tab = tab_widget_->widget(index);
    tab_widget_->removeTab(index);
    delete tab;

    qDebug() << "Closed tab" << index;
}

void MainWindow::closeCurrentTab() {
    closeTab(tab_widget_->currentIndex());
}

void MainWindow::setActiveTab(int index) {
    if (index >= 0 && index < tab_widget_->count()) {
        tab_widget_->setCurrentIndex(index);
    }
}

void MainWindow::nextTab() {
    if (tab_widget_->count() <= 1) return;

    int next = (tab_widget_->currentIndex() + 1) % tab_widget_->count();
    tab_widget_->setCurrentIndex(next);
}

void MainWindow::prevTab() {
    if (tab_widget_->count() <= 1) return;

    int prev = (tab_widget_->currentIndex() - 1 + tab_widget_->count()) % tab_widget_->count();
    tab_widget_->setCurrentIndex(prev);
}

void MainWindow::gotoTab(int index) {
    // Convert from 1-based to 0-based indexing
    int tabIndex = index - 1;
    if (tabIndex >= 0 && tabIndex < tab_widget_->count()) {
        tab_widget_->setCurrentIndex(tabIndex);
    }
}

int MainWindow::tabCount() const {
    return tab_widget_->count();
}

SplitContainer* MainWindow::currentTab() const {
    return qobject_cast<SplitContainer*>(tab_widget_->currentWidget());
}

SplitContainer* MainWindow::tabAt(int index) const {
    return qobject_cast<SplitContainer*>(tab_widget_->widget(index));
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Look up action from config
    QKeySequence keySeq(event->key() | event->modifiers());
    Action action = Config::instance().lookupAction(keySeq);

    // Handle action
    switch (action) {
    case ACTION_NEW_TAB:
        addTab();
        event->accept();
        return;

    case ACTION_CLOSE_TAB:
        closeCurrentTab();
        event->accept();
        return;

    case ACTION_NEXT_TAB:
        nextTab();
        event->accept();
        return;

    case ACTION_PREV_TAB:
        prevTab();
        event->accept();
        return;

    case ACTION_GOTO_TAB_1:
    case ACTION_GOTO_TAB_2:
    case ACTION_GOTO_TAB_3:
    case ACTION_GOTO_TAB_4:
    case ACTION_GOTO_TAB_5:
    case ACTION_GOTO_TAB_6:
    case ACTION_GOTO_TAB_7:
    case ACTION_GOTO_TAB_8:
    case ACTION_GOTO_TAB_9: {
        int tabNum = action - ACTION_GOTO_TAB_1 + 1;
        gotoTab(tabNum);
        event->accept();
        return;
    }

    case ACTION_SPLIT_HORIZONTAL: {
        SplitContainer* root = currentTab();
        if (!root) return;

        // Find the focused terminal and split it
        SplitContainer* focused = root->findFocused();
        if (focused) {
            SplitContainer* newRoot = focused->splitHorizontal();
            // If the split created a new root, replace the tab widget
            if (newRoot && !newRoot->parent()) {
                int index = tab_widget_->currentIndex();
                tab_widget_->removeTab(index);
                tab_widget_->insertTab(index, newRoot, "Terminal");
                tab_widget_->setCurrentIndex(index);
            }
        }
        event->accept();
        return;
    }

    case ACTION_SPLIT_VERTICAL: {
        SplitContainer* root = currentTab();
        if (!root) return;

        // Find the focused terminal and split it
        SplitContainer* focused = root->findFocused();
        if (focused) {
            SplitContainer* newRoot = focused->splitVertical();
            // If the split created a new root, replace the tab widget
            if (newRoot && !newRoot->parent()) {
                int index = tab_widget_->currentIndex();
                tab_widget_->removeTab(index);
                tab_widget_->insertTab(index, newRoot, "Terminal");
                tab_widget_->setCurrentIndex(index);
            }
        }
        event->accept();
        return;
    }

    case ACTION_CLOSE_SPLIT: {
        SplitContainer* root = currentTab();
        if (!root) return;

        // Find the focused terminal and close it
        SplitContainer* focused = root->findFocused();
        if (focused && focused != root) {
            focused->closeSplit();
        }
        event->accept();
        return;
    }

    case ACTION_QUIT:
        close();
        event->accept();
        return;

    case ACTION_FULLSCREEN:
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
        event->accept();
        return;

    case ACTION_COPY: {
        SplitContainer* root = currentTab();
        if (root) {
            SplitContainer* focused = root->findFocused();
            if (focused && focused->terminal()) {
                focused->terminal()->copySelection();
            }
        }
        event->accept();
        return;
    }

    case ACTION_PASTE: {
        SplitContainer* root = currentTab();
        if (root) {
            SplitContainer* focused = root->findFocused();
            if (focused && focused->terminal()) {
                focused->terminal()->paste();
            }
        }
        event->accept();
        return;
    }

    default:
        // No action bound, pass to parent
        QMainWindow::keyPressEvent(event);
        return;
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Save window size to config
    Config::instance().setWindowWidth(width());
    Config::instance().setWindowHeight(height());
    Config::instance().save();

    event->accept();
}

void MainWindow::onTabCloseRequested(int index) {
    closeTab(index);
}

void MainWindow::onSplitSessionEnded(SplitContainer* split) {
    if (!split) return;

    qDebug() << "MainWindow: Handling session end for split";

    // Find which tab contains this split
    int tabIndex = -1;
    SplitContainer* tabRoot = nullptr;
    for (int i = 0; i < tab_widget_->count(); ++i) {
        SplitContainer* root = tabAt(i);
        if (!root) continue;

        // Check if this split is the root or a descendant
        if (root == split || isDescendant(root, split)) {
            tabIndex = i;
            tabRoot = root;
            break;
        }
    }

    if (tabIndex < 0) {
        qWarning() << "MainWindow: Could not find tab containing split";
        return;
    }

    // Case 1: This is the root split (only split in the tab)
    if (split == tabRoot) {
        qDebug() << "MainWindow: Closing tab" << tabIndex << "(last split in tab)";

        // If this is the last tab, close the window
        if (tab_widget_->count() == 1) {
            qDebug() << "MainWindow: Last tab closed, closing window";
            close();
            return;
        }

        // Otherwise just close this tab
        closeTab(tabIndex);
        return;
    }

    // Case 2: This is a split within a split tree
    qDebug() << "MainWindow: Closing split (not root)";

    // Close the split (this will restructure the tree)
    if (split->closeSplit()) {
        // If closeSplit succeeded, the tree was restructured
        // We need to check if the new root changed
        // (This happens when the parent was the old root)
        SplitContainer* currentRoot = tabAt(tabIndex);
        if (currentRoot != tabRoot) {
            // Root changed, need to update tab widget
            qDebug() << "MainWindow: Split tree root changed, updating tab";
            tab_widget_->removeTab(tabIndex);
            tab_widget_->insertTab(tabIndex, currentRoot, "Terminal");
            tab_widget_->setCurrentIndex(tabIndex);

            // Connect session ended signal to new root
            connect(currentRoot, &SplitContainer::sessionEnded,
                    this, &MainWindow::onSplitSessionEnded);
        }
    }
}

// Helper to check if a split is a descendant of a container
bool MainWindow::isDescendant(SplitContainer* container, SplitContainer* split) {
    if (!container || !split) return false;
    if (container == split) return true;

    if (container->isContainer()) {
        return isDescendant(container->child1(), split) ||
               isDescendant(container->child2(), split);
    }

    return false;
}

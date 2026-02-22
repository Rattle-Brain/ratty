/*
 * MainWindow - Top-level application window implementation
 */

#include "main_window.h"
#include "terminal_tab.h"
#include "../config/config.h"
#include <QKeyEvent>
#include <QCloseEvent>
#include <QMessageBox>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , tab_widget_(nullptr)
{
    // Load configuration
    Config::instance().load();

    setupUi();
    setupActions();

    // Apply window size from config
    resize(Config::instance().windowWidth(), Config::instance().windowHeight());

    // Create initial tab
    addTab();
}

MainWindow::~MainWindow() {
}

void MainWindow::setupUi() {
    setWindowTitle("Ratty Terminal");
    resize(1024, 768);

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

    TerminalTab* tab = new TerminalTab(this);
    int index = tab_widget_->addTab(tab, title);
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

TerminalTab* MainWindow::currentTab() const {
    return qobject_cast<TerminalTab*>(tab_widget_->currentWidget());
}

TerminalTab* MainWindow::tabAt(int index) const {
    return qobject_cast<TerminalTab*>(tab_widget_->widget(index));
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
        TerminalTab* tab = currentTab();
        if (tab) tab->splitHorizontal();
        event->accept();
        return;
    }

    case ACTION_SPLIT_VERTICAL: {
        TerminalTab* tab = currentTab();
        if (tab) tab->splitVertical();
        event->accept();
        return;
    }

    case ACTION_CLOSE_SPLIT: {
        TerminalTab* tab = currentTab();
        if (tab) tab->closeSplit();
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
        TerminalTab* tab = currentTab();
        if (tab) {
            // Get the focused terminal widget from the tab
            // For now, this is a simplified implementation
            // In the future, this should find the focused split's terminal
            qDebug() << "Copy action triggered";
        }
        event->accept();
        return;
    }

    case ACTION_PASTE: {
        TerminalTab* tab = currentTab();
        if (tab) {
            // Get the focused terminal widget from the tab
            // For now, this is a simplified implementation
            // In the future, this should find the focused split's terminal
            qDebug() << "Paste action triggered";
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

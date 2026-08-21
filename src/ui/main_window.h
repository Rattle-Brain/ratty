/*
 * MainWindow - top-level window holding a tab per split tree
 *
 * Each tab's widget is the root SplitContainer of a pane tree. Because closing
 * or creating a split can change which node is the root, the window keeps the
 * tab widget in step through installTabRoot() rather than trying to detect the
 * change after the fact.
 */

#ifndef UI_MAIN_WINDOW_H
#define UI_MAIN_WINDOW_H

#include "../config/config.h"
#include <QList>
#include <QMainWindow>
#include <QPointer>

class QTabWidget;
class SplitContainer;
class TabBar;
class TerminalWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    static constexpr int MaxTabs = 32;

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void addTab();
    void closeTab(int index);

    int tabCount() const;
    SplitContainer* currentRoot() const;
    SplitContainer* rootAt(int index) const;

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onPaneSessionEnded(SplitContainer* pane);
    void onPaneTitleChanged(const QString& title);
    void onPaneFocused(SplitContainer* pane);
    void onCurrentTabChanged(int index);

private:
    void setupUi();
    /* Label of the tab at `index`, or a default. Read *before* tree surgery,
     * because the surgery can make the tab disappear. */
    QString tabLabel(int index) const;
    /* Re-read the tab bar's style, position and visibility from the config. */
    void applyTabBarConfiguration();
    void updateTabBarVisibility();
    /*
     * Make `root` the page of the tab at `index`.
     *
     * Deliberately tolerant of the tab having already vanished: promoting a new
     * root reparents the old page out of the tab widget's stack, and QTabWidget
     * responds by removing the tab. See the implementation.
     */
    void installTabRoot(int index, SplitContainer* root, const QString& label);
    void connectRoot(SplitContainer* root);
    int indexOfRootContaining(const SplitContainer* node) const;

    bool handleAction(Action action);
    /*
     * Re-read the configuration and apply it to every pane in every tab, so a
     * theme, font or colour change can be seen without restarting. Deliberately
     * leaves the window's geometry and fullscreen state alone -- those are
     * start-up settings, and reasserting them would fight whatever the user has
     * since done with the window.
     */
    void reloadConfiguration();
    void splitFocusedPane(bool horizontal);
    void closeFocusedPane();
    void focusNeighbour(Qt::Orientation orientation, bool forward);
    void changeFontSize(int delta);
    void goToTab(int oneBasedIndex);

    /*
     * The directory the current pane's shell is in, for the "inherit" start
     * directory setting. Empty when there is no pane or its shell has gone.
     */
    QString currentPaneDirectory() const;

    /* The focused pane of the current tab, or nullptr. */
    SplitContainer* focusedPane() const;
    /* Its terminal, which is what most actions actually want. */
    TerminalWidget* focusedTerminal() const;

    /*
     * Focus `pane` deliberately: the single path for RaTTY choosing a pane, as
     * opposed to Qt handing focus out during a reparenting. Only what goes
     * through here, or a click, reaches the focus history.
     */
    void giveFocusTo(SplitContainer* pane);
    /* Move `pane` to the front of the focus history. */
    void rememberFocus(SplitContainer* pane);
    /* Drop `pane` from it, before closing it. */
    void forgetPane(const SplitContainer* pane);
    /*
     * Give keyboard focus to the pane in `root` that should have it: the most
     * recently focused one still alive, failing that the tree's marked pane,
     * failing that its first leaf.
     *
     * Every path that rearranges a tree ends here, because Qt focus does not
     * survive the reparenting those paths do.
     */
    void restoreFocusIn(SplitContainer* root);

    QTabWidget* tabWidget_ = nullptr;
    TabBar* tabBar_ = nullptr;

    /*
     * Panes in the order they last held focus, most recent first. This is what
     * lets closing a split hand focus back to the pane the user came from
     * instead of to whichever leaf happens to sit leftmost in the tree.
     *
     * QPointers because a pane dies without telling this list: closing a split,
     * a shell exiting and closing a tab all destroy panes. Dead entries are
     * pruned on the way past rather than tracked.
     */
    QList<QPointer<SplitContainer>> focusHistory_;
};

#endif /* UI_MAIN_WINDOW_H */

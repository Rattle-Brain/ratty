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
#include <QMainWindow>

class QTabWidget;
class SplitContainer;
class TabBar;

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

private:
    void setupUi();
    /* Re-read the tab bar's style, position and visibility from the config. */
    void applyTabBarConfiguration();
    void updateTabBarVisibility();
    /* Puts `root` at `index`, taking over the tab's previous widget. */
    void installTabRoot(int index, SplitContainer* root);
    void connectRoot(SplitContainer* root);
    int indexOfRootContaining(const SplitContainer* node) const;

    bool handleAction(Action action);
    void splitFocusedPane(bool horizontal);
    void closeFocusedPane();
    void focusNeighbour(Qt::Orientation orientation, bool forward);
    void changeFontSize(int delta);
    void goToTab(int oneBasedIndex);

    /* The focused pane of the current tab, or nullptr. */
    SplitContainer* focusedPane() const;

    QTabWidget* tabWidget_ = nullptr;
    TabBar* tabBar_ = nullptr;
};

#endif /* UI_MAIN_WINDOW_H */

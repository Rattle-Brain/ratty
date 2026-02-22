/*
 * MainWindow - Top-level application window
 *
 * Manages multiple terminal tabs using QTabWidget
 */

#ifndef UI_MAIN_WINDOW_H
#define UI_MAIN_WINDOW_H

#include <QMainWindow>
#include <QTabWidget>

class TerminalTab;

#define WINDOW_MAX_TABS 32

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Tab management
    void addTab(const QString& title = "Terminal");
    void closeTab(int index);
    void closeCurrentTab();
    void setActiveTab(int index);
    void nextTab();
    void prevTab();
    void gotoTab(int index);  // Go to tab 1-9

    // Queries
    int tabCount() const;
    TerminalTab* currentTab() const;
    TerminalTab* tabAt(int index) const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onTabCloseRequested(int index);

private:
    void setupUi();
    void setupActions();

    QTabWidget* tab_widget_;
};

#endif /* UI_MAIN_WINDOW_H */

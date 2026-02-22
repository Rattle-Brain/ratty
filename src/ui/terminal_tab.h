/*
 * TerminalTab - Container for terminal splits
 *
 * Manages a tree of split containers (horizontal/vertical splits)
 */

#ifndef UI_TERMINAL_TAB_H
#define UI_TERMINAL_TAB_H

#include <QWidget>

class SplitContainer;

class TerminalTab : public QWidget {
    Q_OBJECT

public:
    explicit TerminalTab(QWidget *parent = nullptr);
    ~TerminalTab() override;

    // Split operations
    void splitHorizontal();
    void splitVertical();
    void closeSplit();

private:
    SplitContainer* root_split_;
};

#endif /* UI_TERMINAL_TAB_H */

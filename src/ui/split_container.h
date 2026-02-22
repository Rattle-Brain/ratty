/*
 * SplitContainer - Binary tree structure for terminal panes
 *
 * A split can be:
 * 1. A LEAF node - contains an actual terminal widget
 * 2. A CONTAINER node - contains 2 child splits (vertical or horizontal)
 *
 * Example layout:
 *     [Root: HORIZONTAL]
 *          /        \
 *    [Terminal A]  [VERTICAL]
 *                   /      \
 *            [Terminal B] [Terminal C]
 */

#ifndef UI_SPLIT_CONTAINER_H
#define UI_SPLIT_CONTAINER_H

#include <QWidget>
#include <QSplitter>

class TerminalWidget;

class SplitContainer : public QWidget {
    Q_OBJECT

public:
    enum SplitType {
        Leaf,
        Horizontal,  // Left/Right split
        Vertical     // Top/Bottom split
    };

    enum Direction {
        Up,
        Right,
        Down,
        Left
    };

    // Create a leaf split with a terminal widget
    static SplitContainer* createLeaf(QWidget* parent = nullptr);

    ~SplitContainer() override;

    // Splitting operations - splits this leaf into a container
    // Returns the new container (which replaces this leaf in the tree)
    SplitContainer* splitHorizontal(float ratio = 0.5f);
    SplitContainer* splitVertical(float ratio = 0.5f);

    // Closing panes - closes this pane
    // Returns true if the split was closed (false if it's the last split)
    bool closeSplit();

    // Navigation
    SplitContainer* findFocused();
    SplitContainer* findInDirection(Direction dir);
    SplitContainer* findAtPosition(const QPoint& pos);

    // Focus management
    void setFocused(bool focused);
    bool isFocused() const { return focused_; }

    // Queries
    SplitType type() const { return type_; }
    bool isLeaf() const { return type_ == Leaf; }
    bool isContainer() const { return type_ != Leaf; }
    int countLeaves() const;

    // Tree accessors (for internal use)
    SplitContainer* parent() const { return parent_; }
    SplitContainer* child1() const { return child1_; }
    SplitContainer* child2() const { return child2_; }
    TerminalWidget* terminal() const { return terminal_; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    // Private constructor - use createLeaf() static factory
    explicit SplitContainer(QWidget* parent = nullptr);

    // Create a container split (internal use)
    static SplitContainer* createContainer(SplitType type,
                                           SplitContainer* child1,
                                           SplitContainer* child2,
                                           float ratio,
                                           QWidget* parent = nullptr);

    // Helper for splitting
    SplitContainer* performSplit(SplitType splitType, float ratio);

    // Helper for tree restructuring
    void replaceChild(SplitContainer* oldChild, SplitContainer* newChild);

    SplitType type_;
    bool focused_;

    // Tree structure
    SplitContainer* parent_;

    // If LEAF:
    TerminalWidget* terminal_;

    // If CONTAINER:
    QSplitter* splitter_;
    SplitContainer* child1_;  // Left or Top
    SplitContainer* child2_;  // Right or Bottom
};

#endif /* UI_SPLIT_CONTAINER_H */

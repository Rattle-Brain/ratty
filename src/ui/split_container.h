/*
 * SplitContainer - binary tree of terminal panes
 *
 * A node is either a LEAF holding one TerminalWidget, or a CONTAINER holding a
 * QSplitter with exactly two child nodes.
 *
 *     [Root: Horizontal]
 *          /        \
 *    [Terminal A]  [Vertical]
 *                   /      \
 *            [Terminal B] [Terminal C]
 *
 * Tree surgery (split, close) always reports the resulting root, because
 * closing a pane can promote a sibling and change which node the tab widget
 * should hold. The previous version left that for the caller to infer, and
 * deleted the old parent container while the surviving sibling was still one of
 * its descendants -- so closing a split could take the surviving pane with it.
 */

#ifndef UI_SPLIT_CONTAINER_H
#define UI_SPLIT_CONTAINER_H

#include <QSplitter>
#include <QWidget>

class TerminalWidget;

class SplitContainer : public QWidget {
    Q_OBJECT

public:
    enum SplitType {
        Leaf,
        Horizontal,  // side by side
        Vertical     // stacked
    };

    /* A new leaf with its own terminal. */
    static SplitContainer* createLeaf(QWidget* parent = nullptr);

    ~SplitContainer() override;

    /*
     * Split this leaf in two. Returns the root of the (possibly new) tree, or
     * nullptr if this node is not a leaf.
     */
    SplitContainer* splitHorizontal(float ratio = 0.5f);
    SplitContainer* splitVertical(float ratio = 0.5f);

    /*
     * Remove this pane. Returns the new root of the tree, or nullptr if this is
     * the only pane left (in which case nothing was changed and the caller
     * should close the tab instead).
     */
    SplitContainer* closePane();

    /* Navigation */
    SplitContainer* findFocused();
    SplitContainer* rootNode();
    /* Nearest leaf in `dir` from this one, or nullptr at the edge. */
    SplitContainer* findInDirection(Qt::Orientation orientation, bool forward);

    void focusPane();

    SplitType type() const { return type_; }
    bool isLeaf() const { return type_ == Leaf; }
    bool isContainer() const { return type_ != Leaf; }
    int countLeaves() const;
    bool contains(const SplitContainer* node) const;

    SplitContainer* parentNode() const { return parent_; }
    SplitContainer* child1() const { return child1_; }
    SplitContainer* child2() const { return child2_; }
    TerminalWidget* terminal() const { return terminal_; }

    /* Applies `fn` to every leaf in the subtree. */
    template <typename Fn>
    void forEachLeaf(Fn&& fn) {
        if (type_ == Leaf) {
            fn(this);
            return;
        }
        if (child1_) child1_->forEachLeaf(fn);
        if (child2_) child2_->forEachLeaf(fn);
    }

signals:
    /* A terminal session in this subtree ended; `pane` is the leaf. */
    void paneSessionEnded(SplitContainer* pane);
    /* Title of the focused terminal in this subtree changed. */
    void paneTitleChanged(const QString& title);

private:
    explicit SplitContainer(QWidget* parent = nullptr);

    static SplitContainer* createContainer(SplitType type, SplitContainer* first,
                                           SplitContainer* second, float ratio);

    SplitContainer* performSplit(SplitType splitType, float ratio);
    void replaceChild(SplitContainer* oldChild, SplitContainer* newChild);
    void adoptChildSignals(SplitContainer* child);
    /* Detach `child` from this node's splitter without destroying it. */
    void detachChild(SplitContainer* child);

    SplitType type_ = Leaf;

    SplitContainer* parent_ = nullptr;

    /* Leaf payload. */
    TerminalWidget* terminal_ = nullptr;

    /* Container payload. */
    QSplitter* splitter_ = nullptr;
    SplitContainer* child1_ = nullptr;
    SplitContainer* child2_ = nullptr;
};

#endif /* UI_SPLIT_CONTAINER_H */

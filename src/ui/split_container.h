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
    /* `startDirectory` is where the new pane's shell begins; empty inherits
     * RaTTY's own directory. Callers resolve it from the configuration. */
    static SplitContainer* createLeaf(QWidget* parent = nullptr,
                                      const QString& startDirectory = QString());

    ~SplitContainer() override;

    /*
     * Split this leaf in two. Returns the root of the (possibly new) tree, or
     * nullptr if this node is not a leaf.
     */
    /*
     * `newPane`, when given, receives the leaf that was created. The caller
     * needs it because focus has to be applied *after* the new root is
     * installed in the tab widget: reparenting a widget into a QStackedWidget
     * clears Qt focus, so anything focused during the surgery is lost.
     */
    SplitContainer* splitHorizontal(float ratio = 0.5f, SplitContainer** newPane = nullptr,
                                    const QString& startDirectory = QString());
    SplitContainer* splitVertical(float ratio = 0.5f, SplitContainer** newPane = nullptr,
                                  const QString& startDirectory = QString());

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

    /* Give this pane keyboard focus, and the focus marker with it. */
    void focusPane();
    /*
     * Make this pane the marked one without touching Qt focus. The marker is
     * RaTTY's own record of which pane is current, and unlike Qt focus it
     * survives the reparenting that tree surgery does.
     */
    void markFocused();
    /* The marked pane in this subtree, or nullptr. */
    SplitContainer* findMarkedPane();

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
    /*
     * A pane in this subtree took keyboard focus. Emitted for a click just as
     * much as for focusPane(), which is what lets a focus history built from it
     * be complete.
     */
    void paneFocused(SplitContainer* pane);

private:
    explicit SplitContainer(QWidget* parent = nullptr);

    /* `span` is the space the split is dividing; see the note in the body. */
    static SplitContainer* createContainer(SplitType type, SplitContainer* first,
                                           SplitContainer* second, float ratio,
                                           QSize span);

    SplitContainer* performSplit(SplitType splitType, float ratio, SplitContainer** newPane,
                                 const QString& startDirectory);
    /* `sizes` is the splitter division to restore, for callers that detached
     * the old child first; empty means "whatever the splitter reports now". */
    void replaceChild(SplitContainer* oldChild, SplitContainer* newChild,
                      const QList<int>& sizes = {});
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

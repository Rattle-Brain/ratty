/*
 * SplitContainer - pane tree implementation
 */

#include "split_container.h"
#include "terminal_widget.h"
#include <QVBoxLayout>
#include <QDebug>

namespace {

QVBoxLayout* makeFlushLayout(QWidget* owner) {
    auto* layout = new QVBoxLayout(owner);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return layout;
}

} // namespace

SplitContainer::SplitContainer(QWidget* parent)
    : QWidget(parent)
{
}

SplitContainer::~SplitContainer() = default;

SplitContainer* SplitContainer::createLeaf(QWidget* parent) {
    auto* node = new SplitContainer(parent);
    node->type_ = Leaf;
    node->terminal_ = new TerminalWidget(node);

    connect(node->terminal_, &TerminalWidget::sessionEnded, node, [node]() {
        emit node->paneSessionEnded(node);
    });
    connect(node->terminal_, &TerminalWidget::titleChanged, node,
            [node](const QString& title) {
                emit node->paneTitleChanged(title);
            });
    connect(node->terminal_, &TerminalWidget::paneActivated, node, [node]() {
        /* A click focuses the terminal directly, without going through
         * focusPane(), so the marker has to be brought into line here or two
         * panes would both believe they are the current one. */
        node->markFocused();
        emit node->paneFocused(node);
    });

    makeFlushLayout(node)->addWidget(node->terminal_);
    return node;
}

SplitContainer* SplitContainer::createContainer(SplitType type, SplitContainer* first,
                                                SplitContainer* second, float ratio,
                                                QSize span) {
    if (!first || !second || type == Leaf) return nullptr;

    auto* container = new SplitContainer(nullptr);
    container->type_ = type;

    const Qt::Orientation orientation = (type == Horizontal) ? Qt::Horizontal : Qt::Vertical;
    container->splitter_ = new QSplitter(orientation, container);
    container->splitter_->setHandleWidth(1);
    container->splitter_->setChildrenCollapsible(false);
    container->splitter_->setStyleSheet(
        QStringLiteral("QSplitter::handle { background-color: #3a3a3a; }"));

    container->child1_ = first;
    container->child2_ = second;
    first->parent_ = container;
    second->parent_ = container;

    container->splitter_->addWidget(first);
    container->splitter_->addWidget(second);
    /*
     * Show the children explicitly. QSplitter only auto-shows a widget when the
     * splitter itself is already visible, and a widget that has been through a
     * QStackedWidget (every tab page has) comes back carrying
     * WA_WState_ExplicitShowHide, which suppresses the implicit show entirely.
     * Being explicit here is the difference between a working split and a pane
     * that silently vanishes.
     */
    first->show();
    second->show();
    /* Equal stretch keeps the ratio stable as the window is resized; without it
     * QSplitter hands all new space to the last widget. */
    container->splitter_->setStretchFactor(0, 1);
    container->splitter_->setStretchFactor(1, 1);

    /*
     * `span` is the space the pane being split occupies, measured by the caller
     * before the tree surgery. The container itself cannot supply it: it is
     * built detached, so it still carries Qt's default 640x480 placeholder
     * geometry and would split that instead of the real pane.
     */
    const int total = (orientation == Qt::Horizontal) ? span.width() : span.height();
    const int reference = total > 0 ? total : 1000;
    container->splitter_->setSizes({static_cast<int>(reference * ratio),
                                    static_cast<int>(reference * (1.0f - ratio))});

    makeFlushLayout(container)->addWidget(container->splitter_);

    container->adoptChildSignals(first);
    container->adoptChildSignals(second);
    return container;
}

void SplitContainer::adoptChildSignals(SplitContainer* child) {
    if (!child) return;
    /* Forward child notifications up the tree so only the root needs a
     * listener. Reconnecting after tree surgery is safe because the old node is
     * destroyed with its connections. */
    connect(child, &SplitContainer::paneSessionEnded,
            this, &SplitContainer::paneSessionEnded, Qt::UniqueConnection);
    connect(child, &SplitContainer::paneTitleChanged,
            this, &SplitContainer::paneTitleChanged, Qt::UniqueConnection);
    connect(child, &SplitContainer::paneFocused,
            this, &SplitContainer::paneFocused, Qt::UniqueConnection);
}

SplitContainer* SplitContainer::performSplit(SplitType splitType, float ratio,
                                             SplitContainer** newPane) {
    if (type_ != Leaf) {
        qWarning() << "SplitContainer: only leaves can be split";
        return nullptr;
    }

    SplitContainer* newLeaf = createLeaf(nullptr);
    SplitContainer* oldParent = parent_;

    /*
     * Both measurements have to be taken before the detach below.
     *
     * `parentSizes` is the enclosing splitter's division of space. Once this
     * node leaves, that splitter is down to a single widget and reports a
     * single size, so there is nothing left to restore from -- and a container
     * that QSplitter inserts without sizes is clamped to its minimum, which is
     * what crushed the panes already in the tab into a 100px strip as soon as
     * a third pane appeared.
     *
     * `span` is this pane's own geometry, which the new container inherits.
     */
    const QList<int> parentSizes = (oldParent && oldParent->splitter_)
                                       ? oldParent->splitter_->sizes()
                                       : QList<int>();
    const QSize span = size();

    /* Detach from the current parent first, so the container can take ownership
     * without the splitter reparenting it back mid-construction. */
    if (oldParent) {
        oldParent->detachChild(this);
    } else {
        setParent(nullptr);
    }

    SplitContainer* container = createContainer(splitType, this, newLeaf, ratio, span);
    if (!container) {
        delete newLeaf;
        return nullptr;
    }

    if (oldParent) {
        oldParent->replaceChild(this, container, parentSizes);
        container->parent_ = oldParent;
    }

    /*
     * Mark the new pane as the current one, but leave Qt focus to the caller:
     * installing the root reparents this whole tree into the tab widget's
     * stacked layout, and that clears any focus set here. The marker survives
     * it, so the caller can find its way back.
     */
    newLeaf->markFocused();
    if (newPane) *newPane = newLeaf;
    return container->rootNode();
}

SplitContainer* SplitContainer::splitHorizontal(float ratio, SplitContainer** newPane) {
    return performSplit(Horizontal, ratio, newPane);
}

SplitContainer* SplitContainer::splitVertical(float ratio, SplitContainer** newPane) {
    return performSplit(Vertical, ratio, newPane);
}

void SplitContainer::detachChild(SplitContainer* child) {
    if (!child) return;

    /*
     * Reparenting to nullptr takes the widget out of the splitter's ownership
     * without destroying it, which is the whole point: Qt would otherwise
     * delete it along with the splitter. Qt hides a reparented widget for us, so
     * there is no flash of a stray top-level window; whoever reattaches it is
     * responsible for showing it again.
     */
    child->setParent(nullptr);
    child->parent_ = nullptr;
}

SplitContainer* SplitContainer::closePane() {
    SplitContainer* parent = parent_;
    if (!parent) {
        /* The only pane in the tab: the caller owns the decision to close it. */
        return nullptr;
    }

    SplitContainer* sibling = (parent->child1_ == this) ? parent->child2_ : parent->child1_;
    SplitContainer* grandparent = parent->parent_;

    /* Read the grandparent's division of space before the surgery, for the same
     * reason performSplit() does: the promoted sibling has to land on the share
     * the container it replaces was holding. */
    const QList<int> grandparentSizes = (grandparent && grandparent->splitter_)
                                            ? grandparent->splitter_->sizes()
                                            : QList<int>();

    /*
     * Order matters. The sibling must leave the doomed parent's splitter before
     * that parent is destroyed, or Qt's ownership takes the sibling down with
     * it -- which is exactly what the previous implementation did.
     */
    parent->detachChild(sibling);
    parent->detachChild(this);
    parent->child1_ = nullptr;
    parent->child2_ = nullptr;

    sibling->parent_ = grandparent;

    if (grandparent) {
        /* The doomed container leaves the grandparent's splitter before the
         * sibling takes its place, so the splitter never holds three widgets
         * and the restored sizes line up with what is in it. */
        grandparent->detachChild(parent);
        grandparent->replaceChild(parent, sibling, grandparentSizes);
    }

    /* `this` and the parent container are no longer reachable from the tree. */
    setParent(nullptr);
    deleteLater();
    parent->setParent(nullptr);
    parent->deleteLater();

    /*
     * The marker was on the pane just removed, so it has to move to a live one.
     * Only the marker: as in performSplit(), Qt focus is the caller's to apply
     * once the surviving tree is back in the tab widget.
     */
    SplitContainer* target = sibling;
    while (target && !target->isLeaf()) target = target->child1_;
    if (target) target->markFocused();

    return sibling->rootNode();
}

void SplitContainer::replaceChild(SplitContainer* oldChild, SplitContainer* newChild,
                                  const QList<int>& sizes) {
    if (!splitter_ || !newChild) return;

    int index = -1;
    if (child1_ == oldChild) {
        child1_ = newChild;
        index = 0;
    } else if (child2_ == oldChild) {
        child2_ = newChild;
        index = 1;
    }
    if (index < 0) return;

    /*
     * Preserve the split ratio across the swap. A caller that already detached
     * the old child has to pass the sizes it read beforehand, because the
     * splitter can no longer report them.
     */
    const QList<int> restore = sizes.isEmpty() ? splitter_->sizes() : sizes;
    splitter_->insertWidget(index, newChild);
    newChild->show();       // see the note in createContainer()
    newChild->parent_ = this;
    if (restore.size() == 2) {
        splitter_->setSizes(restore);
    }
    adoptChildSignals(newChild);
}

SplitContainer* SplitContainer::rootNode() {
    SplitContainer* node = this;
    while (node->parent_) node = node->parent_;
    return node;
}

SplitContainer* SplitContainer::findFocused() {
    if (type_ == Leaf) {
        return (terminal_ && terminal_->hasFocus()) ? this : nullptr;
    }
    if (SplitContainer* found = child1_ ? child1_->findFocused() : nullptr) return found;
    return child2_ ? child2_->findFocused() : nullptr;
}

SplitContainer* SplitContainer::findInDirection(Qt::Orientation orientation, bool forward) {
    /*
     * Walk up until a container splits along `orientation` with this subtree on
     * the near side, then descend the far side to its first leaf.
     */
    SplitContainer* node = this;
    while (SplitContainer* parent = node->parentNode()) {
        const bool matches = (parent->type_ == Horizontal && orientation == Qt::Horizontal)
                          || (parent->type_ == Vertical && orientation == Qt::Vertical);
        if (matches) {
            SplitContainer* target = forward
                ? (parent->child1_ == node ? parent->child2_ : nullptr)
                : (parent->child2_ == node ? parent->child1_ : nullptr);
            if (target) {
                while (target && !target->isLeaf()) {
                    target = forward ? target->child1_ : target->child2_;
                }
                return target;
            }
        }
        node = parent;
    }
    return nullptr;
}

void SplitContainer::markFocused() {
    if (type_ != Leaf || !terminal_) return;

    /* Clear the marker on every other pane so exactly one is current. */
    if (SplitContainer* root = rootNode()) {
        root->forEachLeaf([this](SplitContainer* leaf) {
            if (leaf->terminal_) leaf->terminal_->setPaneFocused(leaf == this);
        });
    }
}

void SplitContainer::focusPane() {
    if (type_ != Leaf || !terminal_) return;

    markFocused();
    terminal_->setFocus();
}

SplitContainer* SplitContainer::findMarkedPane() {
    SplitContainer* marked = nullptr;
    forEachLeaf([&marked](SplitContainer* leaf) {
        if (!marked && leaf->terminal_ && leaf->terminal_->isPaneFocused()) marked = leaf;
    });
    return marked;
}

int SplitContainer::countLeaves() const {
    if (type_ == Leaf) return 1;
    return (child1_ ? child1_->countLeaves() : 0) + (child2_ ? child2_->countLeaves() : 0);
}

bool SplitContainer::contains(const SplitContainer* node) const {
    if (!node) return false;
    if (node == this) return true;
    if (type_ == Leaf) return false;
    return (child1_ && child1_->contains(node)) || (child2_ && child2_->contains(node));
}

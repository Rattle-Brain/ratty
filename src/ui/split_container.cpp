/*
 * SplitContainer - Binary tree structure for terminal panes
 */

#include "split_container.h"
#include "terminal_widget.h"
#include <QVBoxLayout>
#include <QDebug>

SplitContainer::SplitContainer(QWidget* parent)
    : QWidget(parent)
    , type_(Leaf)
    , focused_(false)
    , parent_(nullptr)
    , terminal_(nullptr)
    , splitter_(nullptr)
    , child1_(nullptr)
    , child2_(nullptr)
{
}

SplitContainer::~SplitContainer() {
    // Qt will handle child widget deletion
}

SplitContainer* SplitContainer::createLeaf(QWidget* parent) {
    SplitContainer* split = new SplitContainer(parent);
    split->type_ = Leaf;

    // Create terminal widget
    split->terminal_ = new TerminalWidget(split);

    // Set up layout
    QVBoxLayout* layout = new QVBoxLayout(split);
    layout->addWidget(split->terminal_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    qDebug() << "Created leaf split";
    return split;
}

SplitContainer* SplitContainer::createContainer(SplitType type,
                                                 SplitContainer* child1,
                                                 SplitContainer* child2,
                                                 float ratio,
                                                 QWidget* parent)
{
    if (!child1 || !child2 || type == Leaf) {
        return nullptr;
    }

    SplitContainer* container = new SplitContainer(parent);
    container->type_ = type;

    // Create splitter
    Qt::Orientation orientation = (type == Horizontal) ?
                                   Qt::Horizontal : Qt::Vertical;
    container->splitter_ = new QSplitter(orientation, container);
    container->splitter_->setHandleWidth(1);
    container->splitter_->setStyleSheet("QSplitter::handle { background-color: #505050; }");

    // Add children to splitter
    container->child1_ = child1;
    container->child2_ = child2;

    // Reparent children
    child1->setParent(container->splitter_);
    child2->setParent(container->splitter_);
    child1->parent_ = container;
    child2->parent_ = container;

    container->splitter_->addWidget(child1);
    container->splitter_->addWidget(child2);

    // Set initial split ratio
    QList<int> sizes;
    int total = (type == Horizontal) ? container->width() : container->height();
    if (total == 0) total = 1000;  // Default size
    sizes << static_cast<int>(total * ratio) << static_cast<int>(total * (1.0f - ratio));
    container->splitter_->setSizes(sizes);

    // Set up layout
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->addWidget(container->splitter_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    qDebug() << "Created container split:" << (type == Horizontal ? "Horizontal" : "Vertical");
    return container;
}

SplitContainer* SplitContainer::performSplit(SplitType splitType, float ratio) {
    if (type_ != Leaf) {
        qWarning() << "Can only split leaf nodes";
        return nullptr;
    }

    // Create new leaf for the second pane
    SplitContainer* newLeaf = createLeaf(nullptr);
    if (!newLeaf) return nullptr;

    // Create container to hold both leaves
    SplitContainer* container = createContainer(splitType, this, newLeaf, ratio, nullptr);
    if (!container) {
        delete newLeaf;
        return nullptr;
    }

    // Update parent relationship
    if (parent_) {
        parent_->replaceChild(this, container);
        container->parent_ = parent_;
    }

    // The container's layout now owns this widget and newLeaf
    // so we don't need to delete them manually

    return container;
}

SplitContainer* SplitContainer::splitHorizontal(float ratio) {
    qDebug() << "Splitting horizontal";
    return performSplit(Horizontal, ratio);
}

SplitContainer* SplitContainer::splitVertical(float ratio) {
    qDebug() << "Splitting vertical";
    return performSplit(Vertical, ratio);
}

bool SplitContainer::closeSplit() {
    if (!parent_) {
        // Can't close root
        qDebug() << "Cannot close root split";
        return false;
    }

    // Find sibling
    SplitContainer* sibling = (parent_->child1_ == this) ?
                              parent_->child2_ : parent_->child1_;

    // Replace parent with sibling in grandparent
    if (parent_->parent_) {
        SplitContainer* grandparent = parent_->parent_;
        grandparent->replaceChild(parent_, sibling);
        sibling->parent_ = grandparent;
    } else {
        // Parent was root - sibling becomes new root
        sibling->parent_ = nullptr;
    }

    // Clean up
    // Remove this from parent's splitter to prevent double-delete
    parent_->child1_ = nullptr;
    parent_->child2_ = nullptr;

    // Delete this split
    deleteLater();

    // Delete parent container
    parent_->deleteLater();

    qDebug() << "Closed split";
    return true;
}

void SplitContainer::replaceChild(SplitContainer* oldChild, SplitContainer* newChild) {
    if (!splitter_) return;

    int index = -1;
    if (child1_ == oldChild) {
        child1_ = newChild;
        index = 0;
    } else if (child2_ == oldChild) {
        child2_ = newChild;
        index = 1;
    }

    if (index >= 0) {
        // Replace in splitter
        splitter_->replaceWidget(index, newChild);
        newChild->setParent(splitter_);
    }
}

SplitContainer* SplitContainer::findFocused() {
    if (type_ == Leaf) {
        return focused_ ? this : nullptr;
    }

    SplitContainer* found = child1_ ? child1_->findFocused() : nullptr;
    if (found) return found;

    return child2_ ? child2_->findFocused() : nullptr;
}

SplitContainer* SplitContainer::findAtPosition(const QPoint& pos) {
    QPoint localPos = mapFromGlobal(pos);

    if (!rect().contains(localPos)) {
        return nullptr;
    }

    if (type_ == Leaf) {
        return this;
    }

    // Check children
    if (child1_) {
        SplitContainer* found = child1_->findAtPosition(pos);
        if (found) return found;
    }

    if (child2_) {
        return child2_->findAtPosition(pos);
    }

    return nullptr;
}

SplitContainer* SplitContainer::findInDirection(Direction dir) {
    // TODO: Implement directional navigation
    // This is complex and requires walking up the tree to find appropriate sibling
    // For now, just return nullptr
    (void)dir;
    return nullptr;
}

void SplitContainer::setFocused(bool focused) {
    if (type_ != Leaf) return;

    focused_ = focused;

    if (terminal_) {
        terminal_->setFocusedBorder(focused);
    }
}

int SplitContainer::countLeaves() const {
    if (type_ == Leaf) {
        return 1;
    }

    int count = 0;
    if (child1_) count += child1_->countLeaves();
    if (child2_) count += child2_->countLeaves();
    return count;
}

void SplitContainer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // Qt's layout system handles resizing automatically
    // No need for manual geometry recalculation like in C version
}

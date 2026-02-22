/*
 * TerminalTab - Container for terminal splits
 */

#include "terminal_tab.h"
#include "split_container.h"
#include <QVBoxLayout>
#include <QDebug>

TerminalTab::TerminalTab(QWidget *parent)
    : QWidget(parent)
    , root_split_(nullptr)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Create initial leaf split
    root_split_ = SplitContainer::createLeaf(this);
    root_split_->setFocused(true);
    layout->addWidget(root_split_);

    qDebug() << "Created TerminalTab with root split";
}

TerminalTab::~TerminalTab() {
}

void TerminalTab::splitHorizontal() {
    if (!root_split_) return;

    // Find focused split
    SplitContainer* focused = root_split_->findFocused();
    if (!focused) {
        qWarning() << "No focused split found";
        return;
    }

    // Perform the split
    SplitContainer* newContainer = focused->splitHorizontal();
    if (newContainer && newContainer->parent() == nullptr) {
        // The split became the new root
        QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(this->layout());
        if (layout) {
            // Remove old root
            layout->removeWidget(root_split_);

            // Add new root
            root_split_ = newContainer;
            root_split_->setParent(this);
            layout->addWidget(root_split_);
        }
    }

    qDebug() << "Split horizontal - leaf count:" << root_split_->countLeaves();
}

void TerminalTab::splitVertical() {
    if (!root_split_) return;

    // Find focused split
    SplitContainer* focused = root_split_->findFocused();
    if (!focused) {
        qWarning() << "No focused split found";
        return;
    }

    // Perform the split
    SplitContainer* newContainer = focused->splitVertical();
    if (newContainer && newContainer->parent() == nullptr) {
        // The split became the new root
        QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(this->layout());
        if (layout) {
            // Remove old root
            layout->removeWidget(root_split_);

            // Add new root
            root_split_ = newContainer;
            root_split_->setParent(this);
            layout->addWidget(root_split_);
        }
    }

    qDebug() << "Split vertical - leaf count:" << root_split_->countLeaves();
}

void TerminalTab::closeSplit() {
    if (!root_split_) return;

    // Find focused split
    SplitContainer* focused = root_split_->findFocused();
    if (!focused || focused == root_split_) {
        qWarning() << "Cannot close root split or no focused split";
        return;
    }

    // Close the split
    focused->closeSplit();

    qDebug() << "Closed split - leaf count:" << root_split_->countLeaves();
}

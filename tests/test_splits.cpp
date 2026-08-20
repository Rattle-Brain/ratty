/*
 * Pane-tree tests.
 *
 * Splitting and closing panes is tree surgery on live Qt widgets, where the
 * failure modes are a destroyed-but-still-referenced node or a surviving node
 * that is never shown again. Both are invisible to a compiler and obvious here.
 *
 * Runs under QT_QPA_PLATFORM=offscreen; the terminals cannot get a GL context
 * there, which is fine because none of this touches rendering.
 */

#include "check.h"
#include "config/config.h"
#include "ui/split_container.h"
#include "ui/terminal_widget.h"
#include <QApplication>
#include <QPointer>
#include <QTabWidget>

namespace {

/* Run the deferred-delete queue so a dangling node shows up as a null
 * QPointer instead of merely being scheduled for destruction. */
void settle() {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/* What MainWindow::installTabRoot does, in miniature. */
void installRoot(QTabWidget* tabs, SplitContainer* root) {
    tabs->removeTab(0);
    tabs->insertTab(0, root, QStringLiteral("t"));
    tabs->setCurrentIndex(0);
    settle();
}

void testSplitKeepsBothPanes(QTabWidget* tabs, SplitContainer*& root) {
    check::section("splitting keeps both panes alive and visible");

    QPointer<SplitContainer> original = root;
    SplitContainer* newRoot = original->splitHorizontal();

    check::that(newRoot != nullptr, "split returns the new root");
    check::that(newRoot != original, "a container was interposed above the leaf");
    check::equal(newRoot->countLeaves(), 2, "the tree has two leaves");
    check::that(!original.isNull(), "the original leaf was not destroyed");
    check::that(original->parentNode() == newRoot, "the original leaf was reparented");

    installRoot(tabs, newRoot);
    check::that(original->isVisible(), "the original pane is visible after reattach");
    check::that(newRoot->child2()->isVisible(), "the new pane is visible");

    root = newRoot;
}

void testNestedSplit(SplitContainer* root) {
    check::section("nested split");

    QPointer<SplitContainer> first = root->child1();
    SplitContainer* second = root->child2();

    SplitContainer* resultRoot = second->splitVertical();
    settle();

    check::that(resultRoot == root, "splitting a non-root leaf leaves the root alone");
    check::equal(resultRoot->countLeaves(), 3, "the tree has three leaves");
    check::that(!first.isNull() && first->isVisible(), "the untouched pane is still visible");
    check::that(second->isVisible(), "the split pane is still visible");
}

void testClosePaneKeepsSibling(SplitContainer* root) {
    check::section("closing a pane promotes its sibling");

    SplitContainer* inner = root->child2();
    SplitContainer* victim = inner->child1();
    QPointer<SplitContainer> sibling = inner->child2();
    QPointer<SplitContainer> obsoleteContainer = inner;
    QPointer<SplitContainer> closed = victim;

    SplitContainer* resultRoot = victim->closePane();
    settle();

    check::that(resultRoot == root, "the root is unchanged");
    check::that(!sibling.isNull(), "the sibling survived");
    check::that(sibling->isVisible(), "the sibling is visible");
    check::that(closed.isNull(), "the closed pane was destroyed");
    check::that(obsoleteContainer.isNull(), "the now-redundant container was destroyed");
    check::equal(resultRoot->countLeaves(), 2, "the tree is back to two leaves");
    check::that(sibling->parentNode() == root, "the sibling was promoted");
}

void testCloseDownToOne(QTabWidget* tabs, SplitContainer* root) {
    check::section("closing down to a single pane");

    QPointer<SplitContainer> survivor = root->child1();
    SplitContainer* resultRoot = root->child2()->closePane();
    settle();

    check::that(!survivor.isNull(), "the last pane survived");
    check::that(resultRoot == survivor, "the surviving leaf became the root");
    check::equal(resultRoot->countLeaves(), 1, "one leaf remains");

    installRoot(tabs, resultRoot);
    check::that(resultRoot->isVisible(), "the sole pane is visible");
    check::that(resultRoot->closePane() == nullptr,
                "closing the only pane reports nullptr so the caller closes the tab");
}

void testDirectionalNavigation(SplitContainer* root) {
    check::section("directional navigation");

    SplitContainer* newRoot = root->splitHorizontal();
    settle();
    SplitContainer* left = newRoot->child1();
    SplitContainer* right = newRoot->child2();

    check::that(left->findInDirection(Qt::Horizontal, true) == right, "left moves to right");
    check::that(right->findInDirection(Qt::Horizontal, false) == left, "right moves to left");
    check::that(left->findInDirection(Qt::Horizontal, false) == nullptr,
                "the left edge has no neighbour");
    check::that(left->findInDirection(Qt::Vertical, true) == nullptr,
                "a horizontal split has no vertical neighbour");
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    Config::instance().load();

    auto* tabs = new QTabWidget();
    tabs->resize(1000, 700);

    SplitContainer* root = SplitContainer::createLeaf(nullptr);
    tabs->addTab(root, QStringLiteral("t"));
    tabs->show();
    settle();

    testSplitKeepsBothPanes(tabs, root);
    testNestedSplit(root);
    testClosePaneKeepsSibling(root);
    testCloseDownToOne(tabs, root);
    testDirectionalNavigation(tabs->count() > 0
                                  ? qobject_cast<SplitContainer*>(tabs->widget(0))
                                  : root);

    delete tabs;
    settle();
    return check::report("test_splits");
}

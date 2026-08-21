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
#include <cstdlib>
#include <string>

namespace {

/* A QSplitter handle is 1px wide and the halves round, so exact equality is the
 * wrong assertion; anything under a few pixels is an even split. */
constexpr int tolerance = 6;
/* TerminalWidget's minimum size. A pane sitting on it is a crushed pane. */
constexpr int minimumPaneHeight = 100;

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

/*
 * Geometry, not just topology. A nested split used to hand the new container to
 * the parent QSplitter with no sizes, and a QSplitter answers that by clamping
 * the widget to its minimum -- so the moment a third pane appeared, the two
 * already in the tab were squeezed into a 100px strip along one edge while the
 * newcomer took everything else. The tree was perfectly correct throughout,
 * which is why the tests above never noticed.
 */
void testNestedSplitKeepsGeometry() {
    check::section("a nested split divides its own pane, not the whole tab");

    QTabWidget tabs;
    tabs.resize(1000, 700);

    SplitContainer* topLeft = SplitContainer::createLeaf(nullptr);
    tabs.addTab(topLeft, QStringLiteral("g"));
    tabs.show();
    settle();

    /* Two panes, stacked. */
    SplitContainer* root = topLeft->splitVertical();
    installRoot(&tabs, root);

    SplitContainer* bottom = root->child2();
    const int evenGap = std::abs(root->child1()->height() - bottom->height());
    check::that(evenGap <= tolerance,
                "two panes divide the tab evenly (gap " + std::to_string(evenGap) + "px)");
    const int halfHeight = bottom->height();

    /* Three panes: the top half splits side by side, and it has to pay for the
     * new pane out of its own half. */
    SplitContainer* sameRoot = topLeft->splitHorizontal();
    settle();

    check::that(sameRoot == root, "splitting a nested leaf leaves the root alone");
    SplitContainer* top = root->child1();
    check::equal(top->countLeaves(), 2, "the top pane became a split of two");

    check::that(std::abs(top->height() - halfHeight) <= tolerance,
                "the nested split keeps the half it inherited (" + std::to_string(top->height())
                    + "px of " + std::to_string(halfHeight) + "px)");
    check::that(std::abs(bottom->height() - halfHeight) <= tolerance,
                "the untouched pane keeps its half (" + std::to_string(bottom->height())
                    + "px of " + std::to_string(halfHeight) + "px)");

    const int sideGap = std::abs(topLeft->width() - top->child2()->width());
    check::that(sideGap <= tolerance,
                "and divides that half evenly, side by side (gap " + std::to_string(sideGap)
                    + "px)");

    /* The failure this all exists to catch: a pane pinned to its minimum. */
    root->forEachLeaf([](SplitContainer* leaf) {
        check::that(leaf->height() > minimumPaneHeight,
                    "no pane is crushed to its minimum (" + std::to_string(leaf->height())
                        + "px)");
    });
}

/* Closing the middle pane of three has to give its space to the sibling that
 * takes its place, not to whichever pane the splitter feels like. */
void testClosingRestoresTheSiblingsShare() {
    check::section("closing a pane hands its space to the promoted sibling");

    QTabWidget tabs;
    tabs.resize(1000, 700);

    SplitContainer* first = SplitContainer::createLeaf(nullptr);
    tabs.addTab(first, QStringLiteral("g"));
    tabs.show();
    settle();

    SplitContainer* root = first->splitVertical();
    installRoot(&tabs, root);
    const int halfHeight = root->child2()->height();

    /* Split the bottom half, then close one of its two panes: the survivor is
     * promoted into the bottom slot and should fill it. */
    SplitContainer* bottom = root->child2();
    check::that(bottom->splitVertical() == root, "the root is unchanged by the nested split");
    settle();

    SplitContainer* survivor = root->child2()->child2();
    check::that(bottom->closePane() == root, "closing reports the same root");
    settle();

    check::that(root->child2() == survivor, "the survivor was promoted");
    check::that(std::abs(survivor->height() - halfHeight) <= tolerance,
                "and fills the half its parent held (" + std::to_string(survivor->height())
                    + "px of " + std::to_string(halfHeight) + "px)");
    check::that(std::abs(root->child1()->height() - halfHeight) <= tolerance,
                "leaving the other pane alone (" + std::to_string(root->child1()->height())
                    + "px of " + std::to_string(halfHeight) + "px)");
}

/*
 * The focus marker, which is the tree's own record of which pane is current.
 * Qt focus cannot play that role: every piece of tree surgery reparents
 * widgets, and reparenting clears it. So the surgery marks a pane and the
 * caller applies Qt focus afterwards -- these assertions cover the marking half.
 */
void testFocusMarkerTracksSurgery() {
    check::section("the focus marker follows splits and closes");

    QTabWidget tabs;
    tabs.resize(1000, 700);
    SplitContainer* first = SplitContainer::createLeaf(nullptr);
    tabs.addTab(first, QStringLiteral("f"));
    tabs.show();
    settle();

    first->focusPane();
    check::that(first->findMarkedPane() == first, "a lone pane is the marked one");

    /* Splitting marks the pane it created, and reports it. */
    SplitContainer* second = nullptr;
    SplitContainer* root = first->splitHorizontal(0.5f, &second);
    installRoot(&tabs, root);

    check::that(second != nullptr, "the split reports the pane it created");
    check::that(second == root->child2(), "which is the second child");
    check::that(root->findMarkedPane() == second, "and that pane is marked");
    check::that(!first->terminal()->isPaneFocused(), "the pane split away from is not");

    /* Splitting the new pane again marks the newest one. */
    SplitContainer* third = nullptr;
    check::that(second->splitVertical(0.5f, &third) == root, "the root is unchanged");
    settle();
    check::that(root->findMarkedPane() == third, "the newest pane is marked");

    /* Closing it marks a live pane -- never the one just destroyed. */
    check::that(third->closePane() == root, "closing reports the same root");
    settle();
    SplitContainer* marked = root->findMarkedPane();
    check::that(marked != nullptr, "a pane is still marked after a close");
    check::that(marked == second, "and it is the promoted sibling");
    check::equal(root->countLeaves(), 2, "two panes remain");
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

    testFocusMarkerTracksSurgery();
    testNestedSplitKeepsGeometry();
    testClosingRestoresTheSiblingsShare();

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

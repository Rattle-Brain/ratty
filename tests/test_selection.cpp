/*
 * Selection tests: what a drag covers, and what comes out of it as text.
 *
 * All of the hard parts of a terminal selection are questions about the buffer
 * rather than about the mouse -- where a word ends, how a multi-row selection
 * follows the text round the end of a row, whether two rows are one wrapped
 * line, what happens to the padding between the last character and the window
 * edge, and what a double-width character counts as. Every one of those is
 * pinned down here, headlessly, because reproducing them by dragging across a
 * window is slow and unrepeatable.
 */

#include "check.h"
#include "core/selection.h"
#include "core/terminal_emulator.h"
#include <string>

namespace {

void feed(TerminalEmulator& term, const std::string& bytes) {
    term.write(bytes.data(), bytes.size());
}

/* UTF-32 back to something printable in a failure message. */
std::string narrow(const std::u32string& text) {
    std::string out;
    for (const char32_t ch : text) {
        if (ch == U'\n') out += "\\n";
        else if (ch >= 32 && ch < 127) out += static_cast<char>(ch);
        else out += '?';
    }
    return out;
}

SelectionPoint at(const Screen& screen, int row, int col) {
    return SelectionPoint{screen.screenTopLine() + row, col};
}

std::string selected(const Screen& screen, const SelectionRange& range,
                     SelectionMode mode = SelectionMode::Character) {
    return narrow(selectionText(screen, range, mode));
}

void testSingleRow() {
    check::section("a selection within one row");

    TerminalEmulator term(4, 20);
    feed(term, "hello world");
    const Screen& screen = term.screen();

    check::equal(selected(screen, {at(screen, 0, 0), at(screen, 0, 4)}), std::string("hello"),
                 "cell to cell, both ends included");
    check::equal(selected(screen, {at(screen, 0, 6), at(screen, 0, 6)}), std::string("w"),
                 "a single cell");
    /* Dragging right to left is the same selection. */
    check::equal(selected(screen, {at(screen, 0, 4), at(screen, 0, 0)}), std::string("hello"),
                 "a backwards drag normalizes");
}

void testTrailingBlanks() {
    check::section("padding to the window edge is not text");

    TerminalEmulator term(4, 20);
    feed(term, "hi\r\nthere");
    const Screen& screen = term.screen();

    check::equal(selected(screen, {at(screen, 0, 0), at(screen, 0, 19)}), std::string("hi"),
                 "a whole row comes back without its trailing blanks");
    check::equal(selected(screen, {at(screen, 0, 0), at(screen, 1, 19)}),
                 std::string("hi\\nthere"),
                 "and neither row drags its padding into the join");

    /* Blanks the user deliberately selected mid-row are kept: the selection
     * does not reach the margin, so they are not padding. */
    check::equal(selected(screen, {at(screen, 0, 0), at(screen, 0, 4)}), std::string("hi   "),
                 "blanks inside the selection are kept");
}

void testWrappedLineJoins() {
    check::section("a wrapped line comes back as one line");

    TerminalEmulator term(4, 8);
    feed(term, "echo abcdefgh");   // wraps after "echo abc"
    const Screen& screen = term.screen();

    check::equal(selected(screen, {at(screen, 0, 0), at(screen, 1, 7)}),
                 std::string("echo abcdefgh"),
                 "no newline is inserted at a wrap seam");

    /* Two rows that merely follow each other keep their newline. */
    TerminalEmulator hard(4, 8);
    feed(hard, "one\r\ntwo");
    check::equal(selected(hard.screen(), {at(hard.screen(), 0, 0), at(hard.screen(), 1, 7)}),
                 std::string("one\\ntwo"), "a real line break survives");
}

void testMultiRowFollowsTheText() {
    check::section("a multi-row selection runs to the margin and back");

    TerminalEmulator term(4, 10);
    feed(term, "aaaa\r\nbbbb\r\ncccc");
    const Screen& screen = term.screen();

    /* From the middle of the first row to the middle of the third. */
    check::equal(selected(screen, {at(screen, 0, 2), at(screen, 2, 1)}),
                 std::string("aa\\nbbbb\\ncc"),
                 "the rows in between are taken whole");
}

void testBlockSelection() {
    check::section("a block selection takes a rectangle");

    TerminalEmulator term(4, 12);
    feed(term, "one   alpha\r\ntwo   beta\r\nthree gamma");
    const Screen& screen = term.screen();

    const SelectionRange rectangle{at(screen, 0, 6), at(screen, 2, 10)};
    check::equal(selected(screen, rectangle, SelectionMode::Block),
                 std::string("alpha\\nbeta\\ngamma"),
                 "the same columns from every row, one line each");

    /* A block never joins rows, even across a seam. */
    TerminalEmulator wrapped(4, 6);
    feed(wrapped, "abcdefghij");
    const SelectionRange column{at(wrapped.screen(), 0, 0), at(wrapped.screen(), 1, 1)};
    check::equal(selected(wrapped.screen(), column, SelectionMode::Block),
                 std::string("ab\\ngh"), "a block keeps its rows apart");
}

void testWideCharacters() {
    check::section("a double-width character is copied once");

    TerminalEmulator term(4, 10);
    feed(term, "\xe6\x97\xa5x");   // U+65E5, then 'x'
    const Screen& screen = term.screen();

    const std::u32string text = selectionText(screen, {at(screen, 0, 0), at(screen, 0, 2)},
                                              SelectionMode::Character);
    check::equal(text.size(), size_t{2}, "two characters, not three: the trailer is skipped");
    check::equal(static_cast<unsigned>(text[0]), 0x65E5u, "the wide character itself");
    check::equal(static_cast<unsigned>(text[1]), static_cast<unsigned>(U'x'), "then the next");

    /* Starting on the trailer half still yields the character. */
    const std::u32string fromTrailer =
        selectionText(screen, {at(screen, 0, 1), at(screen, 0, 1)}, SelectionMode::Character);
    check::equal(fromTrailer.size(), size_t{1}, "selecting the trailer selects its character");
    check::equal(static_cast<unsigned>(fromTrailer[0]), 0x65E5u, "and it is the right one");
}

void testWordExpansion() {
    check::section("double-click snaps to a word");

    TerminalEmulator term(4, 40);
    feed(term, "run /usr/local/bin/thing --flag=on, ok");
    const Screen& screen = term.screen();

    check::equal(selected(screen, wordAt(screen, at(screen, 0, 1))), std::string("run"),
                 "a plain word");
    check::equal(selected(screen, wordAt(screen, at(screen, 0, 8))),
                 std::string("/usr/local/bin/thing"),
                 "a path is one word, slashes and all");
    check::equal(selected(screen, wordAt(screen, at(screen, 0, 27))),
                 std::string("--flag=on"),
                 "so is a flag with a value, and the comma ends it");
    check::equal(selected(screen, wordAt(screen, at(screen, 0, 3))), std::string(" "),
                 "a click on a blank selects the run of blanks");

    /* A word that wrapped is still one word. */
    TerminalEmulator wrapped(4, 8);
    feed(wrapped, "aa /very/long/path");
    check::equal(selected(wrapped.screen(), wordAt(wrapped.screen(), at(wrapped.screen(), 1, 2))),
                 std::string("/very/long/path"),
                 "a word is followed across the seam");
}

void testLineExpansion() {
    check::section("triple-click takes the whole logical line");

    TerminalEmulator term(6, 8);
    feed(term, "first\r\necho abcdefghij\r\nlast");
    const Screen& screen = term.screen();

    /* The middle line wraps over two rows; clicking either takes both. */
    const SelectionRange fromFirstRow = logicalLineAt(screen, at(screen, 1, 3));
    const SelectionRange fromSecondRow = logicalLineAt(screen, at(screen, 2, 1));
    check::equal(selected(screen, fromFirstRow), std::string("echo abcdefghij"),
                 "clicking the first row of a wrapped line takes all of it");
    check::equal(selected(screen, fromSecondRow), std::string("echo abcdefghij"),
                 "and so does clicking its continuation");
    check::equal(selected(screen, logicalLineAt(screen, at(screen, 0, 2))), std::string("first"),
                 "a line that did not wrap is just itself");
}

void testScrollbackIsSelectable() {
    check::section("history rows can be selected");

    TerminalEmulator term(2, 10);
    feed(term, "one\r\ntwo\r\nthree\r\nfour");
    const Screen& screen = term.screen();
    check::equal(term.historySize(), 2, "two rows are in the history");

    const int64_t first = screen.firstLine();
    check::equal(selected(screen, {{first, 0}, {first + 1, 9}}), std::string("one\\ntwo"),
                 "and they read back as text");
    /* Across the boundary between history and live screen. */
    check::equal(selected(screen, {{first + 1, 0}, {first + 2, 9}}), std::string("two\\nthree"),
                 "including across the join with the live screen");
}

void testSelectionSurvivesOutput() {
    check::section("a selection stays on its text while output arrives");

    TerminalEmulator term(4, 10);
    feed(term, "target\r\n");
    const Screen& screen = term.screen();

    Selection selection;
    selection.begin(screen, {screen.screenTopLine(), 0}, SelectionMode::Line);
    selection.finishDrag();
    check::equal(narrow(selection.text(screen)), std::string("target"), "the line is selected");

    /* Enough output to scroll it off the screen and into the history. */
    feed(term, "a\r\nb\r\nc\r\nd\r\ne\r\n");
    check::equal(narrow(selection.text(screen)), std::string("target"),
                 "and it is still the same text after a screenful of output");
}

void testDragStateMachine() {
    check::section("the drag: press, move, release");

    TerminalEmulator term(4, 20);
    feed(term, "alpha beta gamma");
    const Screen& screen = term.screen();

    Selection selection;
    check::that(selection.isEmpty(), "nothing is selected to begin with");

    selection.begin(screen, at(screen, 0, 0), SelectionMode::Character);
    check::that(!selection.isEmpty() && selection.isDragging(), "a press starts a drag");
    selection.extend(screen, at(screen, 0, 4));
    check::equal(narrow(selection.text(screen)), std::string("alpha"), "moving extends it");
    selection.finishDrag();
    check::that(!selection.isDragging() && !selection.isEmpty(),
                "the release ends the drag and keeps the selection");
    selection.extend(screen, at(screen, 0, 9));
    check::equal(narrow(selection.text(screen)), std::string("alpha"),
                 "and moving afterwards changes nothing");

    /* A word drag grows by whole words. */
    selection.begin(screen, at(screen, 0, 1), SelectionMode::Word);
    check::equal(narrow(selection.text(screen)), std::string("alpha"),
                 "a word drag starts on the whole word");
    selection.extend(screen, at(screen, 0, 7));
    check::equal(narrow(selection.text(screen)), std::string("alpha beta"),
                 "and extends to whole words");

    selection.clear();
    check::that(selection.isEmpty(), "clearing empties it");
    check::equal(narrow(selection.text(screen)), std::string(""), "with no text left");
}

void testHighlightGeometry() {
    check::section("which columns to paint on each row");

    const SelectionRange range{{100, 3}, {102, 5}};
    int first = 0;
    int last = 0;

    check::that(rangeColumnsOn(range, SelectionMode::Character, 100, 10, first, last),
                "the first row is in the selection");
    check::equal(first, 3, "starting where the drag began");
    check::equal(last, 9, "and running to the right margin");

    check::that(rangeColumnsOn(range, SelectionMode::Character, 101, 10, first, last),
                "the row in between is in it");
    check::equal(first, 0, "whole row: from the left");
    check::equal(last, 9, "to the right");

    check::that(rangeColumnsOn(range, SelectionMode::Character, 102, 10, first, last),
                "and so is the last row");
    check::equal(last, 5, "ending where the drag ended");

    check::that(!rangeColumnsOn(range, SelectionMode::Character, 103, 10, first, last),
                "a row past the end is not");
    check::that(!rangeColumnsOn(range, SelectionMode::Character, 99, 10, first, last),
                "and neither is one before the start");

    /* A block selection is the same columns on every row. */
    const SelectionRange block{{100, 7}, {102, 2}};
    check::that(rangeColumnsOn(block, SelectionMode::Block, 101, 10, first, last),
                "a block covers the rows between its corners");
    check::equal(first, 2, "left edge from the leftmost corner");
    check::equal(last, 7, "right edge from the rightmost");
}

} // namespace

int main() {
    testSingleRow();
    testTrailingBlanks();
    testWrappedLineJoins();
    testMultiRowFollowsTheText();
    testBlockSelection();
    testWideCharacters();
    testWordExpansion();
    testLineExpansion();
    testScrollbackIsSelectable();
    testSelectionSurvivesOutput();
    testDragStateMachine();
    testHighlightGeometry();
    return check::report("test_selection");
}

/*
 * Reflow tests: the wrap seam, and rewrapping the buffer on a width change.
 *
 * A row is a slice of the window, not a line of a document, so the only thing
 * that says whether two rows are one wrapped line or two separate ones is the
 * seam CellFlagWrapped records. Everything here is really a test of that bit:
 * that it is set exactly when the text ran into the right margin, that it
 * survives a trip through the compressed scrollback, and that a resize takes
 * the buffer apart along those seams and puts it back together at the new width
 * without losing, duplicating or running together anything.
 *
 * Headless: this is all Screen and TerminalEmulator.
 */

#include "check.h"
#include "core/terminal_emulator.h"
#include <string>

namespace {

Palette palette;

void feed(TerminalEmulator& term, const std::string& bytes) {
    term.write(bytes.data(), bytes.size());
}

/* Text of one row of the live screen, trailing blanks stripped. */
std::string rowText(const Screen& screen, int row) {
    std::string out;
    for (int col = 0; col < screen.cols(); ++col) {
        const char32_t ch = screen.at(row, col).ch;
        out += (ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

/* Text of one line by stable line number, trailing blanks stripped. */
std::string lineText(const Screen& screen, int64_t line) {
    int length = 0;
    const Cell* cells = screen.lineData(line, length);
    std::string out;
    for (int col = 0; col < length; ++col) {
        const char32_t ch = cells[col].ch;
        out += (ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

/* One cell, addressed by stable line number. */
const Cell& cellAtLine(const Screen& screen, int64_t line, int col) {
    static const Cell blank{};
    int length = 0;
    const Cell* cells = screen.lineData(line, length);
    if (!cells || col < 0 || col >= length) return blank;
    return cells[col];
}

/* Every line the buffer holds, oldest first, joined with '|'. */
std::string wholeBuffer(const Screen& screen) {
    std::string out;
    for (int64_t line = screen.firstLine(); line <= screen.lastLine(); ++line) {
        if (!out.empty()) out += '|';
        out += lineText(screen, line);
    }
    return out;
}

void testSeamIsRecorded() {
    check::section("a row that ran into the margin is marked as wrapped");

    TerminalEmulator term(4, 8);
    feed(term, "abcdefghij");   // 10 characters into an 8-column window

    check::equal(rowText(term.screen(), 0), std::string("abcdefgh"), "the first row filled up");
    check::equal(rowText(term.screen(), 1), std::string("ij"), "and the rest wrapped");
    check::that(term.screen().lineWrapped(term.screen().screenTopLine()),
                "the seam is recorded on the row that wrapped");
    check::that(!term.screen().lineWrapped(term.screen().screenTopLine() + 1),
                "and not on the row that merely follows it");

    /* A newline is not a seam, however full the row it ends. */
    TerminalEmulator hard(4, 8);
    feed(hard, "abcdefgh\r\nij");
    check::equal(rowText(hard.screen(), 0), std::string("abcdefgh"), "a full row");
    check::that(!hard.screen().lineWrapped(hard.screen().screenTopLine()),
                "ended by a newline carries no seam");

    /* Autowrap off means the text piles up in the last column: no seam. */
    TerminalEmulator noWrap(4, 8);
    feed(noWrap, "\x1b[?7l" "abcdefghij");
    check::that(!noWrap.screen().lineWrapped(noWrap.screen().screenTopLine()),
                "with DECAWM off there is no wrap to record");
}

void testSeamSurvivesTheScrollback() {
    check::section("the seam survives a trip through the compressed history");

    TerminalEmulator term(2, 6);
    feed(term, "abcdefgh\r\nxy\r\nzz");

    check::that(term.historySize() >= 1, "rows have scrolled off");
    const int64_t first = term.screen().firstLine();
    check::equal(lineText(term.screen(), first), std::string("abcdef"),
                 "the wrapped row is in the history");
    check::that(term.screen().lineWrapped(first),
                "and it still knows that it continues on the next line");
}

void testNarrowingRewraps() {
    check::section("narrowing rewraps a wrapped line instead of truncating it");

    TerminalEmulator term(6, 10);
    feed(term, "0123456789abcdef");   // wraps once at 10 columns

    term.resize(6, 6);
    check::equal(term.cols(), 6, "the width changed");
    /* 16 characters at 6 columns is three rows. */
    check::equal(wholeBuffer(term.screen()).substr(0, 20), std::string("012345|6789ab|cdef||"),
                 "the logical line was laid out again at the new width");
    check::that(term.screen().lineWrapped(term.screen().firstLine()),
                "the first row of it still ends in a seam");
    check::that(!term.screen().lineWrapped(term.screen().firstLine() + 2),
                "and the last row of it does not");
}

void testWideningRejoins() {
    check::section("widening joins the pieces back up");

    TerminalEmulator term(6, 6);
    feed(term, "0123456789abcdef");

    term.resize(6, 20);
    check::equal(lineText(term.screen(), term.screen().firstLine()),
                 std::string("0123456789abcdef"),
                 "a line that had wrapped three times is one row again");
    check::that(!term.screen().lineWrapped(term.screen().firstLine()),
                "with no seam left in it");
}

void testHardLinesAreNeverJoined() {
    check::section("rows that merely follow each other stay separate lines");

    /* What a TUI draws: short rows, each ended deliberately. Rewrapping these
     * into a paragraph is the failure mode the seam exists to prevent. */
    TerminalEmulator term(6, 20);
    feed(term, "+---+\r\n| a |\r\n+---+");

    term.resize(6, 40);
    const std::string buffer = wholeBuffer(term.screen());
    check::that(buffer.find("+---+|| a ||+---+") != std::string::npos
                    || buffer.find("+---+| a |+---+") != std::string::npos,
                "the three rows are still three rows (" + buffer + ")");
    check::equal(lineText(term.screen(), term.screen().screenTopLine()),
                 std::string("+---+"), "the first is unchanged");
    check::equal(lineText(term.screen(), term.screen().screenTopLine() + 1),
                 std::string("| a |"), "and so is the second");
}

void testHistoryIsRewrapped() {
    check::section("the scrollback is rewrapped too, not just the screen");

    TerminalEmulator term(2, 10);
    /* Wraps once, then two more lines push it into the history. */
    feed(term, "0123456789abcde\r\nnext\r\nlast");
    check::that(term.historySize() >= 2, "the wrapped line is in the history");

    term.resize(2, 5);
    /* "0123456789abcde" is exactly three rows of five. */
    const int64_t first = term.screen().firstLine();
    check::equal(lineText(term.screen(), first), std::string("01234"),
                 "a history row was rewrapped");
    check::equal(lineText(term.screen(), first + 1), std::string("56789"), "and so was the next");
    check::equal(lineText(term.screen(), first + 2), std::string("abcde"), "and the last piece");
    check::equal(lineText(term.screen(), first + 3), std::string("next"),
                 "the line after it is still its own line");
}

void testCursorFollowsItsText() {
    check::section("the cursor lands where its text went");

    TerminalEmulator term(6, 10);
    feed(term, "0123456789abc");   // cursor after 'c', on the wrapped second row

    term.resize(6, 5);
    /* 13 characters at 5 columns: rows "01234", "56789", "abc" with the cursor
     * just past the 'c'. */
    check::equal(term.screen().cursorCol(), 3, "the cursor kept its place in the line");
    check::equal(lineText(term.screen(), term.screen().screenTopLine()
                                            + term.screen().cursorRow()),
                 std::string("abc"), "on the row that text ended up on");
}

void testTrailingBlanksAreNotText() {
    check::section("padding to the old width is not carried over");

    TerminalEmulator term(4, 20);
    feed(term, "hi");

    term.resize(4, 6);
    check::equal(lineText(term.screen(), term.screen().screenTopLine()), std::string("hi"),
                 "a short line stays short");
    check::equal(lineText(term.screen(), term.screen().screenTopLine() + 1), std::string(""),
                 "and does not spill 18 spaces onto the next row");
    check::equal(term.screen().cursorRow(), 0, "the cursor is still on it");
    check::equal(term.screen().cursorCol(), 2, "just past the text");
}

void testWideCharactersAreNotSplit() {
    check::section("a double-width character is not split across the margin");

    TerminalEmulator term(4, 10);
    /* Five double-width characters fill exactly ten columns. */
    feed(term, "\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5\xe6\x97\xa5");

    term.resize(4, 5);
    /*
     * Five columns hold two of them and a blank, so ten columns become three
     * rows -- one more than the screen had, so the first is pushed into the
     * history, which is what narrowing does.
     */
    const Screen& screen = term.screen();
    const int64_t first = screen.firstLine();
    check::equal(static_cast<unsigned>(cellAtLine(screen, first, 0).ch), 0x65E5u,
                 "the first character is at the start of the first row");
    check::that(cellAtLine(screen, first, 1).hasFlag(CellFlagWideTrailer), "with its trailer");
    check::equal(static_cast<unsigned>(cellAtLine(screen, first, 2).ch), 0x65E5u,
                 "the second follows it");
    check::equal(static_cast<unsigned>(cellAtLine(screen, first, 4).ch),
                 static_cast<unsigned>(U' '),
                 "and the odd column is left blank rather than half a character");
    check::that(screen.lineWrapped(first), "the row ends in a seam");
    check::equal(static_cast<unsigned>(cellAtLine(screen, first + 1, 0).ch), 0x65E5u,
                 "so the character that did not fit is at the start of the next row");
    check::equal(static_cast<unsigned>(cellAtLine(screen, first + 2, 0).ch), 0x65E5u,
                 "and the last one is on a third row");
}

void testAlternateScreenIsNotRewrapped() {
    check::section("the alternate screen is left alone");

    TerminalEmulator term(4, 10);
    feed(term, "\x1b[?1049h");     // alternate screen up
    feed(term, "0123456789abcde");
    term.resize(4, 5);

    /* No reflow, so the old behaviour applies: the row is truncated to the new
     * width and the application is expected to redraw. */
    check::equal(rowText(term.screen(), 0), std::string("01234"),
                 "a full-screen application's layout is truncated, not rewrapped");
    check::equal(rowText(term.screen(), 1), std::string("abcde"),
                 "and its second row is still its second row");
}

void testStableLineNumbers() {
    check::section("a line number keeps naming the same text");

    TerminalEmulator term(3, 10);
    term.setScrollbackLines(2);
    feed(term, "one\r\ntwo\r\nthree\r\n");

    const int64_t line = term.screen().firstLine();
    const std::string before = lineText(term.screen(), line);
    check::equal(before, std::string("one"), "the oldest line kept");

    /* Enough output to evict it. The number must stop resolving rather than
     * come to mean whatever took its place. */
    feed(term, "four\r\nfive\r\nsix\r\n");
    check::that(term.screen().firstLine() > line, "the origin moved past it");
    int length = 0;
    check::that(term.screen().lineData(line, length) == nullptr,
                "and the old number resolves to nothing");

    /* Numbering is monotonic across a reflow, so a stale anchor cannot alias. */
    const int64_t last = term.screen().lastLine();
    term.resize(3, 4);
    check::that(term.screen().firstLine() > last,
                "a reflow renumbers past the end of the old buffer");
}

void testHeightOnlyResizeKeepsWorking() {
    check::section("a height change still moves rows into the history");

    TerminalEmulator term(4, 10);
    feed(term, "aa\r\nbb\r\ncc\r\ndd");
    term.resize(2, 10);
    check::equal(term.historySize(), 2, "the rows dropped from the top became history");
    check::equal(rowText(term.screen(), 0), std::string("cc"), "the bottom rows stayed");
}

} // namespace

int main() {
    testSeamIsRecorded();
    testSeamSurvivesTheScrollback();
    testNarrowingRewraps();
    testWideningRejoins();
    testHardLinesAreNeverJoined();
    testHistoryIsRewrapped();
    testCursorFollowsItsText();
    testTrailingBlanksAreNotText();
    testWideCharactersAreNotSplit();
    testAlternateScreenIsNotRewrapped();
    testStableLineNumbers();
    testHeightOnlyResizeKeepsWorking();
    return check::report("test_reflow");
}

/*
 * Scrollback storage tests.
 *
 * HistoryLine compresses a row (trailing blanks dropped, attributes run-length
 * encoded, characters narrowed to the smallest fixed width that fits). The
 * compression is an implementation detail, so what these pin down is that it is
 * *lossless* for everything a terminal can put in a cell -- and that the row
 * really does get smaller, since that is the whole point.
 */

#include "check.h"
#include "core/history.h"
#include "core/terminal_emulator.h"
#include <string>
#include <vector>

namespace {

/* Round-trip `cells` through the encoding and report what came back. */
std::vector<Cell> roundTrip(const std::vector<Cell>& cells, size_t& bytes) {
    HistoryLine line;
    line.encode(cells.data(), static_cast<int>(cells.size()));
    bytes = line.byteSize();

    /* Columns past the stored width are default blanks, exactly as Screen
     * reports them, so start from a blank row of the original width. */
    std::vector<Cell> out(cells.size(), Cell{});
    line.decode(out.data());
    return out;
}

bool sameCell(const Cell& a, const Cell& b) {
    return a.ch == b.ch && a.flags == b.flags && a.fg == b.fg && a.bg == b.bg;
}

/* True when the round trip reproduced every cell. */
bool losslessFor(const std::vector<Cell>& cells, size_t& bytes) {
    const std::vector<Cell> back = roundTrip(cells, bytes);
    if (back.size() != cells.size()) return false;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (!sameCell(cells[i], back[i])) return false;
    }
    return true;
}

std::vector<Cell> blankRow(int cols) { return std::vector<Cell>(static_cast<size_t>(cols), Cell{}); }

void testLossless() {
    check::section("HistoryLine round-trips every kind of cell");

    size_t bytes = 0;
    const int cols = 200;

    {
        std::vector<Cell> row = blankRow(cols);
        check::that(losslessFor(row, bytes), "an entirely blank row");
        check::equal(bytes, size_t(0), "  and stores no bytes at all");
    }
    {
        /* Plain ASCII, the overwhelmingly common case. */
        std::vector<Cell> row = blankRow(cols);
        const std::string text = "make -j8 && ./build/ratty";
        for (size_t i = 0; i < text.size(); ++i) row[i].ch = static_cast<char32_t>(text[i]);
        check::that(losslessFor(row, bytes), "an ASCII shell line");
        check::that(bytes < 64, "  and its blank tail costs nothing "
                                "(" + std::to_string(bytes) + " bytes)");
    }
    {
        /* A cell carrying every attribute at once. */
        std::vector<Cell> row = blankRow(cols);
        row[0].ch = U'X';
        row[0].fg = Color::rgb(11, 22, 33);
        row[0].bg = Color::indexed(207);
        row[0].flags = CellFlagBold | CellFlagItalic | CellFlagUnderline
                     | CellFlagStrike | CellFlagInverse | CellFlagBlink
                     | CellFlagFaint | CellFlagInvisible;
        check::that(losslessFor(row, bytes), "truecolour, indexed colour and every flag");
    }
    {
        /* Wide characters, emoji and the flags that go with them. */
        std::vector<Cell> row = blankRow(cols);
        row[0].ch = U'你';                       // CJK, 2 columns
        row[1].flags = CellFlagWideTrailer;
        row[2].ch = U'\U0001F600';                   // emoji, above the BMP
        row[2].flags = CellFlagEmojiPresentation;
        row[3].flags = CellFlagWideTrailer;
        row[4].ch = U'─';                       // box drawing
        check::that(losslessFor(row, bytes), "CJK, astral-plane emoji and box drawing");
    }
    {
        /* A zero code point is not a space, and must not be mistaken for one. */
        std::vector<Cell> row = blankRow(cols);
        row[0].ch = U'a';
        row[cols - 1].ch = 0;
        check::that(losslessFor(row, bytes), "a NUL code point in the last column");
    }
    {
        /* A coloured bar: blank cells that must survive because their
         * background is not the default. */
        std::vector<Cell> row = blankRow(cols);
        for (int i = 0; i < cols; ++i) row[i].bg = Color::indexed(4);
        check::that(losslessFor(row, bytes), "a full-width coloured bar of blanks");
    }
    {
        /* Alternating attributes every cell: the worst case for run encoding. */
        std::vector<Cell> row = blankRow(cols);
        for (int i = 0; i < cols; ++i) {
            row[i].ch = static_cast<char32_t>(U'a' + (i % 26));
            row[i].fg = Color::indexed(static_cast<uint8_t>(i));
        }
        check::that(losslessFor(row, bytes), "a different colour in every column");
        check::that(bytes <= size_t(cols) * sizeof(Cell),
                    "  and even then is no larger than the raw cells "
                    "(" + std::to_string(bytes) + " vs "
                    + std::to_string(cols * sizeof(Cell)) + ")");
    }
}

void testCompression() {
    check::section("HistoryLine is substantially smaller than raw cells");

    const int cols = 200;
    const size_t raw = static_cast<size_t>(cols) * sizeof(Cell);
    size_t bytes = 0;

    std::vector<Cell> row = blankRow(cols);
    for (int i = 0; i < cols; ++i) row[i].ch = static_cast<char32_t>(U'a' + (i % 26));
    losslessFor(row, bytes);
    check::that(bytes * 8 < raw, "a full-width ASCII row is at least 8x smaller "
                                 "(" + std::to_string(bytes) + " vs "
                                 + std::to_string(raw) + " bytes)");
}

/* The behaviour Screen promises on top of the storage. */
void testScreenSemantics() {
    check::section("Scrollback still reads back correctly through Screen");

    TerminalEmulator term(4, 20);
    term.setScrollbackLines(100);

    /* Push a known sequence of lines into the history. */
    for (int i = 0; i < 10; ++i) {
        const std::string line = "line" + std::to_string(i) + "\r\n";
        term.write(line.data(), line.size());
    }

    /* Ten lines each ending in a newline leaves the cursor on an eleventh row,
     * so seven rows have left a four-row screen. */
    check::equal(term.historySize(), 7, "seven rows scrolled off a four-row screen");

    term.scrollViewToTop();
    const Screen& screen = term.screen();

    auto textAt = [&](int row) {
        std::string out;
        for (int col = 0; col < screen.cols(); ++col) {
            const char32_t ch = screen.viewAt(row, col).ch;
            if (ch == U' ' || ch == 0) continue;
            out += static_cast<char>(ch);
        }
        return out;
    };
    check::equal(textAt(0), std::string("line0"), "the oldest history row reads back");
    check::equal(textAt(1), std::string("line1"), "and the one after it");

    /* viewRow must agree with viewAt, including the stored length. */
    int length = 0;
    const Cell* cells = screen.viewRow(0, length);
    check::that(cells != nullptr, "viewRow returns the history row");
    check::equal(length, 5, "trimmed to the text actually stored");
    std::string viaRow;
    for (int i = 0; i < length; ++i) viaRow += static_cast<char>(cells[i].ch);
    check::equal(viaRow, std::string("line0"), "and holds the same text as viewAt");

    /* Columns past the stored width are blank, not stale. */
    check::equal(static_cast<uint32_t>(screen.viewAt(0, 19).ch), uint32_t(U' '),
                 "a column past the stored width is blank");

    /* A coloured background in the tail must survive the trim. */
    TerminalEmulator bar(2, 10);
    bar.setScrollbackLines(10);
    bar.write("\x1b[44m", 5);          // blue background
    bar.write("\x1b[2K", 4);           // erase the line, keeping the background
    bar.write("\r\n\r\n\r\n", 6);
    bar.scrollViewToTop();
    check::that(bar.screen().viewAt(0, 9).bg == Color::indexed(4),
                "an erased line keeps its background all the way to the margin");
}

} // namespace

int main() {
    testLossless();
    testCompression();
    testScreenSemantics();
    return check::report("test_history");
}

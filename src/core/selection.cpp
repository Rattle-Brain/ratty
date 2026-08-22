/*
 * Selection - implementation
 */

#include "selection.h"
#include "unicode.h"
#include <algorithm>

namespace {

/*
 * One row at a time, honouring Screen's contract that a history row is handed
 * out through a shared decode buffer and stays valid only until the next call.
 *
 * Every function below uses exactly one of these, which is what makes holding a
 * row pointer safe: nothing else asks the screen for a row in between.
 */
class RowReader {
public:
    explicit RowReader(const Screen& screen) : screen_(screen) {}

    void seek(int64_t line) {
        if (line == line_ && seeked_) return;
        cells_ = screen_.lineData(line, length_);
        if (!cells_) length_ = 0;
        line_ = line;
        seeked_ = true;
    }

    int length() const { return length_; }
    const Cell* cells() const { return cells_; }

    /* The cell at `col`, or a blank for a column the row does not reach. */
    const Cell& at(int col) const {
        static const Cell blank{};
        if (!cells_ || col < 0 || col >= length_) return blank;
        return cells_[col];
    }

private:
    const Screen& screen_;
    const Cell* cells_ = nullptr;
    int length_ = 0;
    int64_t line_ = 0;
    bool seeked_ = false;
};

/*
 * Word characters.
 *
 * Letters and digits, everything outside ASCII that is not a space, and the
 * punctuation that lives *inside* what a terminal user double-clicks: path
 * separators, dots, dashes, and the handful of characters a URL or a flag is
 * made of. What is left out is what ends a word in a shell: quotes, brackets,
 * the pipe, the comma, the semicolon.
 */
bool isWordChar(char32_t ch) {
    if (ch == 0 || ch == U' ' || isSpaceSeparator(ch)) return false;
    if (ch >= 0x80) return true;
    if ((ch >= U'0' && ch <= U'9') || (ch >= U'A' && ch <= U'Z')
        || (ch >= U'a' && ch <= U'z')) {
        return true;
    }
    switch (ch) {
    case U'_': case U'-': case U'.': case U'/': case U'\\': case U'~':
    case U':': case U'@': case U'+': case U'=': case U'%': case U'#':
    case U'&': case U'?': case U'*': case U'$': case U'^':
        return true;
    default:
        return false;
    }
}

/* The first and last row of the logical line `line` belongs to. */
void logicalLineBounds(const Screen& screen, int64_t line, int64_t& first, int64_t& last) {
    /* A line number from outside the buffer -- one the caller has been holding
     * since before an eviction -- resolves to the nearest line there is. */
    const int64_t clamped = std::clamp(line, screen.firstLine(), screen.lastLine());
    first = clamped;
    last = clamped;

    while (first > screen.firstLine() && screen.lineWrapped(first - 1)) --first;
    while (last < screen.lastLine() && screen.lineWrapped(last)) ++last;
}

} // namespace

/* ------------------------------------------------------------- expansion */

SelectionRange logicalLineAt(const Screen& screen, const SelectionPoint& point) {
    int64_t first = 0;
    int64_t last = 0;
    logicalLineBounds(screen, point.line, first, last);
    return SelectionRange{{first, 0}, {last, std::max(0, screen.cols() - 1)}};
}

SelectionRange wordAt(const Screen& screen, const SelectionPoint& point) {
    const int cols = screen.cols();
    if (cols <= 0) return SelectionRange{point, point};

    int64_t first = 0;
    int64_t last = 0;
    logicalLineBounds(screen, point.line, first, last);

    /*
     * Positions within the logical line. Every row of a wrapped line but the
     * last is exactly `cols` wide -- that is what wrapping means -- so the
     * mapping between a position and a (row, column) pair is plain arithmetic.
     */
    const int64_t total = (last - first + 1) * cols;
    const int64_t clicked = (point.line - first) * cols + std::clamp(point.col, 0, cols - 1);

    RowReader reader(screen);
    auto charAt = [&](int64_t position) -> char32_t {
        reader.seek(first + position / cols);
        const Cell& cell = reader.at(static_cast<int>(position % cols));
        /* A trailer belongs to the character in front of it, so it must not be
         * mistaken for a blank and end the word there. */
        if (cell.hasFlag(CellFlagWideTrailer)) return U'x';
        return cell.ch;
    };

    /*
     * A click on a blank selects the run of blanks. Without this a
     * double-click in the empty part of a line would select nothing at all,
     * which reads as the terminal ignoring it.
     */
    const bool wanted = isWordChar(charAt(clicked));

    int64_t begin = clicked;
    while (begin > 0 && isWordChar(charAt(begin - 1)) == wanted) --begin;
    int64_t end = clicked;
    while (end + 1 < total && isWordChar(charAt(end + 1)) == wanted) ++end;

    return SelectionRange{{first + begin / cols, static_cast<int>(begin % cols)},
                          {first + end / cols, static_cast<int>(end % cols)}};
}

/* --------------------------------------------------------------- geometry */

bool rangeColumnsOn(const SelectionRange& range, SelectionMode mode, int64_t line,
                    int cols, int& firstCol, int& lastCol) {
    if (cols <= 0) return false;

    const SelectionRange span = range.normalized();
    if (line < span.start.line || line > span.end.line) return false;

    if (mode == SelectionMode::Block) {
        firstCol = std::clamp(std::min(span.start.col, span.end.col), 0, cols - 1);
        lastCol = std::clamp(std::max(span.start.col, span.end.col), 0, cols - 1);
        return true;
    }

    /*
     * A character selection runs from its start to the right margin, through
     * whole rows, and up to its end on the last one: the text carries on round
     * the end of a row, so the selection has to as well.
     */
    firstCol = line == span.start.line ? std::clamp(span.start.col, 0, cols - 1) : 0;
    lastCol = line == span.end.line ? std::clamp(span.end.col, 0, cols - 1) : cols - 1;
    return firstCol <= lastCol;
}

/* ------------------------------------------------------------------ text */

std::u32string selectionText(const Screen& screen, const SelectionRange& range,
                             SelectionMode mode) {
    const int cols = screen.cols();
    const SelectionRange span = range.normalized();

    std::u32string out;
    RowReader reader(screen);

    for (int64_t line = span.start.line; line <= span.end.line; ++line) {
        int firstCol = 0;
        int lastCol = 0;
        if (!rangeColumnsOn(span, mode, line, cols, firstCol, lastCol)) continue;

        reader.seek(line);

        /* A span starting on the trailer half of a double-width character
         * should still yield the character. */
        if (firstCol > 0 && reader.at(firstCol).hasFlag(CellFlagWideTrailer)) --firstCol;

        std::u32string row;
        row.reserve(static_cast<size_t>(lastCol - firstCol + 1));
        const int limit = std::min(lastCol, reader.length() - 1);
        for (int col = firstCol; col <= limit; ++col) {
            const Cell& cell = reader.at(col);
            if (cell.hasFlag(CellFlagWideTrailer)) continue;
            row += cell.ch == 0 ? U' ' : cell.ch;
        }

        /*
         * Everything past here asks the screen for another row, which
         * invalidates what `reader` holds -- so the row's text has to be
         * finished first. It is.
         */
        const bool toMargin = lastCol >= cols - 1;
        const bool wrapped = toMargin && mode != SelectionMode::Block
                          && screen.lineWrapped(line);

        /*
         * Trailing blanks on a row that really ends there are padding to the
         * window width; before a seam they are text. A block selection trims
         * every row: its right edge is a column the user drew, not where the
         * text ends, and taking a column out of a table should not come back
         * padded out to the widest row.
         */
        if ((toMargin || mode == SelectionMode::Block) && !wrapped) {
            while (!row.empty() && (row.back() == U' ' || isSpaceSeparator(row.back()))) {
                row.pop_back();
            }
        }

        out += row;
        if (line == span.end.line) break;
        if (!wrapped) out += U'\n';
    }

    return out;
}

/* ------------------------------------------------------------- Selection */

SelectionRange Selection::rangeFor(const Screen& screen, const SelectionPoint& point) const {
    switch (mode_) {
    case SelectionMode::Word: return wordAt(screen, point);
    case SelectionMode::Line: return logicalLineAt(screen, point);
    case SelectionMode::Character:
    case SelectionMode::Block:
        break;
    }
    return SelectionRange{point, point};
}

void Selection::begin(const Screen& screen, const SelectionPoint& point, SelectionMode mode) {
    mode_ = mode;
    anchor_ = rangeFor(screen, point);
    range_ = anchor_;
    active_ = true;
    dragging_ = true;
}

void Selection::extend(const Screen& screen, const SelectionPoint& point) {
    if (!dragging_) return;

    if (mode_ == SelectionMode::Character || mode_ == SelectionMode::Block) {
        /*
         * The anchor stays where the press landed and the far end follows the
         * pointer, so dragging back above the anchor selects upwards.
         */
        range_ = SelectionRange{anchor_.start, point}.normalized();
        return;
    }

    /* Word and line drags grow by whole words and whole lines. */
    range_ = anchor_.united(rangeFor(screen, point));
}

void Selection::clear() {
    active_ = false;
    dragging_ = false;
    range_ = SelectionRange{};
    anchor_ = SelectionRange{};
}

void Selection::set(const SelectionRange& range, SelectionMode mode) {
    mode_ = mode;
    anchor_ = range.normalized();
    range_ = anchor_;
    active_ = true;
    dragging_ = false;
}

bool Selection::columnsOn(int64_t line, int cols, int& firstCol, int& lastCol) const {
    if (!active_) return false;
    return rangeColumnsOn(range_, mode_, line, cols, firstCol, lastCol);
}

std::u32string Selection::text(const Screen& screen) const {
    if (!active_) return std::u32string();
    return selectionText(screen, range_, mode_);
}

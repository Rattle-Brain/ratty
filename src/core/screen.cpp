/*
 * Screen - terminal grid implementation
 */

#include "screen.h"
#include <algorithm>

namespace {
constexpr int kTabWidth = 8;

int clampInt(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}
} // namespace

Screen::Screen(int rows, int cols) {
    allocate(std::max(1, rows), std::max(1, cols));
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
}

void Screen::allocate(int rows, int cols) {
    rows_ = rows;
    cols_ = cols;
    cells_.assign(static_cast<size_t>(rows_) * static_cast<size_t>(cols_), Cell{});
    rowMap_.resize(static_cast<size_t>(rows_));
    for (int i = 0; i < rows_; ++i) {
        rowMap_[static_cast<size_t>(i)] = i;
    }
}

Cell& Screen::cellRef(int row, int col) {
    const size_t offset = static_cast<size_t>(physicalRow(row)) * static_cast<size_t>(cols_)
                        + static_cast<size_t>(col);
    return cells_[offset];
}

void Screen::markWrapped(int row) {
    if (row < 0 || row >= rows_ || cols_ <= 0) return;
    Cell& seam = cellRef(row, cols_ - 1);
    seam.flags = static_cast<uint16_t>(seam.flags | CellFlagWrapped);
}

const Cell& Screen::at(int row, int col) const {
    static const Cell blank{};
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) {
        return blank;
    }
    const size_t offset = static_cast<size_t>(physicalRow(row)) * static_cast<size_t>(cols_)
                        + static_cast<size_t>(col);
    return cells_[offset];
}

const Cell* Screen::rowData(int row) const {
    if (row < 0 || row >= rows_) return nullptr;
    return cells_.data() + static_cast<size_t>(physicalRow(row)) * static_cast<size_t>(cols_);
}

/* ------------------------------------------------------------ scrollback */

void Screen::setHistoryLimit(int lines) {
    historyLimit_ = std::max(0, lines);
    trimHistory();
    viewOffset_ = clampInt(viewOffset_, 0, historySize());
}

void Screen::clearHistory() {
    if (history_.empty() && viewOffset_ == 0) return;
    /* The lines are gone, so the numbers naming them must not come round
     * again on the lines that replace them. */
    discardedLines_ += static_cast<int64_t>(history_.size());
    history_.clear();
    invalidateDecoded();
    viewOffset_ = 0;
    touch();
}

void Screen::pushHistory(int row) {
    if (historyLimit_ <= 0 || row < 0 || row >= rows_) return;

    const Cell* src = rowData(row);

    /*
     * Reuse the buffer of the row being evicted rather than freeing it and
     * allocating a replacement of the same size. Once the scrollback is full --
     * the steady state during any sustained run of output -- every scrolled line
     * came with one malloc and one free, which together were a fifth of the cost
     * of receiving output. assign() keeps the existing capacity, so the copy
     * remains but the allocator is left out of it.
     */
    if (historyLimit_ > 0 && static_cast<int>(history_.size()) >= historyLimit_) {
        HistoryLine recycled = std::move(history_.front());
        history_.pop_front();
        ++discardedLines_;
        recycled.encode(src, cols_);
        history_.push_back(std::move(recycled));
    } else {
        history_.emplace_back();
        history_.back().encode(src, cols_);
    }
    invalidateDecoded();
    trimHistory();

    /*
     * Keep a scrolled-back view looking at the same text. The offset counts
     * rows back from the live screen, so a line entering the history moves that
     * text one row further into the past -- whether or not the oldest line was
     * evicted to make room.
     */
    if (viewOffset_ > 0) {
        viewOffset_ = std::min(viewOffset_ + 1, historySize());
    }
}

void Screen::trimHistory() {
    if (static_cast<int>(history_.size()) <= historyLimit_) return;
    while (static_cast<int>(history_.size()) > historyLimit_) {
        history_.pop_front();
        ++discardedLines_;
    }
    invalidateDecoded();
}

bool Screen::scrollViewTo(int offset) {
    const int clamped = clampInt(offset, 0, maxViewOffset());
    if (clamped == viewOffset_) return false;
    viewOffset_ = clamped;
    touch();
    return true;
}

bool Screen::scrollViewBy(int lines) {
    return scrollViewTo(viewOffset_ + lines);
}

const Cell* Screen::decodedHistory(int absolute, int& length) const {
    const HistoryLine& line = history_[static_cast<size_t>(absolute)];
    length = line.width();
    if (length == 0) return nullptr;

    if (decodedIndex_ != absolute) {
        if (static_cast<int>(decodeScratch_.size()) < length) {
            decodeScratch_.resize(static_cast<size_t>(length));
        }
        line.decode(decodeScratch_.data());
        decodedIndex_ = absolute;
    }
    return decodeScratch_.data();
}

const Cell* Screen::lineData(int64_t line, int& length) const {
    length = 0;

    /* Below the origin is text that has been dropped; the number no longer
     * names anything. */
    const int64_t absolute = line - discardedLines_;
    if (absolute < 0) return nullptr;

    const int history = historySize();
    if (absolute < history) {
        int stored = 0;
        /* An empty captured row yields length 0, so the pointer is never
         * dereferenced even though it may be null. */
        const Cell* cells = decodedHistory(static_cast<int>(absolute), stored);
        length = stored;
        return cells;
    }

    const int64_t row = absolute - history;
    if (row >= rows_) return nullptr;

    length = cols_;
    return cells_.data() + static_cast<size_t>(physicalRow(static_cast<int>(row)))
                               * static_cast<size_t>(cols_);
}

bool Screen::lineWrapped(int64_t line) const {
    int length = 0;
    const Cell* cells = lineData(line, length);
    return cells && length > 0 && cells[length - 1].hasFlag(CellFlagWrapped);
}

const Cell& Screen::viewAt(int row, int col) const {
    static const Cell blank{};
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) return blank;

    int length = 0;
    const Cell* cells = viewRow(row, length);
    /* Past the end of a captured row -- the window is wider than it was, or the
     * blank tail was never stored -- the row is blank. */
    if (!cells || col >= length) return blank;
    return cells[col];
}

const Cell* Screen::viewRow(int row, int& length) const {
    length = 0;
    if (row < 0 || row >= rows_) return nullptr;

    const Cell* cells = lineData(viewTopLine() + row, length);
    length = std::min(length, cols_);
    return cells;
}

bool Screen::scrollViewToLine(int64_t line, int preferredRow) {
    /* viewTopLine() == screenTopLine() - viewOffset_, so putting `line` at
     * `preferredRow` means a view offset of screenTopLine() - line +
     * preferredRow. Out-of-range values clamp, which lands the line as close to
     * the asked-for row as the buffer allows. */
    const int64_t offset = screenTopLine() - line + clampInt(preferredRow, 0, rows_ - 1);
    return scrollViewTo(static_cast<int>(std::clamp<int64_t>(offset, 0, maxViewOffset())));
}

void Screen::clearRow(int row, const Pen& pen) {
    clearRowRange(0, cols_ - 1, row, pen);
}

void Screen::clearRowRange(int fromCol, int toColInclusive, int row, const Pen& pen) {
    if (row < 0 || row >= rows_) return;
    const int from = std::max(0, fromCol);
    const int to = std::min(cols_ - 1, toColInclusive);
    if (from > to) return;

    /*
     * An erased cell is the same cell for the whole range -- a space carrying
     * the pen's colours -- so this is a fill, not a loop of writes. It used to
     * resolve the row indirection and bounds-check the coordinates once per
     * cell, which mattered because this is on the scrolling path: every line of
     * output that pushes the screen up clears a row through here, and a `cat`
     * does that thousands of times a second.
     */
    Cell blank;
    blank.erase(pen);

    Cell* rowBase = cells_.data() + static_cast<size_t>(physicalRow(row))
                                        * static_cast<size_t>(cols_);
    std::fill(rowBase + from, rowBase + to + 1, blank);
}

void Screen::resize(int rows, int cols, const Pen& pen) {
    rows = std::max(1, rows);
    cols = std::max(1, cols);
    if (rows == rows_ && cols == cols_) return;

    /*
     * A width change is what makes old text the wrong shape, so that is what
     * rewraps. A height change alone moves whole rows between the screen and
     * the history and needs none of the work below.
     */
    if (reflowEnabled_ && cols != cols_) {
        reflow(rows, cols, pen);
        return;
    }

    /* Snapshot the old contents in logical order, then rebuild. Content is
     * anchored to the *bottom* of the screen so that the prompt stays put when
     * the window grows, matching what every other terminal does. */
    std::vector<Cell> old(static_cast<size_t>(rows_) * static_cast<size_t>(cols_));
    for (int r = 0; r < rows_; ++r) {
        const Cell* src = rowData(r);
        std::copy(src, src + cols_, old.begin() + static_cast<size_t>(r) * static_cast<size_t>(cols_));
    }

    const int oldRows = rows_;
    const int oldCols = cols_;
    const int oldCursorRow = cursorRow_;

    /* How many rows to drop from the top when shrinking vertically. */
    const int rowShift = (oldRows > rows) ? std::min(oldRows - rows, std::max(0, oldCursorRow - rows + 1))
                                         : 0;

    /* Those rows scrolled off the top as far as the user is concerned, so they
     * belong in the history rather than in the bin. */
    for (int r = 0; r < rowShift; ++r) {
        pushHistory(r);
    }

    allocate(rows, cols);

    const int copyCols = std::min(oldCols, cols);
    for (int r = 0; r < rows; ++r) {
        const int srcRow = r + rowShift;
        if (srcRow < 0 || srcRow >= oldRows) {
            clearRow(r, pen);
            continue;
        }
        const Cell* src = old.data() + static_cast<size_t>(srcRow) * static_cast<size_t>(oldCols);
        for (int c = 0; c < copyCols; ++c) {
            cellRef(r, c) = src[c];
        }
        clearRowRange(copyCols, cols - 1, r, pen);
    }

    cursorRow_ = clampInt(oldCursorRow - rowShift, 0, rows_ - 1);
    cursorCol_ = clampInt(cursorCol_, 0, cols_ - 1);
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    viewOffset_ = clampInt(viewOffset_, 0, historySize());
    resetScrollRegion();
    touch();
}

void Screen::reflow(int newRows, int newCols, const Pen& pen) {
    /*
     * Rebuild the buffer at a new width.
     *
     * The buffer is taken apart into *logical* lines -- rows joined at the seams
     * CellFlagWrapped marks -- and each is laid out again at the new width. A
     * row that ended in a newline stays a line of its own, which is the whole
     * point of tracking the seam: rewrapping from the cells alone would run a
     * table drawn by a TUI together into a paragraph.
     *
     * Done as a stream rather than by materialising the buffer twice: rows come
     * out of the history in order, each finished logical line is rewrapped and
     * re-encoded straight away, and only the last screenful is expanded back
     * into cells at the end. A 10 000-line history therefore costs one logical
     * line of scratch space instead of a second copy of the scrollback.
     */
    const int history = historySize();
    const int oldRows = rows_;
    const int oldCols = cols_;

    std::deque<HistoryLine> rebuilt;
    std::vector<Cell> logical;   // the line being accumulated
    std::vector<Cell> emitted;   // one rewrapped row

    /*
     * The cursor is tracked by its offset within its logical line, because that
     * is the only thing about it a rewrap leaves alone: which row and column it
     * ends up on is exactly what changes.
     */
    int cursorOffsetInLine = -1;
    int cursorNewRow = 0;
    int cursorNewCol = 0;
    bool cursorPlaced = false;

    /* Cut the accumulated logical line into rows of the new width. */
    auto flushLogical = [&]() {
        const size_t size = logical.size();
        size_t index = 0;
        bool firstRow = true;

        while (firstRow || index < size) {
            firstRow = false;
            emitted.clear();
            const size_t from = index;

            while (index < size && static_cast<int>(emitted.size()) < newCols) {
                const bool wide = (index + 1 < size)
                               && logical[index + 1].hasFlag(CellFlagWideTrailer);
                if (wide && newCols >= 2) {
                    if (static_cast<int>(emitted.size()) == newCols - 1) {
                        /* A double-width glyph may not straddle the margin, so
                         * the column it will not fit in is left blank -- which
                         * is what print() does when it meets the same case. */
                        Cell blank;
                        blank.erase(pen);
                        emitted.push_back(blank);
                        break;
                    }
                    emitted.push_back(logical[index]);
                    emitted.push_back(logical[index + 1]);
                    index += 2;
                    continue;
                }
                emitted.push_back(logical[index]);
                /* A one-column window has no room for a pair, so the trailer is
                 * dropped rather than drawn on its own. */
                index += wide ? 2 : 1;
            }

            /* More to come means this row ends in a seam, and a row that ends in
             * a seam is full -- so its last cell is the last column. */
            const bool continues = index < size;
            if (continues && !emitted.empty()) {
                Cell& seam = emitted.back();
                seam.flags = static_cast<uint16_t>(seam.flags | CellFlagWrapped);
            }

            if (!cursorPlaced && cursorOffsetInLine >= 0) {
                const size_t offset = static_cast<size_t>(cursorOffsetInLine);
                /* Either the offset falls in this row, or the line has run out
                 * and the cursor sat past its text -- a cursor parked beyond the
                 * last character of its line, which is where a prompt leaves it. */
                if ((offset >= from && offset < index) || !continues) {
                    cursorNewRow = static_cast<int>(rebuilt.size());
                    cursorNewCol = static_cast<int>(offset - from);
                    cursorPlaced = true;
                }
            }

            rebuilt.emplace_back();
            rebuilt.back().encode(emitted.data(), static_cast<int>(emitted.size()));
        }

        logical.clear();
        cursorOffsetInLine = -1;
    };

    for (int index = 0; index < history + oldRows; ++index) {
        const Cell* cells = nullptr;
        int length = 0;
        bool wrapped = false;

        if (index < history) {
            cells = decodedHistory(index, length);
            wrapped = length > 0 && cells[length - 1].hasFlag(CellFlagWrapped);
        } else {
            const int row = index - history;
            cells = rowData(row);
            length = oldCols;
            wrapped = oldCols > 0 && cells[oldCols - 1].hasFlag(CellFlagWrapped);
            /*
             * A row that ends its line contributes no trailing blanks: they are
             * padding at the old width rather than text, and carrying them over
             * would leave a screenful of spaces in the middle of the rewrapped
             * buffer. A row that ends in a seam keeps every column, blanks
             * included -- the text really does continue after them.
             */
            if (!wrapped) {
                while (length > 0 && cells[length - 1].isDefaultBlank()) --length;
            }
            if (row == cursorRow_) {
                cursorOffsetInLine = static_cast<int>(logical.size()) + cursorCol_;
            }
        }

        if (cells && length > 0) {
            const size_t begin = logical.size();
            logical.insert(logical.end(), cells, cells + length);
            /* The seam belongs to the old layout; the new one sets its own. */
            for (size_t i = begin; i < logical.size(); ++i) {
                logical[i].flags = static_cast<uint16_t>(logical[i].flags & ~CellFlagWrapped);
            }
        }

        if (!wrapped) flushLogical();
    }
    /* A buffer whose very last row was left mid-wrap still owes a line. */
    if (!logical.empty() || cursorOffsetInLine >= 0) flushLogical();

    const int total = static_cast<int>(rebuilt.size());
    if (!cursorPlaced) {
        cursorNewRow = std::max(0, total - 1);
        cursorNewCol = 0;
    }

    /*
     * The live screen is the last newRows rows, so a prompt at the bottom stays
     * at the bottom -- unless rewrapping pushed the cursor above that window, in
     * which case the window follows the cursor instead of scrolling it out of
     * sight.
     */
    int screenStart = std::max(0, total - newRows);
    screenStart = std::min(screenStart, cursorNewRow);

    allocate(newRows, newCols);

    for (int row = 0; row < newRows; ++row) {
        clearRow(row, pen);
        const int index = screenStart + row;
        if (index >= total) continue;
        Cell* target = cells_.data() + static_cast<size_t>(physicalRow(row))
                                           * static_cast<size_t>(cols_);
        rebuilt[static_cast<size_t>(index)].decode(target);
    }

    rebuilt.erase(rebuilt.begin() + screenStart, rebuilt.end());
    history_ = std::move(rebuilt);

    /*
     * Renumber past the end of the old buffer. Every line number handed out
     * before this point named text at the old width, and that text has just been
     * re-cut into different rows; advancing the origin means such a number
     * resolves to nothing rather than to a line it never meant.
     */
    discardedLines_ += static_cast<int64_t>(history) + oldRows;
    invalidateDecoded();
    trimHistory();

    cursorRow_ = clampInt(cursorNewRow - screenStart, 0, rows_ - 1);
    cursorCol_ = clampInt(cursorNewCol, 0, cols_ - 1);
    pendingWrap_ = false;
    lastPrintRow_ = -1;
    lastPrintCol_ = -1;
    /*
     * The rows have been re-cut, so there is no exact answer for a view that was
     * scrolled back; clamping keeps it scrolled back by about as much rather
     * than snapping it to the live screen under the user.
     */
    viewOffset_ = clampInt(viewOffset_, 0, historySize());
    resetScrollRegion();
    touch();
}

void Screen::reset(const Pen& pen) {
    for (int r = 0; r < rows_; ++r) {
        clearRow(r, pen);
    }
    /* RIS discards the scrollback, as it does on a real terminal. */
    discardedLines_ += static_cast<int64_t>(history_.size()) + rows_;
    history_.clear();
    invalidateDecoded();
    viewOffset_ = 0;
    cursorRow_ = 0;
    cursorCol_ = 0;
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    cursorVisible_ = true;
    autoWrap_ = true;
    saved_ = SavedCursor{};
    resetScrollRegion();
    touch();
}

/* ---------------------------------------------------------------- cursor */

void Screen::moveTo(int row, int col) {
    cursorRow_ = clampInt(row, 0, rows_ - 1);
    cursorCol_ = clampInt(col, 0, cols_ - 1);
    pendingWrap_ = false;
    /* A grapheme cluster cannot span a cursor movement. */
    lastPrintCol_ = -1;
}

void Screen::moveBy(int deltaRow, int deltaCol) {
    moveTo(cursorRow_ + deltaRow, cursorCol_ + deltaCol);
}

void Screen::moveToRow(int row) {
    moveTo(row, cursorCol_);
}

void Screen::moveToColumn(int col) {
    moveTo(cursorRow_, col);
}

void Screen::saveCursor() {
    saved_ = SavedCursor{cursorRow_, cursorCol_, true};
}

void Screen::restoreCursor() {
    if (saved_.valid) {
        moveTo(saved_.row, saved_.col);
    } else {
        moveTo(0, 0);
    }
}

/* ------------------------------------------------------------------ text */

void Screen::print(char32_t ch, const Pen& pen, int charWidth, uint16_t extraFlags) {
    if (charWidth <= 0) {
        /* Zero-width marks are handled as cluster continuations by the caller;
         * anything still reporting zero here must not consume a column. */
        return;
    }

    if (pendingWrap_) {
        pendingWrap_ = false;
        /* Record the seam before the line feed, which may push this very row
         * into the history. */
        markWrapped(cursorRow_);
        cursorCol_ = 0;
        lineFeed(pen);
    }

    /* A double-width glyph may not straddle the right margin. */
    if (charWidth == 2 && cursorCol_ == cols_ - 1) {
        cellRef(cursorRow_, cursorCol_).erase(pen);
        markWrapped(cursorRow_);
        cursorCol_ = 0;
        lineFeed(pen);
    }

    const uint16_t flags = static_cast<uint16_t>(pen.flags | extraFlags);

    Cell& cell = cellRef(cursorRow_, cursorCol_);
    cell.ch = ch;
    cell.fg = pen.fg;
    cell.bg = pen.bg;
    cell.flags = flags;

    lastPrintRow_ = cursorRow_;
    lastPrintCol_ = cursorCol_;
    lastPrintWidth_ = charWidth;

    if (charWidth == 2 && cursorCol_ + 1 < cols_) {
        Cell& trailer = cellRef(cursorRow_, cursorCol_ + 1);
        trailer.ch = U' ';
        trailer.fg = pen.fg;
        trailer.bg = pen.bg;
        trailer.flags = static_cast<uint16_t>(flags | CellFlagWideTrailer);
    }

    const int nextCol = cursorCol_ + charWidth;
    if (nextCol >= cols_) {
        /* Park in the last column and defer the wrap (see header). */
        cursorCol_ = cols_ - 1;
        pendingWrap_ = autoWrap_;
    } else {
        cursorCol_ = nextCol;
    }
    touch();
}

char32_t Screen::lastPrintedChar() const {
    if (lastPrintCol_ < 0) return 0;
    return at(lastPrintRow_, lastPrintCol_).ch;
}

bool Screen::adjustLastCell(int charWidth, uint16_t setFlags, uint16_t clearFlags,
                            const Pen& pen) {
    if (lastPrintCol_ < 0 || lastPrintRow_ < 0) return false;
    if (lastPrintRow_ >= rows_ || lastPrintCol_ >= cols_) return false;

    const int row = lastPrintRow_;
    const int col = lastPrintCol_;
    const int oldWidth = lastPrintWidth_;
    const int newWidth = (charWidth == 2) ? 2 : 1;

    /* Widening past the right margin is not possible; keep the narrow form
     * rather than wrapping a character that has already been placed. */
    if (newWidth == 2 && col + 1 >= cols_) return false;

    Cell& cell = cellRef(row, col);
    cell.flags = static_cast<uint16_t>((cell.flags | setFlags) & ~clearFlags);

    if (newWidth == oldWidth) {
        touch();
        return true;
    }

    if (newWidth == 2) {
        /* Claim the following column as the trailing half and push the cursor
         * one further, since the cell now covers two columns. */
        Cell& trailer = cellRef(row, col + 1);
        trailer.ch = U' ';
        trailer.fg = cell.fg;
        trailer.bg = cell.bg;
        trailer.flags = static_cast<uint16_t>(cell.flags | CellFlagWideTrailer);

        if (!pendingWrap_ && cursorRow_ == row && cursorCol_ == col + 1) {
            if (col + 2 >= cols_) {
                cursorCol_ = cols_ - 1;
                pendingWrap_ = autoWrap_;
            } else {
                cursorCol_ = col + 2;
            }
        }
    } else {
        /* Release the trailing half and pull the cursor back onto it. */
        if (col + 1 < cols_) {
            Cell& trailer = cellRef(row, col + 1);
            if (trailer.hasFlag(CellFlagWideTrailer)) {
                trailer.erase(pen);
            }
        }
        if (cursorRow_ == row && (cursorCol_ == col + 2 || pendingWrap_)) {
            cursorCol_ = col + 1;
            pendingWrap_ = false;
        }
    }

    lastPrintWidth_ = newWidth;
    touch();
    return true;
}

void Screen::carriageReturn() {
    cursorCol_ = 0;
    pendingWrap_ = false;
    lastPrintCol_ = -1;
}

void Screen::lineFeed(const Pen& pen) {
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    if (cursorRow_ == scrollBottom_) {
        scrollUp(1, pen);
    } else if (cursorRow_ < rows_ - 1) {
        ++cursorRow_;
    }
}

void Screen::reverseIndex(const Pen& pen) {
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    if (cursorRow_ == scrollTop_) {
        scrollDown(1, pen);
    } else if (cursorRow_ > 0) {
        --cursorRow_;
    }
}

void Screen::backspace() {
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    if (cursorCol_ > 0) {
        --cursorCol_;
    }
}

void Screen::tab(int count) {
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    for (int i = 0; i < std::max(1, count); ++i) {
        const int next = ((cursorCol_ / kTabWidth) + 1) * kTabWidth;
        cursorCol_ = std::min(next, cols_ - 1);
        if (cursorCol_ == cols_ - 1) break;
    }
}

void Screen::backTab(int count) {
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    for (int i = 0; i < std::max(1, count); ++i) {
        if (cursorCol_ == 0) break;
        cursorCol_ = ((cursorCol_ - 1) / kTabWidth) * kTabWidth;
    }
}

/* --------------------------------------------------------------- erasing */

void Screen::eraseInDisplay(int mode, const Pen& pen) {
    switch (mode) {
    case 0:  // cursor to end of screen
        clearRowRange(cursorCol_, cols_ - 1, cursorRow_, pen);
        for (int r = cursorRow_ + 1; r < rows_; ++r) clearRow(r, pen);
        break;
    case 1:  // start of screen to cursor
        for (int r = 0; r < cursorRow_; ++r) clearRow(r, pen);
        clearRowRange(0, cursorCol_, cursorRow_, pen);
        break;
    case 2:  // whole screen
        for (int r = 0; r < rows_; ++r) clearRow(r, pen);
        break;
    case 3:
        /*
         * "Erase saved lines", and only those -- the display is left alone.
         * That is xterm's definition, and applications that want both send ED 2
         * first: `tput clear` is "CSI H CSI 2 J CSI 3 J".
         */
        clearHistory();
        break;
    default:
        return;
    }
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    touch();
}

void Screen::eraseInLine(int mode, const Pen& pen) {
    switch (mode) {
    case 0: clearRowRange(cursorCol_, cols_ - 1, cursorRow_, pen); break;
    case 1: clearRowRange(0, cursorCol_, cursorRow_, pen); break;
    case 2: clearRow(cursorRow_, pen); break;
    default: return;
    }
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    touch();
}

void Screen::eraseChars(int count, const Pen& pen) {
    const int n = std::max(1, count);
    clearRowRange(cursorCol_, cursorCol_ + n - 1, cursorRow_, pen);
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    touch();
}

/* --------------------------------------------------------------- editing */

void Screen::insertChars(int count, const Pen& pen) {
    const int n = std::min(std::max(1, count), cols_ - cursorCol_);
    if (n <= 0) return;

    for (int col = cols_ - 1; col >= cursorCol_ + n; --col) {
        cellRef(cursorRow_, col) = cellRef(cursorRow_, col - n);
    }
    clearRowRange(cursorCol_, cursorCol_ + n - 1, cursorRow_, pen);
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    touch();
}

void Screen::deleteChars(int count, const Pen& pen) {
    const int n = std::min(std::max(1, count), cols_ - cursorCol_);
    if (n <= 0) return;

    for (int col = cursorCol_; col + n < cols_; ++col) {
        cellRef(cursorRow_, col) = cellRef(cursorRow_, col + n);
    }
    clearRowRange(cols_ - n, cols_ - 1, cursorRow_, pen);
    pendingWrap_ = false;
    lastPrintCol_ = -1;
    touch();
}

void Screen::insertLines(int count, const Pen& pen) {
    if (cursorRow_ < scrollTop_ || cursorRow_ > scrollBottom_) return;

    const int n = std::min(std::max(1, count), scrollBottom_ - cursorRow_ + 1);
    /* Rotate row indices inside [cursorRow_, scrollBottom_] so the last n rows
     * move to the top of the range, then blank them. */
    auto first = rowMap_.begin() + cursorRow_;
    auto last = rowMap_.begin() + scrollBottom_ + 1;
    std::rotate(first, last - n, last);
    for (int r = cursorRow_; r < cursorRow_ + n; ++r) clearRow(r, pen);

    pendingWrap_ = false;
    lastPrintCol_ = -1;
    touch();
}

void Screen::deleteLines(int count, const Pen& pen) {
    if (cursorRow_ < scrollTop_ || cursorRow_ > scrollBottom_) return;

    const int n = std::min(std::max(1, count), scrollBottom_ - cursorRow_ + 1);
    auto first = rowMap_.begin() + cursorRow_;
    auto last = rowMap_.begin() + scrollBottom_ + 1;
    std::rotate(first, first + n, last);
    for (int r = scrollBottom_ - n + 1; r <= scrollBottom_; ++r) clearRow(r, pen);

    pendingWrap_ = false;
    lastPrintCol_ = -1;
    touch();
}

/* ------------------------------------------------------------- scrolling */

void Screen::scrollUp(int count, const Pen& pen) {
    const int regionHeight = scrollBottom_ - scrollTop_ + 1;
    const int n = std::min(std::max(1, count), regionHeight);

    /*
     * Only rows leaving the top of the *whole screen* are history. A DECSTBM
     * region is a subwindow the application scrolls itself -- htop's process
     * list, vim's text area -- and keeping those rows would fill the scrollback
     * with the same screen redrawn over and over.
     */
    if (historyLimit_ > 0 && scrollTop_ == 0 && scrollBottom_ == rows_ - 1) {
        for (int i = 0; i < n; ++i) {
            pushHistory(scrollTop_ + i);
        }
    }

    auto first = rowMap_.begin() + scrollTop_;
    auto last = rowMap_.begin() + scrollBottom_ + 1;
    std::rotate(first, first + n, last);

    for (int r = scrollBottom_ - n + 1; r <= scrollBottom_; ++r) clearRow(r, pen);
    touch();
}

void Screen::scrollDown(int count, const Pen& pen) {
    const int regionHeight = scrollBottom_ - scrollTop_ + 1;
    const int n = std::min(std::max(1, count), regionHeight);

    auto first = rowMap_.begin() + scrollTop_;
    auto last = rowMap_.begin() + scrollBottom_ + 1;
    std::rotate(first, last - n, last);

    for (int r = scrollTop_; r < scrollTop_ + n; ++r) clearRow(r, pen);
    touch();
}

void Screen::setScrollRegion(int top, int bottom) {
    top = clampInt(top, 0, rows_ - 1);
    bottom = clampInt(bottom, 0, rows_ - 1);
    if (top >= bottom) {
        resetScrollRegion();
    } else {
        scrollTop_ = top;
        scrollBottom_ = bottom;
    }
    /* DECSTBM homes the cursor. */
    moveTo(scrollTop_, 0);
}

void Screen::resetScrollRegion() {
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
}

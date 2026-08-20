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

void Screen::clearRow(int row, const Pen& pen) {
    clearRowRange(0, cols_ - 1, row, pen);
}

void Screen::clearRowRange(int fromCol, int toColInclusive, int row, const Pen& pen) {
    if (row < 0 || row >= rows_) return;
    const int from = std::max(0, fromCol);
    const int to = std::min(cols_ - 1, toColInclusive);
    for (int col = from; col <= to; ++col) {
        cellRef(row, col).erase(pen);
    }
}

void Screen::resize(int rows, int cols, const Pen& pen) {
    rows = std::max(1, rows);
    cols = std::max(1, cols);
    if (rows == rows_ && cols == cols_) return;

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
    resetScrollRegion();
    touch();
}

void Screen::reset(const Pen& pen) {
    for (int r = 0; r < rows_; ++r) {
        clearRow(r, pen);
    }
    cursorRow_ = 0;
    cursorCol_ = 0;
    pendingWrap_ = false;
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

void Screen::print(char32_t ch, const Pen& pen, int charWidth) {
    if (charWidth <= 0) {
        /* Zero-width (combining) marks are not represented yet; dropping them
         * is preferable to letting them consume a column. */
        return;
    }

    if (pendingWrap_) {
        pendingWrap_ = false;
        cursorCol_ = 0;
        lineFeed(pen);
    }

    /* A double-width glyph may not straddle the right margin. */
    if (charWidth == 2 && cursorCol_ == cols_ - 1) {
        cellRef(cursorRow_, cursorCol_).erase(pen);
        cursorCol_ = 0;
        lineFeed(pen);
    }

    Cell& cell = cellRef(cursorRow_, cursorCol_);
    cell.ch = ch;
    cell.fg = pen.fg;
    cell.bg = pen.bg;
    cell.flags = pen.flags;

    if (charWidth == 2 && cursorCol_ + 1 < cols_) {
        Cell& trailer = cellRef(cursorRow_, cursorCol_ + 1);
        trailer.ch = U' ';
        trailer.fg = pen.fg;
        trailer.bg = pen.bg;
        trailer.flags = static_cast<uint16_t>(pen.flags | CellFlagWideTrailer);
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

void Screen::carriageReturn() {
    cursorCol_ = 0;
    pendingWrap_ = false;
}

void Screen::lineFeed(const Pen& pen) {
    pendingWrap_ = false;
    if (cursorRow_ == scrollBottom_) {
        scrollUp(1, pen);
    } else if (cursorRow_ < rows_ - 1) {
        ++cursorRow_;
    }
}

void Screen::reverseIndex(const Pen& pen) {
    pendingWrap_ = false;
    if (cursorRow_ == scrollTop_) {
        scrollDown(1, pen);
    } else if (cursorRow_ > 0) {
        --cursorRow_;
    }
}

void Screen::backspace() {
    pendingWrap_ = false;
    if (cursorCol_ > 0) {
        --cursorCol_;
    }
}

void Screen::tab(int count) {
    pendingWrap_ = false;
    for (int i = 0; i < std::max(1, count); ++i) {
        const int next = ((cursorCol_ / kTabWidth) + 1) * kTabWidth;
        cursorCol_ = std::min(next, cols_ - 1);
        if (cursorCol_ == cols_ - 1) break;
    }
}

void Screen::backTab(int count) {
    pendingWrap_ = false;
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
    case 3:  // whole screen + scrollback (no scrollback yet)
        for (int r = 0; r < rows_; ++r) clearRow(r, pen);
        break;
    default:
        return;
    }
    pendingWrap_ = false;
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
    touch();
}

void Screen::eraseChars(int count, const Pen& pen) {
    const int n = std::max(1, count);
    clearRowRange(cursorCol_, cursorCol_ + n - 1, cursorRow_, pen);
    pendingWrap_ = false;
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
    touch();
}

/* ------------------------------------------------------------- scrolling */

void Screen::scrollUp(int count, const Pen& pen) {
    const int regionHeight = scrollBottom_ - scrollTop_ + 1;
    const int n = std::min(std::max(1, count), regionHeight);

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

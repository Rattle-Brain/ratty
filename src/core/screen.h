/*
 * Screen - the terminal character grid and cursor
 *
 * This is pure terminal *state*: no parsing, no Qt widgets, no rendering. It
 * implements the VT geometry rules that a terminal has to get right, most
 * importantly:
 *
 *   - Deferred (pending) wrap. When a character lands in the last column the
 *     cursor stays put and only a *flag* is set; the line break happens when
 *     the next printable character arrives. Shells rely on this: zsh's
 *     end-of-line marker prints an inverse '%', pads to the right margin, then
 *     does "\r \r" to erase it. With eager wrapping the erase lands one row too
 *     low, the marker survives, and you get a stray block above every prompt.
 *
 *   - A scrolling region (DECSTBM), so full-screen apps can scroll a subrange.
 *
 * Rows are held in a flat cell buffer addressed through an indirection table,
 * so scrolling rotates row indices instead of copying cell data.
 */

#ifndef CORE_SCREEN_H
#define CORE_SCREEN_H

#include "cell.h"
#include <vector>

class Screen {
public:
    Screen(int rows, int cols);

    /* Geometry */
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    void resize(int rows, int cols, const Pen& pen);

    /* Read-only grid access. Out-of-range coordinates yield a blank cell. */
    const Cell& at(int row, int col) const;
    const Cell* rowData(int row) const;

    /* Cursor */
    int cursorRow() const { return cursorRow_; }
    int cursorCol() const { return cursorCol_; }
    bool cursorVisible() const { return cursorVisible_; }
    void setCursorVisible(bool visible) { cursorVisible_ = visible; }

    /* Autowrap mode (DECAWM). When off, characters pile up in the last column. */
    void setAutoWrap(bool on) { autoWrap_ = on; }
    bool autoWrap() const { return autoWrap_; }

    /* Cursor movement. All of these cancel a pending wrap. */
    void moveTo(int row, int col);
    void moveBy(int deltaRow, int deltaCol);
    void moveToRow(int row);
    void moveToColumn(int col);
    void saveCursor();
    void restoreCursor();

    /* Text output */
    void print(char32_t ch, const Pen& pen, int charWidth);
    void carriageReturn();
    void lineFeed(const Pen& pen);
    void reverseIndex(const Pen& pen);
    void backspace();
    void tab(int count = 1);
    void backTab(int count = 1);

    /* Erasing. `mode` follows the ED/EL parameter conventions. */
    void eraseInDisplay(int mode, const Pen& pen);
    void eraseInLine(int mode, const Pen& pen);
    void eraseChars(int count, const Pen& pen);

    /* Editing */
    void insertChars(int count, const Pen& pen);
    void deleteChars(int count, const Pen& pen);
    void insertLines(int count, const Pen& pen);
    void deleteLines(int count, const Pen& pen);

    /* Scrolling */
    void scrollUp(int count, const Pen& pen);
    void scrollDown(int count, const Pen& pen);
    void setScrollRegion(int top, int bottom);
    void resetScrollRegion();
    int scrollTop() const { return scrollTop_; }
    int scrollBottom() const { return scrollBottom_; }

    /* Wholesale reset (RIS / ED 2 style) */
    void reset(const Pen& pen);

    /* Every mutation bumps this, so the view can skip repainting an
     * unchanged grid. */
    uint64_t revision() const { return revision_; }

private:
    Cell& cellRef(int row, int col);
    void clearRow(int row, const Pen& pen);
    void clearRowRange(int fromCol, int toColInclusive, int row, const Pen& pen);
    void allocate(int rows, int cols);
    void touch() { ++revision_; }

    /* Physical row index for a logical row. */
    int physicalRow(int row) const { return rowMap_[static_cast<size_t>(row)]; }

    std::vector<Cell> cells_;
    std::vector<int> rowMap_;

    int rows_;
    int cols_;

    int cursorRow_ = 0;
    int cursorCol_ = 0;
    bool cursorVisible_ = true;

    /* Deferred-wrap flag: the cursor is parked in the last column and the next
     * printable character must wrap first. */
    bool pendingWrap_ = false;
    bool autoWrap_ = true;

    int scrollTop_ = 0;
    int scrollBottom_ = 0;   // inclusive

    struct SavedCursor {
        int row = 0;
        int col = 0;
        bool valid = false;
    } saved_;

    uint64_t revision_ = 1;
};

#endif /* CORE_SCREEN_H */

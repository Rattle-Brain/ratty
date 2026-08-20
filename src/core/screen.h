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
 *   - Scrollback. A row that leaves the top of the screen is pushed into a
 *     history deque instead of being dropped, and a *view offset* lets the
 *     renderer show `history + viewport` rather than the live grid. Lines
 *     leaving the top of a smaller scrolling region are not kept: a region is a
 *     subwindow the application manages itself, and capturing it would fill the
 *     history with the redrawn middle of a TUI.
 *
 *     History rows are stored at the width they had when they were captured and
 *     are deliberately *not* reflowed on resize -- see doc/known-gaps.md. A
 *     narrower window therefore shows old lines truncated rather than rewrapped,
 *     which is what xterm does and what keeps `viewAt()` O(1).
 *
 * Rows are held in a flat cell buffer addressed through an indirection table,
 * so scrolling rotates row indices instead of copying cell data.
 */

#ifndef CORE_SCREEN_H
#define CORE_SCREEN_H

#include "cell.h"
#include <deque>
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
    void print(char32_t ch, const Pen& pen, int charWidth, uint16_t extraFlags = 0);

    /*
     * Retrofit the cell most recently printed. A grapheme cluster only reveals
     * itself one code point at a time -- a U+FE0F arriving after its base
     * character both recolours it and widens it from one column to two -- so the
     * cell has to be adjustable after the fact.
     *
     * Returns false when there is no adjustable cell (the cursor has moved since,
     * or nothing has been printed on this line yet).
     */
    bool adjustLastCell(int charWidth, uint16_t setFlags, uint16_t clearFlags,
                        const Pen& pen);
    /* True while the most recent print is still the cell under adjustment. */
    bool hasAdjustableCell() const { return lastPrintCol_ >= 0; }
    char32_t lastPrintedChar() const;
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

    /* ----------------------------------------------------------- scrollback */

    /*
     * How many rows of history to keep. Zero disables it entirely, which is
     * what the alternate screen wants: `less` and `vim` redraw their whole
     * window, so every scroll would otherwise deposit a screenful of noise.
     * Lowering the limit trims what is already there.
     */
    void setHistoryLimit(int lines);
    int historyLimit() const { return historyLimit_; }
    int historySize() const { return static_cast<int>(history_.size()); }
    void clearHistory();

    /*
     * View offset: how many rows the display is scrolled back, 0 being the live
     * screen and historySize() the oldest line still kept. The grid accessors
     * above are unaffected -- an application writes to the live screen whether
     * or not the user is looking at history.
     */
    int viewOffset() const { return viewOffset_; }
    int maxViewOffset() const { return historySize(); }
    bool scrolledBack() const { return viewOffset_ > 0; }

    /* All three return true when the view actually moved. `lines` is positive
     * towards the past. */
    bool scrollViewBy(int lines);
    bool scrollViewTo(int offset);
    bool scrollViewToBottom() { return scrollViewTo(0); }
    bool scrollViewToTop() { return scrollViewTo(maxViewOffset()); }

    /* The grid as displayed: history rows above, live rows below. With no
     * offset this is exactly at(). */
    const Cell& viewAt(int row, int col) const;

    /* Every mutation bumps this, so the view can skip repainting an
     * unchanged grid. */
    uint64_t revision() const { return revision_; }

private:
    Cell& cellRef(int row, int col);
    /* Copy a logical row into the history, evicting the oldest if needed. */
    void pushHistory(int row);
    void trimHistory();
    void clearRow(int row, const Pen& pen);
    void clearRowRange(int fromCol, int toColInclusive, int row, const Pen& pen);
    void allocate(int rows, int cols);
    void touch() { ++revision_; }

    /* Physical row index for a logical row. */
    int physicalRow(int row) const { return rowMap_[static_cast<size_t>(row)]; }

    std::vector<Cell> cells_;
    std::vector<int> rowMap_;

    /*
     * Scrollback. A deque of whole rows rather than a second flat buffer: rows
     * are appended and evicted one at a time and never addressed as a
     * rectangle, and the width they were captured at has to travel with them.
     */
    std::deque<std::vector<Cell>> history_;
    int historyLimit_ = 0;
    int viewOffset_ = 0;

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

    /*
     * Where the most recently printed cell is, for adjustLastCell(). Reset by
     * anything that moves the cursor, because a cluster cannot span a cursor
     * movement.
     */
    int lastPrintRow_ = -1;
    int lastPrintCol_ = -1;
    int lastPrintWidth_ = 0;

    struct SavedCursor {
        int row = 0;
        int col = 0;
        bool valid = false;
    } saved_;

    uint64_t revision_ = 1;
};

#endif /* CORE_SCREEN_H */

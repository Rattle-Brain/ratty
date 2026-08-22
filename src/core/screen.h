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
 *   - Reflow. A row that ran into the right margin is marked as wrapped (see
 *     CellFlagWrapped), so a width change can take the buffer apart into
 *     *logical* lines and lay them out again at the new width. Only a real seam
 *     is joined: a row that ended in a newline stays its own line, which is what
 *     keeps a table drawn by a TUI from being run together into a paragraph.
 *
 *   - Stable line numbers. An absolute row index shifts every time the oldest
 *     history line is evicted, so it cannot name a piece of text for longer than
 *     the buffer stays quiet. Stable line numbers count from the first line ever
 *     captured instead, which is what lets a selection anchor and a search
 *     result survive a screenful of output.
 *
 * Rows are held in a flat cell buffer addressed through an indirection table,
 * so scrolling rotates row indices instead of copying cell data.
 */

#ifndef CORE_SCREEN_H
#define CORE_SCREEN_H

#include "cell.h"
#include "history.h"
#include <cstdint>
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

    /*
     * The grid as displayed: history rows above, live rows below. With no
     * offset this is exactly at().
     *
     * A history row is stored compressed, so the reference may point into an
     * internal decode buffer that the next call to viewAt() or viewRow() is
     * free to overwrite. Read it before calling either again -- which is what
     * every caller already does, and what the reference-returning signature
     * always implied.
     */
    const Cell& viewAt(int row, int col) const;

    /*
     * The same row as viewAt(), handed over whole.
     *
     * For a caller that walks a row left to right -- which is what the renderer
     * does, every frame, over every cell -- resolving the history/live split and
     * the row indirection once per row rather than once per cell is the
     * difference between two function calls plus four bounds checks per cell and
     * a pointer increment.
     *
     * `length` is how many cells the returned pointer actually covers. A history
     * row captured at a narrower width is shorter than the screen; the columns
     * past it are blank, exactly as viewAt() reports them. nullptr for a row
     * outside the grid.
     *
     * As with viewAt(), a history row is decompressed into an internal buffer,
     * so the pointer is valid only until the next viewAt()/viewRow() call. The
     * renderer walks one row to completion before asking for the next, which is
     * the access pattern this is built for.
     */
    const Cell* viewRow(int row, int& length) const;

    /* ------------------------------------------------- stable line numbers */

    /*
     * A line number names a row of the history+screen buffer in a way that
     * survives eviction: it counts from the first line this screen ever
     * captured, not from the oldest one still kept. Two rows never share a
     * number, and a number never comes to mean different text -- it stops
     * resolving instead, once the text it named has been dropped.
     *
     * That is the coordinate a selection anchor and a search result are held
     * in. In view coordinates a selection would slide up the screen with every
     * line of output; in absolute ones it would slide as soon as the history
     * filled up and started evicting.
     */
    int64_t firstLine() const { return discardedLines_; }
    /* The line at the top of the live screen, and at the top of the view. */
    int64_t screenTopLine() const { return discardedLines_ + historySize(); }
    int64_t viewTopLine() const { return screenTopLine() - viewOffset_; }
    /* The last line there is: the bottom row of the live screen. */
    int64_t lastLine() const { return screenTopLine() + rows_ - 1; }

    /*
     * The row holding `line`, or nullptr when that line has been dropped or is
     * past the bottom of the live screen. `length` is how many cells the
     * pointer covers; columns past it are blank, as viewAt() reports them.
     *
     * Same buffer caveat as viewRow(), which is now a thin wrapper around this:
     * a history row is decompressed into an internal buffer, so the pointer is
     * valid only until the next call.
     */
    const Cell* lineData(int64_t line, int& length) const;

    /*
     * True when `line` ran into the right margin and continues on the next one,
     * rather than having been ended by a newline. What tells a soft wrap from a
     * hard line break apart, for reflow, for search, and for copying a wrapped
     * command line back out as one line.
     */
    bool lineWrapped(int64_t line) const;

    /*
     * Scroll the view so `line` is on screen, at `preferredRow` when the buffer
     * reaches that far. True when the view moved.
     */
    bool scrollViewToLine(int64_t line, int preferredRow);

    /*
     * Whether a width change rewraps the buffer. On for the primary screen; off
     * for the alternate one, whose content is a full-screen application's own
     * layout -- rewrapping htop's process table would be nonsense, and it keeps
     * no history to rewrap anyway.
     */
    void setReflowEnabled(bool enabled) { reflowEnabled_ = enabled; }
    bool reflowEnabled() const { return reflowEnabled_; }

    /* Every mutation bumps this, so the view can skip repainting an
     * unchanged grid. */
    uint64_t revision() const { return revision_; }

private:
    Cell& cellRef(int row, int col);
    /* Mark `row` as continuing on the next one; see CellFlagWrapped. */
    void markWrapped(int row);
    /*
     * Rebuild the buffer at a new width, rewrapping logical lines. Called by
     * resize() when the column count changes and reflow is enabled.
     */
    void reflow(int newRows, int newCols, const Pen& pen);
    /* Copy a logical row into the history, evicting the oldest if needed. */
    void pushHistory(int row);
    void trimHistory();
    void clearRow(int row, const Pen& pen);
    void clearRowRange(int fromCol, int toColInclusive, int row, const Pen& pen);
    void allocate(int rows, int cols);
    void touch() { ++revision_; }

    /* Physical row index for a logical row. */
    int physicalRow(int row) const { return rowMap_[static_cast<size_t>(row)]; }

    /*
     * Decompress history row `absolute` into the scratch buffer and return it,
     * or return the buffer already holding it. Cheap to call per cell, which is
     * what viewAt() does.
     */
    const Cell* decodedHistory(int absolute, int& length) const;
    /* Any change to history_ invalidates what the scratch buffer holds. */
    void invalidateDecoded() { decodedIndex_ = -1; }

    std::vector<Cell> cells_;
    std::vector<int> rowMap_;

    /*
     * Scrollback. A deque of whole rows rather than a second flat buffer: rows
     * are appended and evicted one at a time and never addressed as a
     * rectangle, and the width they were captured at has to travel with them.
     *
     * Rows are stored compressed -- see history.h, which explains why -- and
     * expanded through decodeScratch_ on the way out.
     */
    std::deque<HistoryLine> history_;

    /* The most recently decompressed history row, and which one it is. */
    mutable std::vector<Cell> decodeScratch_;
    mutable int decodedIndex_ = -1;
    int historyLimit_ = 0;
    int viewOffset_ = 0;

    /*
     * Lines that have left the buffer, which is what stable line numbers count
     * from. Advanced by an eviction, by clearing the history -- and by a reflow,
     * which renumbers past the end of the old buffer so that a line number
     * issued before the resize resolves to nothing rather than to text that has
     * since been rewrapped.
     */
    int64_t discardedLines_ = 0;
    bool reflowEnabled_ = true;

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

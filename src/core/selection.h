/*
 * Selection - a range of the terminal buffer, and the text in it
 *
 * Kept in core/, away from Qt and the mouse, because everything difficult about
 * a terminal selection is a question about the *buffer* rather than about the
 * pointer: which cells a drag covers once it spans several rows, where a word
 * ends, whether two rows are one wrapped line or two separate ones, and what
 * the result should look like as text. All of that is far easier to pin down
 * with a headless test than by dragging across a window, and none of it needs a
 * widget.
 *
 * Coordinates are Screen's *stable line numbers* (see screen.h), not view rows.
 * A selection has to survive both the view scrolling and the buffer moving
 * underneath it: a screenful of output while text is held selected would
 * otherwise leave the highlight on whatever rows have since taken those
 * positions.
 *
 * Endpoints are inclusive. A single clicked cell is a range from itself to
 * itself, which is what makes the arithmetic below read as it does.
 */

#ifndef CORE_SELECTION_H
#define CORE_SELECTION_H

#include "screen.h"
#include <cstdint>
#include <string>

/*
 * What a drag selects.
 *
 *   Character  cell to cell, following the text round the end of each row
 *   Word       whole words, snapped out from wherever the drag reaches
 *   Line       whole logical lines, wrapped rows included
 *   Block      the rectangle between the two corners, ignoring the text
 */
enum class SelectionMode : uint8_t { Character, Word, Line, Block };

struct SelectionPoint {
    int64_t line = 0;
    int col = 0;

    friend bool operator==(const SelectionPoint& a, const SelectionPoint& b) {
        return a.line == b.line && a.col == b.col;
    }
    friend bool operator!=(const SelectionPoint& a, const SelectionPoint& b) {
        return !(a == b);
    }
    /* Reading order: earlier line first, then earlier column. */
    friend bool operator<(const SelectionPoint& a, const SelectionPoint& b) {
        return a.line != b.line ? a.line < b.line : a.col < b.col;
    }
};

/* An inclusive range, always in reading order once normalized. */
struct SelectionRange {
    SelectionPoint start;
    SelectionPoint end;

    /* Swap the ends if the drag went backwards. */
    SelectionRange normalized() const {
        return end < start ? SelectionRange{end, start} : *this;
    }
    /* The smallest range covering both. */
    SelectionRange united(const SelectionRange& other) const {
        const SelectionRange a = normalized();
        const SelectionRange b = other.normalized();
        return SelectionRange{a.start < b.start ? a.start : b.start,
                              b.end < a.end ? a.end : b.end};
    }
};

/*
 * The word around `point`, snapped to word boundaries -- what a double-click
 * selects.
 *
 * "Word" is deliberately generous: alongside letters and digits it keeps the
 * punctuation that appears *inside* the things a terminal user double-clicks,
 * which are paths, URLs, flags and identifiers rather than prose. It follows a
 * wrapped line across the seam, so a path long enough to wrap still selects
 * whole.
 *
 * A click on a blank selects the run of blanks instead, so the result is never
 * an empty selection.
 */
SelectionRange wordAt(const Screen& screen, const SelectionPoint& point);

/*
 * The whole logical line `point` is on: every row joined to it by a wrap seam,
 * from the first to the last. What a triple-click selects, and the reason a
 * command that wrapped over three rows copies back as one line.
 */
SelectionRange logicalLineAt(const Screen& screen, const SelectionPoint& point);

/*
 * The columns of `range` that fall on `line`, inclusive. False when the line is
 * outside the range entirely.
 *
 * `cols` is the grid width, which a character selection needs: every row but
 * the last runs to the right margin, because that is where the text carries on
 * from.
 */
bool rangeColumnsOn(const SelectionRange& range, SelectionMode mode, int64_t line,
                    int cols, int& firstCol, int& lastCol);

/*
 * The text of `range`.
 *
 *   - the trailer cell of a double-width character is skipped, so a CJK
 *     character or an emoji comes back once rather than twice
 *   - a row whose selected span reaches the right margin has its trailing
 *     blanks dropped: they are padding to the window width, not text. So does
 *     every row of a block selection, whose right edge is a column the user
 *     drew rather than where the text ends
 *   - a row that ends in a wrap seam runs into the next without a newline, so a
 *     wrapped command line comes back as the one line a shell will accept.
 *     A block selection never joins rows -- taking a column out of a table is
 *     the whole reason for it.
 */
std::u32string selectionText(const Screen& screen, const SelectionRange& range,
                             SelectionMode mode);

/*
 * Selection - the range, and the drag that is building it
 *
 * A drag remembers the range it started from rather than just its anchor point,
 * which is what makes a word or line drag extend by whole words or lines: the
 * result is the anchor range united with the word or line under the pointer.
 */
class Selection {
public:
    bool isEmpty() const { return !active_; }
    bool isDragging() const { return dragging_; }
    SelectionMode mode() const { return mode_; }
    const SelectionRange& range() const { return range_; }

    /* Start a drag. Word and Line modes expand immediately, so a double or
     * triple click selects without any movement. */
    void begin(const Screen& screen, const SelectionPoint& point, SelectionMode mode);
    /* Move the far end. Ignored when no drag is in progress. */
    void extend(const Screen& screen, const SelectionPoint& point);
    /* The button came up; the selection stays, the drag ends. */
    void finishDrag() { dragging_ = false; }
    void clear();

    /* Adopt a range outright -- a search match, or a select-all. */
    void set(const SelectionRange& range, SelectionMode mode = SelectionMode::Character);

    /* The columns selected on `line`; false when none are. */
    bool columnsOn(int64_t line, int cols, int& firstCol, int& lastCol) const;

    /* The selected text, or empty when there is no selection. */
    std::u32string text(const Screen& screen) const;

private:
    /* Snap `point` to a range according to the current mode. */
    SelectionRange rangeFor(const Screen& screen, const SelectionPoint& point) const;

    bool active_ = false;
    bool dragging_ = false;
    SelectionMode mode_ = SelectionMode::Character;
    /* Where the drag began, already snapped to a word or line if the mode says
     * so; the far end is united with this. */
    SelectionRange anchor_;
    SelectionRange range_;
};

#endif /* CORE_SELECTION_H */

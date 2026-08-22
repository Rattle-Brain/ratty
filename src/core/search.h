/*
 * Search - finding text in the scrollback
 *
 * The awkward part of searching a terminal buffer is not the matching, it is
 * deciding what the text *is*: a row is a slice of the window rather than a
 * line of a document, so a command long enough to wrap is spread over two rows
 * with no separator, while two rows that merely follow each other are separate
 * lines that a match must not run across. CellFlagWrapped is what tells the two
 * apart, and it is the reason this could not be written before -- see
 * doc/known-gaps.md.
 *
 * Matches come back in Selection's coordinates, so the same code that draws and
 * copies a selection draws and copies a match.
 */

#ifndef CORE_SEARCH_H
#define CORE_SEARCH_H

#include "selection.h"
#include <cstddef>
#include <string>
#include <vector>

struct SearchResults {
    /* Oldest first, so stepping backwards through them walks up the buffer. */
    std::vector<SelectionRange> matches;
    /*
     * True when the search stopped at the cap rather than at the end of the
     * buffer. Reported rather than swallowed: a count that silently means
     * "some" is worse than one that says so.
     */
    bool truncated = false;
};

/*
 * Every match of `needle` in the scrollback and the live screen.
 *
 * Case-insensitive unless asked otherwise, folding ASCII only -- a terminal
 * search is nearly always for a command, a path or an identifier, and full
 * Unicode case folding needs tables RaTTY does not carry.
 *
 * Double-width characters are matched by their own code point, with the trailer
 * cell skipped, so searching for a CJK word finds it.
 */
SearchResults searchScrollback(const Screen& screen, const std::u32string& needle,
                               bool caseSensitive = false,
                               size_t maxMatches = 10000);

#endif /* CORE_SEARCH_H */

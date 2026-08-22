/*
 * Search - implementation
 */

#include "search.h"
#include <algorithm>

namespace {

char32_t foldAscii(char32_t ch) {
    return (ch >= U'A' && ch <= U'Z') ? ch + 0x20 : ch;
}

} // namespace

SearchResults searchScrollback(const Screen& screen, const std::u32string& needle,
                               bool caseSensitive, size_t maxMatches) {
    SearchResults results;
    if (needle.empty() || screen.cols() <= 0) return results;

    const int cols = screen.cols();

    std::u32string pattern = needle;
    if (!caseSensitive) {
        for (char32_t& ch : pattern) ch = foldAscii(ch);
    }

    /*
     * One logical line at a time: its text, and for each character the position
     * it occupies within the line. The two are kept apart because a
     * double-width character is one character in the text and two positions on
     * the screen, and a match has to be reported in positions.
     */
    std::u32string text;
    std::vector<int> position;   // text index -> offset within the logical line
    std::vector<uint8_t> width;  // ... and how many columns it covers

    int64_t lineStart = screen.firstLine();
    const int64_t lastLine = screen.lastLine();

    auto flush = [&]() {
        if (text.empty() || results.truncated) {
            text.clear();
            position.clear();
            width.clear();
            return;
        }

        size_t from = 0;
        while (true) {
            const size_t hit = text.find(pattern, from);
            if (hit == std::u32string::npos) break;

            const size_t last = hit + pattern.size() - 1;
            const int startOffset = position[hit];
            const int endOffset = position[last] + width[last] - 1;

            results.matches.push_back(
                SelectionRange{{lineStart + startOffset / cols, startOffset % cols},
                               {lineStart + endOffset / cols, endOffset % cols}});

            if (results.matches.size() >= maxMatches) {
                results.truncated = true;
                break;
            }
            /* Overlapping matches are not interesting; step past this one. */
            from = hit + pattern.size();
        }

        text.clear();
        position.clear();
        width.clear();
    };

    int64_t rowsInLine = 0;
    for (int64_t line = screen.firstLine(); line <= lastLine; ++line) {
        int length = 0;
        const Cell* cells = screen.lineData(line, length);
        const int offsetBase = static_cast<int>(rowsInLine * cols);

        if (cells) {
            for (int col = 0; col < length; ++col) {
                const Cell& cell = cells[col];
                if (cell.hasFlag(CellFlagWideTrailer)) {
                    /* Counted with the character in front of it. */
                    if (!width.empty()) width.back() = 2;
                    continue;
                }
                const char32_t ch = cell.ch == 0 ? U' ' : cell.ch;
                text += caseSensitive ? ch : foldAscii(ch);
                position.push_back(offsetBase + col);
                width.push_back(1);
            }
        }

        /*
         * lineWrapped() asks the screen for the row again, which invalidates
         * `cells` -- everything above has already copied what it needs.
         */
        if (screen.lineWrapped(line)) {
            ++rowsInLine;
            continue;
        }

        flush();
        rowsInLine = 0;
        lineStart = line + 1;
    }
    flush();

    return results;
}

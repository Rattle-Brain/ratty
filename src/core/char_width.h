/*
 * charWidth - how many terminal columns a code point occupies
 *
 * A deliberately small wcwidth(): the system one depends on the process locale
 * (and on macOS quietly returns -1 for plenty of printable characters), which
 * makes grid layout non-reproducible. Terminals only need three answers -- 0
 * for combining marks, 2 for East Asian wide and emoji presentation, 1 for
 * everything else -- so the ranges are tabulated here.
 */

#ifndef CORE_CHAR_WIDTH_H
#define CORE_CHAR_WIDTH_H

#include <cstddef>

struct CodepointRange {
    char32_t first;
    char32_t last;
};

/* Combining marks and other zero-width characters. */
inline constexpr CodepointRange kZeroWidthRanges[] = {
    {0x0300, 0x036F},  // combining diacritical marks
    {0x0483, 0x0489},
    {0x0591, 0x05BD},
    {0x0610, 0x061A},
    {0x064B, 0x065F},
    {0x0670, 0x0670},
    {0x06D6, 0x06DC},
    {0x0900, 0x0903},
    {0x093A, 0x093A},
    {0x093C, 0x093C},
    {0x0941, 0x0948},
    {0x0E31, 0x0E31},
    {0x0E34, 0x0E3A},
    {0x1AB0, 0x1AFF},
    {0x1DC0, 0x1DFF},
    {0x200B, 0x200F},  // zero-width space .. RLM
    {0x2060, 0x2064},
    {0x20D0, 0x20F0},  // combining marks for symbols
    {0xFE00, 0xFE0F},  // variation selectors
    {0xFE20, 0xFE2F},
    {0xFEFF, 0xFEFF},  // BOM / zero-width no-break space
};

/* East Asian Wide / Fullwidth, plus the emoji blocks that render double-wide. */
inline constexpr CodepointRange kWideRanges[] = {
    {0x1100, 0x115F},   // Hangul Jamo initial consonants
    {0x231A, 0x231B},   // watch, hourglass
    {0x2329, 0x232A},
    {0x23E9, 0x23EC},
    {0x23F0, 0x23F0},
    {0x23F3, 0x23F3},
    {0x25FD, 0x25FE},
    {0x2614, 0x2615},
    {0x2648, 0x2653},
    {0x267F, 0x267F},
    {0x2693, 0x2693},
    {0x26A1, 0x26A1},
    {0x26AA, 0x26AB},
    {0x26BD, 0x26BE},
    {0x26C4, 0x26C5},
    {0x26CE, 0x26CE},
    {0x26D4, 0x26D4},
    {0x26EA, 0x26EA},
    {0x26F2, 0x26F3},
    {0x26F5, 0x26F5},
    {0x26FA, 0x26FA},
    {0x26FD, 0x26FD},
    {0x2705, 0x2705},
    {0x270A, 0x270B},
    {0x2728, 0x2728},
    {0x274C, 0x274C},
    {0x274E, 0x274E},
    {0x2753, 0x2755},
    {0x2757, 0x2757},
    {0x2795, 0x2797},
    {0x27B0, 0x27B0},
    {0x27BF, 0x27BF},
    {0x2B1B, 0x2B1C},
    {0x2B50, 0x2B50},
    {0x2B55, 0x2B55},
    {0x2E80, 0x303E},   // CJK radicals, Kangxi, CJK symbols
    {0x3041, 0x33FF},   // Hiragana .. CJK compatibility
    {0x3400, 0x4DBF},   // CJK extension A
    {0x4E00, 0x9FFF},   // CJK unified ideographs
    {0xA000, 0xA4CF},   // Yi
    {0xAC00, 0xD7A3},   // Hangul syllables
    {0xF900, 0xFAFF},   // CJK compatibility ideographs
    {0xFE10, 0xFE19},
    {0xFE30, 0xFE6F},
    {0xFF00, 0xFF60},   // fullwidth forms
    {0xFFE0, 0xFFE6},
    {0x16FE0, 0x16FE4},
    {0x17000, 0x18AFF}, // Tangut
    {0x1B000, 0x1B2FF},
    {0x1F004, 0x1F004},
    {0x1F0CF, 0x1F0CF},
    {0x1F18E, 0x1F18E},
    {0x1F191, 0x1F19A},
    {0x1F300, 0x1F320},
    {0x1F32D, 0x1F335},
    {0x1F337, 0x1F37C},
    {0x1F37E, 0x1F393},
    {0x1F3A0, 0x1F3CA},
    {0x1F3CF, 0x1F3D3},
    {0x1F3E0, 0x1F3F0},
    {0x1F3F4, 0x1F3F4},
    {0x1F3F8, 0x1F43E},
    {0x1F440, 0x1F440},
    {0x1F442, 0x1F4FC},
    {0x1F4FF, 0x1F53D},
    {0x1F54B, 0x1F54E},
    {0x1F550, 0x1F567},
    {0x1F57A, 0x1F57A},
    {0x1F595, 0x1F596},
    {0x1F5A4, 0x1F5A4},
    {0x1F5FB, 0x1F64F},
    {0x1F680, 0x1F6C5},
    {0x1F6CC, 0x1F6CC},
    {0x1F6D0, 0x1F6D2},
    {0x1F6EB, 0x1F6EC},
    {0x1F910, 0x1F9FF},
    {0x1FA70, 0x1FAFF},
    {0x20000, 0x2FFFD}, // CJK extensions B-F
    {0x30000, 0x3FFFD},
};

namespace detail {
template <size_t N>
constexpr bool inRanges(char32_t ch, const CodepointRange (&ranges)[N]) {
    /* Ranges are sorted, so a binary search is possible; a linear scan is
     * plenty for the table sizes here and keeps the code obvious. */
    for (size_t i = 0; i < N; ++i) {
        if (ch < ranges[i].first) return false;
        if (ch <= ranges[i].last) return true;
    }
    return false;
}
} // namespace detail

/* Returns 0, 1 or 2. Control characters report 0 and must not be printed. */
inline int charWidth(char32_t ch) {
    if (ch == 0) return 0;
    if (ch < 0x20 || (ch >= 0x7F && ch < 0xA0)) return 0;
    if (ch < 0x300) return 1;   // fast path: Latin, Greek, Cyrillic

    if (detail::inRanges(ch, kZeroWidthRanges)) return 0;
    if (detail::inRanges(ch, kWideRanges)) return 2;
    return 1;
}

#endif /* CORE_CHAR_WIDTH_H */

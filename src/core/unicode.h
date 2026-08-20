/*
 * Unicode properties a terminal needs
 *
 * Column width, and the emoji properties that decide how a code point is
 * presented. Deliberately a small hand-maintained set of range tables rather
 * than a generated database: the system's wcwidth() depends on the process
 * locale (and on macOS quietly returns -1 for plenty of printable characters),
 * which makes grid layout non-reproducible.
 *
 * The emoji tables exist because many code points have two forms. U+26A0 is
 * "warning sign", a narrow monochrome glyph, until a U+FE0F selector turns it
 * into the double-width colour emoji; U+FE0E forces the reverse. Getting this
 * wrong shows up as an emoji rendered flat and narrow, or a text symbol
 * rendered in colour and overlapping its neighbour.
 */

#ifndef CORE_UNICODE_H
#define CORE_UNICODE_H

#include <cstddef>

struct CodepointRange {
    char32_t first;
    char32_t last;
};

namespace unicode_detail {

/* Ranges are sorted, so the scan can stop early. */
template <size_t N>
constexpr bool inRanges(char32_t ch, const CodepointRange (&ranges)[N]) {
    for (size_t i = 0; i < N; ++i) {
        if (ch < ranges[i].first) return false;
        if (ch <= ranges[i].last) return true;
    }
    return false;
}

/* Combining marks and other zero-width characters. Variation selectors and the
 * joiner are handled separately, as cluster continuations. */
inline constexpr CodepointRange kZeroWidth[] = {
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
    {0x200B, 0x200F},  // zero-width space .. RLM (includes the joiner at 200D)
    {0x2060, 0x2064},
    {0x20D0, 0x20F0},  // combining marks for symbols (keycap enclosure at 20E3)
    {0xFE00, 0xFE0F},  // variation selectors
    {0xFE20, 0xFE2F},
    {0xFEFF, 0xFEFF},  // BOM / zero-width no-break space
    {0xE0000, 0xE0FFF}, // tag characters and variation selector supplement
};

/* East Asian Wide and Fullwidth. */
inline constexpr CodepointRange kWide[] = {
    {0x1100, 0x115F},   // Hangul Jamo initial consonants
    {0x2329, 0x232A},
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
    {0x20000, 0x2FFFD}, // CJK extensions B-F
    {0x30000, 0x3FFFD},
};

/*
 * Emoji_Presentation: code points that are emoji *by default*, without needing
 * a selector. These are double-width and want a colour glyph.
 *
 * Regional indicators (U+1F1E6..U+1F1FF) are deliberately absent even though
 * they carry the property: on their own they are narrow letter tiles, and it is
 * a *pair* of them that forms one double-width flag. The cluster logic in
 * TerminalEmulator handles that case.
 */
inline constexpr CodepointRange kEmojiPresentation[] = {
    {0x231A, 0x231B}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0}, {0x23F3, 0x23F3},
    {0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653}, {0x267F, 0x267F},
    {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE},
    {0x26C4, 0x26C5}, {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA},
    {0x26F2, 0x26F3}, {0x26F5, 0x26F5}, {0x26FA, 0x26FA}, {0x26FD, 0x26FD},
    {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728}, {0x274C, 0x274C},
    {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
    {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50},
    {0x2B55, 0x2B55},
    {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E},
    {0x1F191, 0x1F19A},
    {0x1F201, 0x1F201}, {0x1F21A, 0x1F21A}, {0x1F22F, 0x1F22F},
    {0x1F232, 0x1F236}, {0x1F238, 0x1F23A}, {0x1F250, 0x1F251},
    {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C},
    {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3},
    {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E},
    {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D},
    {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A},
    {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F},
    {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2},
    {0x1F6D5, 0x1F6D7}, {0x1F6DD, 0x1F6DF}, {0x1F6EB, 0x1F6EC},
    {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB}, {0x1F7F0, 0x1F7F0},
    {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945}, {0x1F947, 0x1F9FF},
    {0x1FA70, 0x1FA74}, {0x1FA78, 0x1FA7C}, {0x1FA80, 0x1FA86},
    {0x1FA90, 0x1FAAC}, {0x1FAB0, 0x1FABA}, {0x1FAC0, 0x1FAC5},
    {0x1FAD0, 0x1FAD9}, {0x1FAE0, 0x1FAE7}, {0x1FAF0, 0x1FAF6},
};

/*
 * Extended_Pictographic, approximated: the code points a U+FE0F selector can
 * legitimately turn into an emoji. Anything outside this set keeps its text
 * form even if a stray selector follows, so a selector after ordinary text
 * cannot widen a letter.
 */
inline constexpr CodepointRange kPictographic[] = {
    {0x00A9, 0x00A9}, {0x00AE, 0x00AE}, {0x203C, 0x203C}, {0x2049, 0x2049},
    {0x2122, 0x2122}, {0x2139, 0x2139}, {0x2194, 0x2199}, {0x21A9, 0x21AA},
    {0x231A, 0x231B}, {0x2328, 0x2328}, {0x2388, 0x2388}, {0x23CF, 0x23CF},
    {0x23E9, 0x23F3}, {0x23F8, 0x23FA}, {0x24C2, 0x24C2}, {0x25AA, 0x25AB},
    {0x25B6, 0x25B6}, {0x25C0, 0x25C0}, {0x25FB, 0x25FE}, {0x2600, 0x2605},
    {0x2607, 0x2612}, {0x2614, 0x2685}, {0x2690, 0x2705}, {0x2708, 0x2712},
    {0x2714, 0x2714}, {0x2716, 0x2716}, {0x271D, 0x271D}, {0x2721, 0x2721},
    {0x2728, 0x2728}, {0x2733, 0x2734}, {0x2744, 0x2744}, {0x2747, 0x2747},
    {0x274C, 0x274C}, {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757},
    {0x2763, 0x2767}, {0x2795, 0x2797}, {0x27A1, 0x27A1}, {0x27B0, 0x27B0},
    {0x27BF, 0x27BF}, {0x2934, 0x2935}, {0x2B05, 0x2B07}, {0x2B1B, 0x2B1C},
    {0x2B50, 0x2B50}, {0x2B55, 0x2B55}, {0x3030, 0x3030}, {0x303D, 0x303D},
    {0x3297, 0x3297}, {0x3299, 0x3299},
    {0x1F000, 0x1F0FF}, {0x1F10D, 0x1F10F}, {0x1F12F, 0x1F12F},
    {0x1F16C, 0x1F171}, {0x1F17E, 0x1F17F}, {0x1F18E, 0x1F18E},
    {0x1F191, 0x1F19A}, {0x1F1AD, 0x1F1E5}, {0x1F201, 0x1F20F},
    {0x1F21A, 0x1F21A}, {0x1F22F, 0x1F22F}, {0x1F232, 0x1F23A},
    {0x1F250, 0x1F251}, {0x1F260, 0x1F265}, {0x1F300, 0x1F3FA},
    {0x1F400, 0x1F6FF}, {0x1F7C0, 0x1F7FF}, {0x1F800, 0x1F8FF},
    {0x1F900, 0x1F9FF}, {0x1FA00, 0x1FAFF}, {0x1FC00, 0x1FFFD},
};

} // namespace unicode_detail

/* --------------------------------------------------------------- queries */

/* U+FE0F: present the preceding pictograph as a colour emoji. */
constexpr bool isEmojiPresentationSelector(char32_t ch) { return ch == 0xFE0F; }
/* U+FE0E: present the preceding pictograph as monochrome text. */
constexpr bool isTextPresentationSelector(char32_t ch) { return ch == 0xFE0E; }
constexpr bool isVariationSelector(char32_t ch) {
    return (ch >= 0xFE00 && ch <= 0xFE0F) || (ch >= 0xE0100 && ch <= 0xE01EF);
}

constexpr bool isZeroWidthJoiner(char32_t ch) { return ch == 0x200D; }
/* Skin-tone modifiers, which attach to the preceding emoji. */
constexpr bool isEmojiModifier(char32_t ch) { return ch >= 0x1F3FB && ch <= 0x1F3FF; }
/* Regional indicator symbols; a pair of them is one flag. */
constexpr bool isRegionalIndicator(char32_t ch) { return ch >= 0x1F1E6 && ch <= 0x1F1FF; }
/* Tag characters, used by the subdivision flag sequences. */
constexpr bool isTagCharacter(char32_t ch) { return ch >= 0xE0020 && ch <= 0xE007F; }
/* U+20E3, the enclosing keycap that completes a keycap sequence. */
constexpr bool isEnclosingKeycap(char32_t ch) { return ch == 0x20E3; }

inline bool isZeroWidth(char32_t ch) {
    return unicode_detail::inRanges(ch, unicode_detail::kZeroWidth);
}

/*
 * A space that occupies its columns and paints nothing: Unicode's Zs category.
 *
 * Worth a predicate of its own because a *blank* glyph and a *missing* glyph are
 * indistinguishable to a coverage test -- both have an empty outline -- so a
 * renderer that asks the font chain for U+00A0 gets told no font has it and
 * falls back to drawing .notdef. `tree` indents with two NO-BREAK SPACEs per
 * level, which is how its output turned into a field of empty boxes.
 *
 * U+200B and the other zero-width spaces are not here: they take no columns at
 * all and are handled by isZeroWidth().
 */
constexpr bool isSpaceSeparator(char32_t ch) {
    switch (ch) {
    case 0x0020:   // SPACE
    case 0x00A0:   // NO-BREAK SPACE
    case 0x1680:   // OGHAM SPACE MARK
    case 0x202F:   // NARROW NO-BREAK SPACE
    case 0x205F:   // MEDIUM MATHEMATICAL SPACE
    case 0x3000:   // IDEOGRAPHIC SPACE (double-width, still blank)
        return true;
    default:
        /* U+2000..U+200A: EN QUAD through HAIR SPACE, figure and punctuation
         * spaces among them. */
        return ch >= 0x2000 && ch <= 0x200A;
    }
}

/* True when `ch` is emoji by default, so it needs no selector. */
inline bool hasEmojiPresentationByDefault(char32_t ch) {
    if (ch < 0x231A) return false;
    return unicode_detail::inRanges(ch, unicode_detail::kEmojiPresentation);
}

/* True when a U+FE0F selector can turn `ch` into an emoji. */
inline bool isExtendedPictographic(char32_t ch) {
    if (ch < 0x00A9) return false;
    return unicode_detail::inRanges(ch, unicode_detail::kPictographic);
}

/*
 * Columns `ch` occupies on its own: 0 for a zero-width mark, 2 for East Asian
 * wide characters and default-presentation emoji, 1 otherwise. Control
 * characters report 0 and must not be printed.
 *
 * A code point whose presentation is changed by a selector does *not* change
 * width here -- the caller applies that, because it depends on the sequence
 * rather than on the code point alone.
 */
inline int charWidth(char32_t ch) {
    if (ch == 0) return 0;
    if (ch < 0x20 || (ch >= 0x7F && ch < 0xA0)) return 0;
    if (ch < 0x231A) return 1;   // fast path: Latin, Greek, Cyrillic, punctuation

    if (isZeroWidth(ch)) return 0;
    if (hasEmojiPresentationByDefault(ch)) return 2;
    if (unicode_detail::inRanges(ch, unicode_detail::kWide)) return 2;
    return 1;
}

/* Columns a cell takes once its presentation is known. */
constexpr int presentationWidth(bool emojiPresentation, int defaultWidth) {
    /* Emoji presentation is always double-width; text presentation is whatever
     * the code point is on its own. */
    return emojiPresentation ? 2 : defaultWidth;
}

#endif /* CORE_UNICODE_H */

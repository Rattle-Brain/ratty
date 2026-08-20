/*
 * Cell - a single character cell of the terminal grid
 *
 * A cell is deliberately small and self-contained: 16 bytes, trivially
 * copyable, no Qt types. Colours are stored *symbolically* (see Color) rather
 * than as resolved RGB values, so that "default foreground" stays default
 * until the renderer resolves it through the active Palette. That is what
 * lets a theme change repaint correctly without rewriting the grid.
 */

#ifndef CORE_CELL_H
#define CORE_CELL_H

#include <cstdint>

/*
 * Color - symbolic terminal colour.
 *
 *   Default    - "whatever the palette says the default fg/bg is"
 *   Indexed    - one of the 256 palette slots (SGR 30-37/40-47/90-97/100-107
 *                and SGR 38;5;N / 48;5;N)
 *   Rgb        - direct 24-bit colour (SGR 38;2;R;G;B / 48;2;R;G;B)
 */
struct Color {
    enum class Kind : uint8_t { Default = 0, Indexed = 1, Rgb = 2 };

    Kind kind = Kind::Default;
    uint8_t r = 0;   // palette index when kind == Indexed
    uint8_t g = 0;
    uint8_t b = 0;

    static constexpr Color defaultColor() { return Color{}; }
    static constexpr Color indexed(uint8_t index) { return Color{Kind::Indexed, index, 0, 0}; }
    static constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) { return Color{Kind::Rgb, r, g, b}; }

    constexpr bool isDefault() const { return kind == Kind::Default; }
    constexpr uint8_t index() const { return r; }

    constexpr bool operator==(const Color& o) const {
        return kind == o.kind && r == o.r && g == o.g && b == o.b;
    }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }
};

/* Rendition flags (SGR attributes) carried by every cell. */
enum CellFlag : uint16_t {
    CellFlagNone        = 0,
    CellFlagBold        = 1 << 0,
    CellFlagFaint       = 1 << 1,
    CellFlagItalic      = 1 << 2,
    CellFlagUnderline   = 1 << 3,
    CellFlagBlink       = 1 << 4,
    CellFlagInverse     = 1 << 5,
    CellFlagInvisible   = 1 << 6,
    CellFlagStrike      = 1 << 7,
    /*
     * Continuation half of a double-width (CJK/emoji) character. The glyph is
     * drawn from the leading cell; this one is a placeholder that must not be
     * painted, but still occupies a grid column.
     */
    CellFlagWideTrailer = 1 << 8,
};

/*
 * Pen - the "current graphic rendition": the colours and flags that newly
 * printed characters inherit. SGR sequences mutate the pen, never the grid.
 */
struct Pen {
    Color fg = Color::defaultColor();
    Color bg = Color::defaultColor();
    uint16_t flags = CellFlagNone;

    void reset() { *this = Pen{}; }

    void setFlag(uint16_t flag, bool on) {
        if (on) flags |= flag;
        else    flags = static_cast<uint16_t>(flags & ~flag);
    }
    bool hasFlag(uint16_t flag) const { return (flags & flag) != 0; }
};

/* A single grid cell. */
struct Cell {
    char32_t ch = U' ';
    Color fg = Color::defaultColor();
    Color bg = Color::defaultColor();
    uint16_t flags = CellFlagNone;

    bool hasFlag(uint16_t flag) const { return (flags & flag) != 0; }

    /* Reset to a blank cell that carries `pen`'s colours (VT erase semantics:
     * erased cells keep the current background, which is how full-width
     * coloured bars are drawn by TUI apps). */
    void erase(const Pen& pen) {
        ch = U' ';
        fg = pen.fg;
        bg = pen.bg;
        /* Erased cells keep no rendition other than the background. */
        flags = CellFlagNone;
    }

    /* True when the cell would paint nothing but its background. */
    bool isBlank() const { return ch == U' ' || ch == 0; }
};

#endif /* CORE_CELL_H */

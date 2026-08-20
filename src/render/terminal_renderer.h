/*
 * TerminalRenderer - turns a Screen into draw calls
 *
 * The only place that knows how a grid maps onto pixels. This used to be an
 * 80-line double loop inside TerminalWidget, which meant the widget owned input
 * handling, process management, layout arithmetic *and* painting; pulling it out
 * makes both halves legible and lets the layout maths be reasoned about on its
 * own.
 *
 * Everything here is in physical pixels.
 */

#ifndef RENDER_TERMINAL_RENDERER_H
#define RENDER_TERMINAL_RENDERER_H

#include "../core/cursor.h"
#include "../core/palette.h"
#include "../core/screen.h"
#include "gl_renderer.h"

class TerminalRenderer {
public:
    struct Options {
        bool cursorVisible = true;
        CursorStyle cursorStyle = CursorStyle::Block;
        /* Blink phase; when false a blinking cursor is hidden this frame. */
        bool cursorPhaseOn = true;
    };

    /* Grid geometry derived from the font metrics and the viewport. */
    struct Layout {
        int cellWidth = 0;
        int cellHeight = 0;
        int baseline = 0;    // baseline offset within a cell
        int originX = 0;     // left padding, centring the grid in the viewport
        int originY = 0;
        int rows = 0;
        int cols = 0;

        bool isValid() const { return cellWidth > 0 && cellHeight > 0; }
    };

    /*
     * Rows/cols that fit `pixelWidth` x `pixelHeight`, inset by `padding`
     * physical pixels on every side, plus the offset needed to centre them.
     *
     * `padding` keeps the text off the window edge; any pixels left over after
     * whole cells are divided are then split evenly, so the grid stays centred
     * at any window size.
     */
    static Layout computeLayout(const FontMetrics& metrics, int pixelWidth, int pixelHeight,
                                int padding = 0);

    void paint(GLRenderer& renderer, const Screen& screen, const Palette& palette,
               const Layout& layout, const Options& options) const;

private:
    void paintBackgrounds(GLRenderer& renderer, const Screen& screen, const Palette& palette,
                          const Layout& layout) const;
    void paintGlyphs(GLRenderer& renderer, const Screen& screen, const Palette& palette,
                     const Layout& layout) const;
    void paintCursor(GLRenderer& renderer, const Screen& screen, const Palette& palette,
                     const Layout& layout, const Options& options) const;
};

#endif /* RENDER_TERMINAL_RENDERER_H */

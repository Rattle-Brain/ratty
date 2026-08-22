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
#include "../core/selection.h"
#include "gl_renderer.h"
#include <string>
#include <vector>

class TerminalRenderer {
public:
    struct Options {
        bool cursorVisible = true;
        CursorStyle cursorStyle = CursorStyle::Block;
        /* Blink phase; when false a blinking cursor is hidden this frame. */
        bool cursorPhaseOn = true;

        /*
         * Text selection, in Screen's stable line numbers; null when there is
         * none. Painted into the background layer, under the glyphs, so the
         * selected text keeps its full contrast -- see paintHighlights().
         */
        const Selection* selection = nullptr;

        /*
         * Scrollback search: every match, and which one the user is looking at.
         * Same coordinates as the selection, because a match *is* one.
         */
        const std::vector<SelectionRange>* matches = nullptr;
        int currentMatch = -1;

        /*
         * A one-row status line -- the search prompt -- drawn over the bottom
         * row of the grid, which is left unpainted to make room for it.
         *
         * It has to displace a row rather than sit on top of one: the overlay
         * layer is above the glyphs, so a bar drawn there would cover its own
         * text, and the grid is not resized for it because the shell must not
         * see the window change size because someone opened a search box.
         */
        const std::u32string* statusLine = nullptr;

        /* A slim thumb down the right edge, so a view that is not live says so. */
        bool scrollIndicator = false;
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

    /* Not const: the per-row scratch buffer below is reused across frames
     * rather than reallocated on every one. */
    void paint(GLRenderer& renderer, const Screen& screen, const Palette& palette,
               const Layout& layout, const Options& options);

private:
    /* Backgrounds, glyphs and decorations, in one pass over the grid. `rows` is
     * how many rows to paint, which is one short of the grid when a status line
     * has taken the bottom one. */
    void paintGrid(GLRenderer& renderer, const Screen& screen, const Palette& palette,
                   const Layout& layout, int rows);
    /* Selection and search highlights, in the overlay layer. */
    void paintHighlights(GLRenderer& renderer, const Screen& screen, const Palette& palette,
                         const Layout& layout, const Options& options, int rows) const;
    void paintCursor(GLRenderer& renderer, const Screen& screen, const Palette& palette,
                     const Layout& layout, const Options& options, int rows) const;
    /* Where the view sits in the scrollback, as a thumb on the right edge. */
    void paintScrollIndicator(GLRenderer& renderer, const Screen& screen,
                              const Palette& palette, const Layout& layout) const;
    /* The search prompt, in the row paintGrid() left empty. */
    void paintStatusLine(GLRenderer& renderer, const Palette& palette, const Layout& layout,
                         const std::u32string& text) const;

    /* One row's worth of resolved colours, so the palette is consulted once per
     * cell instead of once per cell per layer. Kept as a member purely to reuse
     * its allocation between frames. */
    struct CellColors {
        const Cell* cell = nullptr;
        QColor fg;
        QColor bg;
    };
    std::vector<CellColors> rowColors_;
};

#endif /* RENDER_TERMINAL_RENDERER_H */

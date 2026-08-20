/*
 * TerminalRenderer - grid to draw calls
 */

#include "terminal_renderer.h"
#include "../core/unicode.h"
#include <algorithm>

TerminalRenderer::Layout TerminalRenderer::computeLayout(const FontMetrics& metrics,
                                                         int pixelWidth, int pixelHeight,
                                                         int padding) {
    Layout layout;
    if (!metrics.isValid() || pixelWidth <= 0 || pixelHeight <= 0) {
        return layout;
    }

    layout.cellWidth = metrics.cellWidth;
    layout.cellHeight = metrics.cellHeight;
    layout.baseline = metrics.ascender;

    /*
     * Never let padding squeeze the grid out of existence: on a very small
     * window, give up the padding rather than the last row or column.
     */
    const int maxPaddingX = std::max(0, (pixelWidth - metrics.cellWidth) / 2);
    const int maxPaddingY = std::max(0, (pixelHeight - metrics.cellHeight) / 2);
    const int padX = std::clamp(padding, 0, maxPaddingX);
    const int padY = std::clamp(padding, 0, maxPaddingY);

    const int usableWidth = pixelWidth - 2 * padX;
    const int usableHeight = pixelHeight - 2 * padY;

    layout.cols = std::max(1, usableWidth / metrics.cellWidth);
    layout.rows = std::max(1, usableHeight / metrics.cellHeight);

    /*
     * Split the pixels left over after whole cells evenly, instead of leaving
     * them all on the right and bottom edges. Without this a window that is not
     * an exact multiple of the cell size looks visibly off-centre.
     */
    layout.originX = padX + (usableWidth - layout.cols * layout.cellWidth) / 2;
    layout.originY = padY + (usableHeight - layout.rows * layout.cellHeight) / 2;
    return layout;
}

void TerminalRenderer::paint(GLRenderer& renderer, const Screen& screen, const Palette& palette,
                             const Layout& layout, const Options& options) const {
    if (!layout.isValid()) return;

    /* Submission order does not matter -- GLRenderer keeps the layers apart --
     * but reads best bottom-up. */
    paintBackgrounds(renderer, screen, palette, layout);
    paintGlyphs(renderer, screen, palette, layout);
    paintCursor(renderer, screen, palette, layout, options);
}

void TerminalRenderer::paintBackgrounds(GLRenderer& renderer, const Screen& screen,
                                        const Palette& palette, const Layout& layout) const {
    const QColor defaultBackground = palette.defaultBackground();
    const int rows = std::min(layout.rows, screen.rows());
    const int cols = std::min(layout.cols, screen.cols());

    for (int row = 0; row < rows; ++row) {
        const int y = layout.originY + row * layout.cellHeight;

        /*
         * Merge horizontally adjacent cells that share a background into a
         * single quad. A full-width coloured bar costs 6 vertices instead of
         * 6 per column, and the common case (everything on the default
         * background) emits nothing at all because the frame was already
         * cleared to that colour.
         */
        int runStart = 0;
        QColor runColor;
        bool runActive = false;

        auto flushRun = [&](int endColExclusive) {
            if (!runActive) return;
            const int x = layout.originX + runStart * layout.cellWidth;
            const int width = (endColExclusive - runStart) * layout.cellWidth;
            renderer.fillBackground(x, y, width, layout.cellHeight, runColor);
            runActive = false;
        };

        for (int col = 0; col < cols; ++col) {
            QColor fg;
            QColor bg;
            palette.resolveCell(screen.viewAt(row, col), fg, bg);

            const bool needsFill = (bg != defaultBackground);

            if (runActive && needsFill && bg == runColor) {
                continue;   // extend the current run
            }

            flushRun(col);

            if (needsFill) {
                runStart = col;
                runColor = bg;
                runActive = true;
            }
        }
        flushRun(cols);
    }
}

void TerminalRenderer::paintGlyphs(GLRenderer& renderer, const Screen& screen,
                                   const Palette& palette, const Layout& layout) const {
    const int rows = std::min(layout.rows, screen.rows());
    const int cols = std::min(layout.cols, screen.cols());

    for (int row = 0; row < rows; ++row) {
        const int cellTop = layout.originY + row * layout.cellHeight;
        const int baselineY = cellTop + layout.baseline;

        for (int col = 0; col < cols; ++col) {
            /* viewAt(), not at(): with the view scrolled back these rows come
             * from the scrollback, while the cursor and everything the
             * application writes still belong to the live grid. */
            const Cell& cell = screen.viewAt(row, col);

            /* The trailing half of a wide character carries no glyph of its
             * own; drawing it would double-strike the left half. */
            if (cell.hasFlag(CellFlagWideTrailer)) continue;

            QColor fg;
            QColor bg;
            palette.resolveCell(cell, fg, bg);

            const int cellLeft = layout.originX + col * layout.cellWidth;

            /*
             * A space paints nothing but its background. That includes the
             * spaces that are not U+0020: asking the font chain for U+00A0 gets
             * "no font covers this" -- a blank glyph fails a coverage test the
             * same way a missing one does -- and the .notdef box that follows is
             * what turned `tree`'s indentation into rows of empty rectangles.
             */
            if (!cell.isBlank() && !isSpaceSeparator(cell.ch)) {
                /*
                 * Bold and italic now pick a real font style. Previously every
                 * cell was drawn with the regular face and bold was faked by
                 * lightening the colour, so bold text was merely brighter.
                 */
                const FontStyle style = fontStyleFor(cell.hasFlag(CellFlagBold),
                                                     cell.hasFlag(CellFlagItalic));
                /*
                 * The emulator has already decided which form of a dual-form
                 * code point this is; asking for Auto here would let the font
                 * chain override a U+FE0E/U+FE0F selector.
                 */
                const GlyphPresentation presentation =
                    cell.isEmojiPresentation() ? GlyphPresentation::Emoji
                                               : GlyphPresentation::Text;
                renderer.drawGlyph(cell.ch, style, presentation, cellLeft, baselineY, fg);
            }

            /* Decorations go in the background layer, submitted after the cell
             * fills, so they sit above the background and below the glyph. */
            if (cell.hasFlag(CellFlagUnderline)) {
                const int thickness = std::max(1, layout.cellHeight / 16);
                const int y = std::min(baselineY + std::max(1, layout.cellHeight / 12),
                                       cellTop + layout.cellHeight - thickness);
                renderer.fillBackground(cellLeft, y, layout.cellWidth, thickness, fg);
            }
            if (cell.hasFlag(CellFlagStrike)) {
                const int thickness = std::max(1, layout.cellHeight / 16);
                renderer.fillBackground(cellLeft, baselineY - layout.cellHeight / 4,
                                        layout.cellWidth, thickness, fg);
            }
        }
    }
}

void TerminalRenderer::paintCursor(GLRenderer& renderer, const Screen& screen,
                                   const Palette& palette, const Layout& layout,
                                   const Options& options) const {
    if (!options.cursorVisible || !options.cursorPhaseOn || !screen.cursorVisible()) {
        return;
    }

    /*
     * The cursor is at a position on the *live* screen, which sits `viewOffset`
     * rows below what is being displayed; scrolled far enough back it leaves the
     * view entirely and must not be drawn.
     */
    const int row = screen.cursorRow() + screen.viewOffset();
    const int col = screen.cursorCol();
    if (row < 0 || row >= layout.rows || col < 0 || col >= layout.cols) return;

    const int x = layout.originX + col * layout.cellWidth;
    const int y = layout.originY + row * layout.cellHeight;
    const QColor color = palette.cursorColor();

    switch (options.cursorStyle) {
    case CursorStyle::Block: {
        /*
         * A translucent block keeps the character underneath readable without
         * needing a second text pass in the background colour.
         */
        QColor blockColor = color;
        blockColor.setAlpha(140);
        renderer.fillOverlay(x, y, layout.cellWidth, layout.cellHeight, blockColor);
        break;
    }
    case CursorStyle::HollowBlock:
        renderer.strokeOverlay(x, y, layout.cellWidth, layout.cellHeight,
                               std::max(1, layout.cellHeight / 16), color);
        break;
    case CursorStyle::Underline: {
        const int thickness = std::max(2, layout.cellHeight / 10);
        renderer.fillOverlay(x, y + layout.cellHeight - thickness,
                             layout.cellWidth, thickness, color);
        break;
    }
    case CursorStyle::Bar: {
        const int thickness = std::max(2, layout.cellWidth / 8);
        renderer.fillOverlay(x, y, thickness, layout.cellHeight, color);
        break;
    }
    }
}

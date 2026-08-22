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
                             const Layout& layout, const Options& options) {
    if (!layout.isValid()) return;

    /* A status line takes the bottom row, so the grid gives one up rather than
     * being painted underneath it. */
    const int rows = options.statusLine ? std::max(0, layout.rows - 1) : layout.rows;

    paintGrid(renderer, screen, palette, layout, rows);
    paintHighlights(renderer, screen, palette, layout, options, rows);
    paintCursor(renderer, screen, palette, layout, options, rows);
    if (options.scrollIndicator) {
        paintScrollIndicator(renderer, screen, palette, layout);
    }
    if (options.statusLine) {
        paintStatusLine(renderer, palette, layout, *options.statusLine);
    }
}

void TerminalRenderer::paintGrid(GLRenderer& renderer, const Screen& screen,
                                 const Palette& palette, const Layout& layout,
                                 int rowLimit) {
    static const Cell blank{};

    const QColor defaultBackground = palette.defaultBackground();
    const int rows = std::min(std::min(layout.rows, rowLimit), screen.rows());
    const int cols = std::min(layout.cols, screen.cols());
    if (rows <= 0 || cols <= 0) return;

    /*
     * One pass over the grid, not two.
     *
     * Backgrounds and glyphs used to be separate traversals, which meant every
     * cell was read twice and -- more expensively -- resolved through the
     * palette twice, once for a background colour that the glyph pass threw
     * away and once for a foreground colour the background pass had thrown away.
     * On a large window that is tens of thousands of redundant resolutions per
     * frame. Resolving each cell once into a per-row scratch buffer costs a few
     * kilobytes and halves the work.
     *
     * The draw order is unchanged, and has to be: within a row the background
     * runs are submitted before that row's underlines and strikethroughs, so
     * the decorations still land on top of the fills. Rows are submitted in
     * order too, and since two rows never share a pixel it does not matter that
     * row N's decorations now precede row N+1's fills.
     */
    rowColors_.resize(static_cast<size_t>(cols));

    for (int row = 0; row < rows; ++row) {
        int rowLength = 0;
        const Cell* cells = screen.viewRow(row, rowLength);

        const int y = layout.originY + row * layout.cellHeight;
        const int baselineY = y + layout.baseline;

        for (int col = 0; col < cols; ++col) {
            const Cell& cell = (cells && col < rowLength) ? cells[col] : blank;
            CellColors& resolved = rowColors_[static_cast<size_t>(col)];
            resolved.cell = &cell;
            palette.resolveCell(cell, resolved.fg, resolved.bg);
        }

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
            const QColor& bg = rowColors_[static_cast<size_t>(col)].bg;
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

        for (int col = 0; col < cols; ++col) {
            const CellColors& resolved = rowColors_[static_cast<size_t>(col)];
            const Cell& cell = *resolved.cell;

            /* The trailing half of a wide character carries no glyph of its
             * own; drawing it would double-strike the left half. */
            if (cell.hasFlag(CellFlagWideTrailer)) continue;

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
                 * Bold and italic pick a real font style rather than faking
                 * bold by lightening the colour.
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
                renderer.drawGlyph(cell.ch, style, presentation, cellLeft, baselineY,
                                   resolved.fg);
            }

            /* Decorations go in the background layer, submitted after this
             * row's cell fills, so they sit above the background and below the
             * glyph. */
            if (cell.hasFlag(CellFlagUnderline)) {
                const int thickness = std::max(1, layout.cellHeight / 16);
                const int underlineY = std::min(baselineY + std::max(1, layout.cellHeight / 12),
                                                y + layout.cellHeight - thickness);
                renderer.fillBackground(cellLeft, underlineY, layout.cellWidth, thickness,
                                        resolved.fg);
            }
            if (cell.hasFlag(CellFlagStrike)) {
                const int thickness = std::max(1, layout.cellHeight / 16);
                renderer.fillBackground(cellLeft, baselineY - layout.cellHeight / 4,
                                        layout.cellWidth, thickness, resolved.fg);
            }
        }
    }
}

/* ------------------------------------------------------------- highlights */

namespace {

/*
 * A search highlight is a tint over finished text: the match keeps its own
 * colours and is marked rather than repainted, so several matches on screen at
 * once stay readable. Hence the overlay layer, and hence the alpha -- a theme
 * states its colours opaque, since they are meant to be backgrounds.
 */
QColor translucent(QColor color, int alpha) {
    if (color.alpha() == 255) color.setAlpha(alpha);
    return color;
}

constexpr int kMatchAlpha = 80;
constexpr int kCurrentMatchAlpha = 150;

} // namespace

void TerminalRenderer::paintHighlights(GLRenderer& renderer, const Screen& screen,
                                       const Palette& palette, const Layout& layout,
                                       const Options& options, int rowLimit) const {
    const int rows = std::min(std::min(layout.rows, rowLimit), screen.rows());
    const int cols = std::min(layout.cols, screen.cols());
    if (rows <= 0 || cols <= 0) return;

    /* Rows are addressed by stable line number, which is what a selection and a
     * search match are held in: the view is a window onto the buffer, and the
     * highlight belongs to the text rather than to the row it happens to be on. */
    const int64_t topLine = screen.viewTopLine();

    /* `background` chooses the layer: a selection replaces the cell's background
     * and a match tints over the finished text. See the note on each below. */
    auto highlight = [&](int64_t line, int firstCol, int lastCol, const QColor& color,
                         bool background) {
        const int row = static_cast<int>(line - topLine);
        if (row < 0 || row >= rows) return;
        const int from = std::max(0, firstCol);
        const int to = std::min(cols - 1, lastCol);
        if (from > to) return;

        const int x = layout.originX + from * layout.cellWidth;
        const int y = layout.originY + row * layout.cellHeight;
        const int width = (to - from + 1) * layout.cellWidth;
        if (background) {
            renderer.fillBackground(x, y, width, layout.cellHeight, color);
        } else {
            renderer.fillOverlay(x, y, width, layout.cellHeight, color);
        }
    };

    /*
     * The selection goes in the *background* layer, not over the glyphs.
     *
     * Layer order does the work: this is submitted after the grid's own cell
     * backgrounds, so it covers them, and the glyphs are a layer above it, so
     * the selected text is drawn on top at full contrast rather than seen
     * through a veil. That is what lets the theme's selection colour be used as
     * the opaque background it was written to be -- and it costs one quad per
     * row rather than a second pass over the cells to redraw their text.
     */
    if (options.selection && !options.selection->isEmpty()) {
        const QColor color = palette.selectionBackground();
        const SelectionRange span = options.selection->range().normalized();
        const int64_t from = std::max(span.start.line, topLine);
        const int64_t to = std::min(span.end.line, topLine + rows - 1);
        for (int64_t line = from; line <= to; ++line) {
            int firstCol = 0;
            int lastCol = 0;
            if (!options.selection->columnsOn(line, cols, firstCol, lastCol)) continue;
            highlight(line, firstCol, lastCol, color, /*background=*/true);
        }
    }

    if (options.matches) {
        const QColor matchColor = translucent(palette.entry(3), kMatchAlpha);
        const QColor currentColor = translucent(palette.entry(11), kCurrentMatchAlpha);
        const int64_t bottomLine = topLine + rows - 1;

        for (size_t index = 0; index < options.matches->size(); ++index) {
            const SelectionRange match = (*options.matches)[index].normalized();
            /* The buffer can hold hundreds of thousands of lines; only the
             * screenful in front of the user is worth walking. */
            if (match.end.line < topLine || match.start.line > bottomLine) continue;

            const bool current = static_cast<int>(index) == options.currentMatch;
            for (int64_t line = match.start.line; line <= match.end.line; ++line) {
                int firstCol = 0;
                int lastCol = 0;
                if (!rangeColumnsOn(match, SelectionMode::Character, line, cols,
                                    firstCol, lastCol)) {
                    continue;
                }
                highlight(line, firstCol, lastCol, current ? currentColor : matchColor,
                          /*background=*/false);
            }
        }
    }
}

void TerminalRenderer::paintScrollIndicator(GLRenderer& renderer, const Screen& screen,
                                            const Palette& palette,
                                            const Layout& layout) const {
    const int history = screen.historySize();
    if (history <= 0) return;

    /*
     * The thumb's size and position are the view's share of the whole buffer,
     * so it reads as a scrollbar without being one: there is nothing to drag,
     * only something to tell the user that what they are looking at is not the
     * live screen.
     */
    const int total = history + layout.rows;
    const int trackTop = layout.originY;
    const int trackHeight = layout.rows * layout.cellHeight;
    if (trackHeight <= 0 || total <= 0) return;

    const int width = std::max(2, layout.cellWidth / 3);
    const int x = layout.originX + layout.cols * layout.cellWidth - width;

    const int thumbHeight = std::max(layout.cellHeight,
                                     trackHeight * layout.rows / total);
    const int available = std::max(0, trackHeight - thumbHeight);
    /* viewOffset counts rows back from the live screen, so the distance from the
     * top of the buffer is history - viewOffset. */
    const int fromTop = history - screen.viewOffset();
    const int thumbTop = trackTop + (history > 0 ? available * fromTop / history : available);

    QColor track = palette.defaultForeground();
    track.setAlpha(40);
    QColor thumb = palette.defaultForeground();
    thumb.setAlpha(150);

    renderer.fillOverlay(x, trackTop, width, trackHeight, track);
    renderer.fillOverlay(x, thumbTop, width, thumbHeight, thumb);
}

void TerminalRenderer::paintStatusLine(GLRenderer& renderer, const Palette& palette,
                                       const Layout& layout,
                                       const std::u32string& text) const {
    const int row = layout.rows - 1;
    if (row < 0) return;

    const int y = layout.originY + row * layout.cellHeight;
    const int width = layout.cols * layout.cellWidth;

    /*
     * Opaque, and in the background layer: the row it sits in was left
     * unpainted, so there is nothing underneath to show through, and being in
     * the background layer means the glyphs below can be ordinary text drawn on
     * top of it rather than something the overlay layer would cover.
     */
    QColor background = palette.selectionBackground();
    background.setAlpha(255);
    renderer.fillBackground(layout.originX, y, width, layout.cellHeight, background);

    const QColor foreground = palette.defaultForeground();
    const int baselineY = y + layout.baseline;
    const int limit = std::min(static_cast<int>(text.size()), layout.cols);
    for (int col = 0; col < limit; ++col) {
        const char32_t ch = text[static_cast<size_t>(col)];
        if (ch == U' ' || ch == 0) continue;
        renderer.drawGlyph(ch, FontStyleRegular, GlyphPresentation::Text,
                           layout.originX + col * layout.cellWidth, baselineY, foreground);
    }
}

void TerminalRenderer::paintCursor(GLRenderer& renderer, const Screen& screen,
                                   const Palette& palette, const Layout& layout,
                                   const Options& options, int rowLimit) const {
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
    if (row < 0 || row >= std::min(layout.rows, rowLimit)) return;
    if (col < 0 || col >= layout.cols) return;

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

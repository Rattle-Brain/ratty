/*
 * Box drawing - geometric glyphs for the line and block characters
 *
 * TUI borders, tree guides and progress bars are built from U+2500-U+259F, and
 * those characters have to *tile*: a vertical line in one row must meet the one
 * below it with no seam, and a horizontal line must span the cell exactly.
 *
 * No font can guarantee that here. Different families draw a different
 * proportion of the em, so as soon as these characters come from a fallback font
 * -- which is normal, since patched icon fonts routinely omit the whole block --
 * lines arrive a pixel or two short and borders look dashed. Scaling the
 * fallback to compensate only trades gaps for overhang.
 *
 * Drawing them from the cell geometry instead makes tiling exact by
 * construction. The result is an ordinary 8-bit coverage mask, so it caches in
 * the glyph atlas and renders through the same path as any other glyph.
 */

#ifndef RENDER_BOX_DRAWING_H
#define RENDER_BOX_DRAWING_H

#include <cstdint>
#include <vector>

/* True if `codepoint` is one this module can draw. */
bool isBoxDrawingCodepoint(char32_t codepoint);

/*
 * Render `codepoint` into an 8-bit coverage mask exactly `cellWidth` x
 * `cellHeight`. Returns false when the code point is not supported, leaving
 * `pixels` untouched so the caller can fall back to a font.
 */
bool renderBoxDrawing(char32_t codepoint, int cellWidth, int cellHeight,
                      int lineThickness, std::vector<uint8_t>& pixels);

#endif /* RENDER_BOX_DRAWING_H */

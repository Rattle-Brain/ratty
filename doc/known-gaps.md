# Known gaps


Tracked in `todo-ratty.md`; listed here with the architectural context.

**The scrollback is not reflowed on resize.** History rows are stored at the
width they had when they were captured, so a narrower window shows old lines
truncated rather than rewrapped. Doing better needs a record of which rows were
continuations of one logical line, which `Cell` does not carry — and rewrapping
from the stored cells alone would mangle TUI output that was never a paragraph.
The same missing information is why there is no scrollback *search* yet.

**No text selection.** `copySelection()` logs and returns. `Palette` already
carries a selection colour, `GLRenderer` has an overlay layer, and
`TerminalWidget` now hit-tests the mouse to a cell and knows when the application
does *not* want the mouse — so what is left is a selection range, the drag state
machine over those hooks, and a grid→string conversion that handles wide
characters and trailing blanks.

**No pixel-resolution mouse reporting (`?1016`).** Reports are per cell, which is
all a text application needs; `?1016` exists for graphics protocols RaTTY does not
implement either.

**Grapheme clusters keep only their base code point.** `Cell` stores a single
`char32_t`, so combining marks and emoji continuations are consumed rather than
retained. Widths and cursor movement are correct
([grapheme clusters](terminal-emulation.md#grapheme-clusters-and-emoji-presentation)), but the exact sequence
is not recoverable — which will matter once text selection exists. The fix is a
side table of cluster extensions keyed by cell.

**A fallback glyph is not scaled to the cell.** `matchFallbackSize()` matches line
height, not advance, so a private-use icon served from a CJK font (whose em is
square) is drawn about two columns wide in a one-column cell and bleeds into its
neighbour. Scaling it down would shrink legitimate CJK text served the same way,
so the trade-off wants a decision rather than a patch.

**No text shaping, so joined emoji show their base.** Rendering `👨‍💻` as one
combined glyph needs GSUB ligature substitution, which FreeType alone cannot do;
the same goes for flags, keycaps and skin-tone variants. Each occupies the right
two columns and draws its base emoji. HarfBuzz would fix this and ligatures at
once.

**No ligatures or complex shaping.** Rendering is glyph-per-cell with no
HarfBuzz, which is the right default for a terminal grid but rules out
programming ligatures.

**`unicode.h` tables are hand-maintained.** `Emoji_Presentation` and
`Extended_Pictographic` are transcribed range tables, not generated from
`emoji-data.txt`, so they will drift from newer Unicode revisions.

**No gamma-correct blending.** Glyph coverage is blended in sRGB space, which
makes light-on-dark text slightly thinner than a gamma-aware blend would. A
correction term in `text.frag` would be a small, self-contained improvement.

**No damage tracking.** Every frame redraws the whole grid. At 80×24 this is one
batched draw call and entirely adequate, but `Screen::revision()` and a per-row
dirty flag are the hooks for doing better on very large windows.

**`src/utils/retcodes.h` is unused.** 273 lines of error-code macros that nothing
includes. It is kept for now because a consistent error vocabulary is a
reasonable future direction, but it is currently dead weight.

**Geometry is not persisted.** Window size and position are read from config but
never written back.

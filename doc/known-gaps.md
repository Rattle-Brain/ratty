# Known gaps


Tracked in `todo-ratty.md`; listed here with the architectural context.

**No scrollback.** `Screen` holds exactly one viewport. The row indirection table
is the natural place to grow one: a ring buffer of rows with a view offset, with
`scrollUp` pushing the evicted row into history instead of clearing it. Until
then `wheelEvent` swallows scrolling and the scroll actions are bound but inert.

**No text selection.** `copySelection()` logs and returns. `Palette` already
carries a selection colour and `GLRenderer` has an overlay layer, so the missing
pieces are a selection range in the widget, mouse drag handling, and a
grid→string conversion that handles wide characters and trailing blanks.

**No mouse reporting.** Modes 1000–1006 are recognised and ignored, so
applications that probe for mouse support correctly conclude there is none.

**Grapheme clusters keep only their base code point.** `Cell` stores a single
`char32_t`, so combining marks and emoji continuations are consumed rather than
retained. Widths and cursor movement are correct
([grapheme clusters](terminal-emulation.md#grapheme-clusters-and-emoji-presentation)), but the exact sequence
is not recoverable — which will matter once text selection exists. The fix is a
side table of cluster extensions keyed by cell.

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

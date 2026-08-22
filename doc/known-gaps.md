# Known gaps


Tracked in `todo-ratty.md`; listed here with the architectural context.

**The Linux keybindings were transliterated, not designed.** The two default sets
[are asserted to resolve identically](keybindings.md#two-default-keybinding-files),
which is what has kept them from drifting — and is also why the Linux file is the
macOS file with `cmd` spelled `super`. On Linux that lands two bindings in the
wrong place: `ctrl+shift+c` closes a pane where nearly every other terminal
copies, `ctrl+shift+v` splits one where nearly every other terminal pastes, and
`super+<letter>` is the desktop's territory rather than the application's. Copy
and paste do work — on `super+c` and `super+v` — so this is a matter of habit
rather than capability, which is exactly why it wants a decision (does the
equivalence invariant survive?) rather than a patch. Top of `todo-ratty.md`.

**A selection copies the base code point of a grapheme cluster, not the cluster.**
The mechanism is the gap below: a cell holds one `char32_t`, so copying `👨‍💻`
yields the man rather than the sequence. Widths and cursor movement are right; it
is the exact bytes that are not recoverable.

**Scrollback search folds case for ASCII only, and matches literal text.** A
search for `Makefile` finds `makefile`, but `Straße` does not match `STRASSE` and
there is no regular-expression mode. Full Unicode case folding needs tables RaTTY
does not carry, and a terminal search is nearly always for a command, a path or an
identifier. A match also cannot span a hard line break — two lines are two lines,
however they came to be adjacent — which is a decision rather than a limitation.

**Reflow can lose lines that a narrower window pushed past the history limit.**
Rewrapping a 10 000-line history at half the width produces more rows than the
limit allows, and the oldest are evicted; widening again cannot bring them back.
Every terminal that reflows a line-bounded history has this property, and the
alternative — a byte-bounded history — makes `scrollback.lines` mean nothing a
user can predict.

**No pixel-resolution mouse reporting (`?1016`).** Reports are per cell, which is
all a text application needs; `?1016` exists for graphics protocols RaTTY does not
implement either.

**Grapheme clusters keep only their base code point.** `Cell` stores a single
`char32_t`, so combining marks and emoji continuations are consumed rather than
retained. Widths and cursor movement are correct
([grapheme clusters](terminal-emulation.md#grapheme-clusters-and-emoji-presentation)), but the exact sequence
is not recoverable — which now shows in what a selection copies, rather than being
theoretical. The fix is a side table of cluster extensions keyed by cell.

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

**A resize rewraps the whole scrollback synchronously.** About 10 ms for a full
10 000-line history, on the thread that is also drawing, and a splitter drag is
[not throttled](../src/ui/split_container.cpp), so a drag pays it per resize event.
A width change is the only trigger — a height change alone costs nothing — and
throttling the drag is the fix, not making the reflow lazier: a half-rewrapped
buffer is not a thing a `Screen` can be.

**No damage tracking.** Every frame redraws the whole grid. At 80×24 this is one
batched draw call and entirely adequate, but `Screen::revision()` and a per-row
dirty flag are the hooks for doing better on very large windows.

**`src/utils/retcodes.h` is unused.** 273 lines of error-code macros that nothing
includes. It is kept for now because a consistent error vocabulary is a
reasonable future direction, but it is currently dead weight.

**Geometry is not persisted.** Window size and position are read from config but
never written back.

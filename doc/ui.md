# Widgets, panes and the tab bar

## `SplitContainer`

A binary tree where each node is either a *leaf* holding one `TerminalWidget` or
a *container* holding a `QSplitter` with exactly two children.

```
        [Root: Horizontal]
             /        \
      [Terminal A]  [Vertical]
                     /      \
              [Terminal B] [Terminal C]
```

`splitHorizontal()`, `splitVertical()` and `closePane()` all **return the
resulting root**, because tree surgery can change which node the tab widget
should hold. The caller (`MainWindow::installTabRoot`) is then told rather than
having to infer it.

Two Qt ownership hazards live here, both of which bit the previous
implementation:

**Detaching before destroying.** When a pane closes, its sibling must leave the
doomed parent's `QSplitter` *before* that parent is destroyed. The old code set
only the logical `sibling->parent_ = nullptr` and then called
`parent_->deleteLater()` — while the sibling was still a Qt child of the parent's
splitter, so Qt destroyed the surviving pane along with it. `detachChild()` now
reparents to `nullptr` first.

**Showing after reattaching.** `QSplitter::insertWidget()` only auto-shows a
widget when the splitter itself is already visible, and a widget that has been
through a `QStackedWidget` — which every tab page has — comes back carrying
`WA_WState_ExplicitShowHide`, which suppresses the implicit show entirely. Both
reattachment points therefore call `show()` explicitly. This is verified by
`tests/test_splits.cpp`; the failure mode is a pane that silently vanishes.

### Which pane is current

Two different notions of "current pane" have to coexist here, and conflating them
was the cause of two separate complaints (a split that left the caret behind, and
a close that left it nowhere).

*Qt focus* is where a keystroke lands. It is not usable as a record of the user's
intent, for two reasons: reparenting a widget clears it, and **every** split and
close reparents most of the tree. Qt then hands focus to whatever sits first in
the new focus chain — which is not the pane anybody asked for.

*The marker* (`markFocused()`, `findMarkedPane()`, stored per pane in
`TerminalWidget::paneFocused_`) is RaTTY's own record, and it survives
reparenting because it is not Qt state.

So the two are split by responsibility. Tree surgery only ever moves the
**marker** — `performSplit()` marks the pane it created, `closePane()` marks a
survivor — and reports the new pane to its caller. `MainWindow` applies **Qt
focus** afterwards, once `installTabRoot()` has finished moving widgets around.
Doing it in the other order is exactly what silently lost the focus before.

`MainWindow::focusedPane()` reads them in that order too: Qt focus first, then
the marker, and only then "the first leaf" — so an action fires on the pane the
user is looking at even when Qt focus has just been dropped on the floor.

### The focus history

Closing a pane should hand the caret back to the pane the user came *from*. The
tree cannot answer that: all it knows is which node is adjacent, so
`closePane()` can only offer the promoted sibling's leftmost leaf.

`MainWindow` therefore keeps `focusHistory_`, panes in the order they last held
focus, most recent first, as `QPointer`s — a pane is destroyed by a close, a
shell exiting or a tab closing, and none of those routes through the list, so
dead entries are pruned on the way past rather than tracked. `closeFocusedPane()`
drops the doomed pane from the history first, so it cannot inherit from itself,
then `restoreFocusIn()` walks the list for the most recent survivor *in that
tree*, falling back to the marker and finally to the first leaf.

Only deliberate focus changes go in, or the history would be worthless:

- `giveFocusTo()` — the single path for RaTTY choosing a pane (a new split, a
  `focus_left`-style action, a tab switch, restoring after a close).
- `TerminalWidget::paneActivated`, emitted from `mousePressEvent` — the user
  clicking a pane.

Note that the click signal comes from the mouse handler rather than from
`focusInEvent`. A focus event cannot do the job: Qt sends one for its own
reparenting reassignments, and sends none at all until the window is *active*,
so the marker would go stale in an inactive window.

## `TabBar`

Qt's stock tab bar is a document-style control: tall, boxy, and styled by the
platform rather than by the terminal's theme. `TabBar` subclasses `QTabBar` and
replaces only the **drawing and the metrics**, keeping the model — page
association, ordering, drag-to-reorder, keyboard navigation, accessibility. That
is a much smaller surface than reimplementing tabs over a `QStackedWidget`, and
it does not quietly drop behaviour.

Three decisions are worth knowing:

- **The close affordance is painted, not a child widget.** `QTabBar`'s built-in
  one is a platform-styled button whose size fights a bar this thin. It is drawn
  only for the hovered and current tab; on every tab at once it reads as clutter.
  Space for it is reserved unconditionally in `tabSizeHint()`, so a label does
  not shift sideways when the pointer enters a tab. A drag that starts on the
  affordance is swallowed, so it cannot begin a reorder.
- **Metrics come from the terminal font.** The bar is one text line plus padding,
  and the label is drawn in the configured family at 85% size. That is most of
  what makes it look like part of the terminal rather than part of the window
  manager, and it means the bar scales with the font instead of being a fixed
  pixel count that looks wrong at either extreme.
- **The accent follows the edge that faces the terminal**, read from the bar's own
  `shape()`. `QTabWidget::setTabPosition` is the only thing `MainWindow` has to
  set; the bar works out the rest.

`QTabWidget::setTabBar()` is protected, so a replacement can only be installed
from a subclass. `TabWidget` exists for that one reason and adds nothing else.

#### Chrome colours

`ChromeColors` holds six optional colours and `resolve()` fills the gaps from the
terminal palette. Keeping them out of `Palette` matters: `Palette` is the
terminal's own colours, and an application's `OSC 4` request must not be able to
repaint the window chrome.

This is what lets a theme state only terminal colours and still get a coherent
tab bar: all ten shipped themes define no chrome at all.

The derivation is deliberately luminance-aware. `shift()` lightens a dark colour
and darkens a light one, because always going one way leaves a white bar on a
white terminal. The offset is large enough (45) that a filled active tab reads as
a distinct surface — at a smaller value the `blocks` style was
indistinguishable from `minimal`. The accent defaults to palette slot 12, the
bright blue, which every theme defines and which therefore tracks the theme
without being stated.

A label drawn on the accent picks whichever of the theme's two candidate colours
has more measured contrast against it, rather than switching on a fixed luminance
threshold. A mid-tone accent — Gruvbox Light's blue, for instance — sits close
enough to the middle that a threshold picks badly for some themes and well for
others.

## `MainWindow`

Tabs, shortcut dispatch and the window title. `handleAction()` is a single
`switch` over `Action`, and the tab bar auto-hides when there is only one tab so
a single-terminal window looks like a terminal.

Note that both `paneSessionEnded` and `paneTitleChanged` connect to *member
functions*: `Qt::UniqueConnection` is silently rejected for lambdas, and
`installTabRoot()` may reconnect the same root more than once.

#### Promoting a new root can remove the tab

This is the second half of the split bug, and the half that only appears inside a
`QTabWidget`.

When the pane being split *is* the tab's page, the tree surgery reparents it
under a new container — which takes it out of the tab widget's stacked layout.
`QTabWidget` watches for a page leaving and responds by **removing its tab**, so
the count legitimately drops to zero between the split and the moment the new
root is installed:

```
before:  tabCount=1   count()=1
--- Ctrl+Shift+W (split) ---
after:   tabCount=0   count()=0   currentIndex=-1   <- no page at all
```

`installTabRoot()` used to open with `if (index >= tabCount()) return;`, so it
returned without inserting anything and left the tab widget empty: a blank
window, with the shell still running behind it. It is now tolerant of the tab
having vanished, and only removes an occupant that has demonstrably been absorbed
into the new tree (`parentNode() != nullptr`) rather than assuming one is there.

Callers read the tab's label *before* the surgery, because afterwards there may be
no tab to read it from.

## Composed input: dead keys and input methods

`TerminalWidget` sets `Qt::WA_InputMethodEnabled` and implements
`inputMethodEvent()` / `inputMethodQuery()`. Without all three a terminal cannot
type a good part of the world's text, which is how it shipped:

A **dead key** produces no character of its own. On a Spanish keyboard `~` is
one — `Option+ñ` on macOS, `AltGr+ñ` on Linux — and so is every accent (acute,
then `a`, for `á`). The key event that arrives carries an empty `text()`; the
platform input method holds the half-finished composition and delivers the
result separately, as a `QInputMethodEvent`. It only does that for a widget that
has asked. Not asking meant the composition was silently discarded and the tilde
was unreachable on both platforms — as was any input method at all.

Three pieces, none optional:

- `WA_InputMethodEnabled` — without it the platform never routes composition to
  this widget, and the other two are never called.
- `inputMethodEvent()` — sends `commitString()` to the shell as UTF-8. The
  preedit string is the composition still in progress, which ought to be drawn
  under the cursor; RaTTY does not draw it yet (`todo-ratty.md`) but still
  *accepts* the event, because refusing it abandons the composition rather than
  letting it finish.
- `inputMethodQuery()` — answers `ImEnabled`, and `ImCursorRectangle` so a
  candidate window opens at the cursor instead of in a corner. That rectangle is
  the one conversion worth watching: the layout is in physical pixels and Qt
  asks in logical ones.

## The mouse

**A mouse event does not arrive at the pane directly.** The window's single
`TerminalCanvas` is a native child window stacked over the panes, so the platform
delivers to *it*; it hit-tests the widget underneath and forwards. A press
latches its target until the release, which is what keeps a `QSplitter` divider
draggable once the pointer leaves it. See
[one surface per window](rendering.md#one-surface-per-window).

Once the event lands, `TerminalWidget` decides, per event, whether the mouse
belongs to the application or to the terminal:

```
application asked for the mouse (?1000/?1002/?1003)
  and Shift is not held
  and the view is not scrolled back      -> report it (core/mouse.h)
otherwise
  left press / drag / release            -> select text
  wheel, primary screen                  -> move the scrollback view
  wheel, alternate screen                -> cursor keys, if ?1007 is on
  middle click                           -> paste the primary selection
```

Three of those conditions earn their place:

**Shift is the local override**, as it is in xterm. Without it, a user running a
mouse-driven TUI has no way to scroll the terminal's own scrollback or to paste.

**Scrolled back, nothing is reported.** The row under the pointer is then a
history row, and reporting its coordinates would tell the application about a
position on a screen it cannot see.

**Motion is reported per cell, not per pixel.** Mouse tracking is on for the
whole widget, so `mouseMoveEvent` fires on every pixel of a drag; only a change
of cell is worth a report, or a slow drag across one character floods the pty
with identical sequences.

The wheel accumulates fractional notches (`wheelRemainder_`). Trackpads and
high-resolution wheels send small `angleDelta()` values, so taking each event on
its own would either ignore them or scroll a whole notch per pixel.

Alternate-screen wheel handling is worth stating plainly: a full-screen
application repaints instead of scrolling, so there is no scrollback to move and
the wheel would simply do nothing — which reads as a broken terminal in `less`.
Translating a notch into cursor keys is what every other terminal does, and
`DECRST 1007` lets an application opt out.

---

## Selection

The model is [`core/selection.h`](../src/core/selection.h) and knows nothing about
the mouse: everything difficult about a terminal selection is a question about the
*buffer* — where a word ends, how a multi-row selection follows the text round the
end of a row, whether two rows are one wrapped line, what happens to the padding
between the last character and the window edge — and all of it is far easier to
pin down headlessly than by dragging across a window. `tests/test_selection.cpp`
does exactly that; `tests/test_select_gl.cpp` then drives the real thing with real
mouse events and reads the result back off the clipboard.

The widget's half is the state machine over it:

| Gesture | Mode |
|---|---|
| drag | character |
| double-click | word |
| triple-click | whole logical line, wrapped rows included |
| `Alt`+drag | rectangle |
| click without moving | clears the selection |

Points worth knowing:

**Clicks are counted here, not by Qt.** Qt reports a double click as its own event
type but has no notion of a third, and a terminal needs one. `countClick()` counts
against the platform's own double-click interval and the cell the previous click
landed on, and cycles 1 → 2 → 3 → 1, so a fourth click is a plain click again.
`mouseDoubleClickEvent()` routes into the same place as a press.

**`Alt` is the rectangular modifier**, not `Ctrl` or `Shift`. Shift is spoken for:
it is what bypasses an application's mouse grab, so it is held for most selections
made *inside* a TUI and cannot also mean "rectangle". `Ctrl`+click is the context
menu on macOS. That is also why Shift+click does not extend a selection yet.

**A word is generous about punctuation.** Letters and digits, anything outside
ASCII that is not a space, and the characters that appear *inside* what a terminal
user double-clicks: `/ . - _ ~ : @ + = % # & ? * $ \ ^`. What is left out is what
ends a word at a shell: quotes, brackets, the pipe, the comma, the semicolon. So
`/usr/local/bin/thing` and `--flag=on` are each one word, and a URL comes out
whole. A double-click on a blank selects the run of blanks, so the gesture never
does nothing.

**A selection is held in [stable line numbers](terminal-emulation.md#stable-line-numbers)**,
not view rows, so it stays on its text while the view scrolls and while output
arrives. It is dropped on a keystroke that reaches the shell (the text is about to
move), on a width change (reflow re-cuts the rows), when the scrollback is
cleared, and when the alternate screen goes up or comes down — the two screens
number their lines independently, so a selection made on one could otherwise
highlight unrelated text on the other.

**Copying** turns the range into text with the trailer half of a double-width
character skipped, padding to the window edge dropped, and rows joined across a
wrap seam — so a wrapped command line comes back as the one line a shell will
accept. A block selection never joins rows; taking a column out of a table is the
whole point of it.

**Where it goes.** The primary selection is set by the act of selecting, wherever
the platform has one, because that is what middle-click pastes. The clipboard is
only ever written deliberately — by `copy`, or by `clipboard.copy_on_select` — on
the grounds that it holds something the user meant to keep.

### Why the selection is painted in the *background* layer

The obvious place for a highlight is the overlay layer, above the glyphs. It is
the wrong one. A theme states `selection_background` as an opaque background
colour, and an opaque quad drawn over the text hides the text it is highlighting;
a translucent one veils it.

Painted into the background layer *after* the grid's own cell fills, the selection
covers those fills and the glyphs — a layer above — are drawn on top of it at full
contrast. That is the same result every other terminal gets by repainting the
selected text, at the cost of one quad per row instead of a second pass over the
cells. Search highlights do use the overlay, because they are a different thing: a
tint that marks text without replacing its background, so several matches on
screen at once stay readable.

## The search prompt

`cmd+f` / `super+f` opens an incremental search over the scrollback. It is drawn
by `TerminalRenderer`, not built from widgets, and it **takes the bottom row of
the grid** rather than resizing it.

Both halves of that are deliberate. There is nowhere to put a widget: the pane is
covered by the shared `TerminalCanvas`, a native child window, so anything laid
out over the pane would be hidden by the very surface the terminal draws on. And
the grid is not resized because the shell must not see the window change size
because someone opened a search box — `SIGWINCH` in the middle of a search would
be a surprising thing to send.

```
   340        <- "34" tinted: an ordinary match
   341
   349        <- "34" brighter: the current match, and the selection
   350
/34     14/14 <- the prompt, over the bottom row of the grid
```

- typing refines the query; `Backspace` and `Ctrl+U` edit it
- the newest match is selected first, on the grounds that what is being looked
  for in a scrollback is usually what happened most recently
- `Return` steps back through the buffer and `Shift+Return` forward again, which
  is the direction a scrollback search goes; the arrow keys do the same
- `Escape` closes the prompt and leaves the current match **selected**, so it can
  be copied
- the query survives closing, so `find_next` resumes it

While the prompt is open it owns the keyboard — but only after the keybindings
have had first refusal, so a shortcut still works mid-search. The current match is
also the selection, which is what makes it copyable and is why the two highlight
styles have to be distinguishable.

## The scroll-position indicator

A slim thumb down the right edge while the view is scrolled back, sized and
positioned as the view's share of the whole buffer. It is not a scrollbar: there
is nothing to drag, and the point is only to answer the question a static screen
of text raises, which is whether what is on it is still live. `scrollback.indicator`
turns it off.

---

## `InputHandler`

Qt key events to VT bytes, with xterm's modifier encoding (`CSI 1 ; mod A`), so
Shift+Arrow and Ctrl+Arrow are distinguishable from the bare key. Cursor keys
switch to the SS3 form (`ESC O A`) when the application has set `DECCKM`.

Covered by `tests/test_input.cpp`, including the check that shell control keys
(Ctrl+C/D/W/R/Z/L/A/E/U and Tab) are **not** bound to application shortcuts.

---


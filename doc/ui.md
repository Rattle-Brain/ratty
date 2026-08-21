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

`TerminalWidget` decides, per event, whether the mouse belongs to the
application or to the terminal:

```
application asked for the mouse (?1000/?1002/?1003)
  and Shift is not held
  and the view is not scrolled back      -> report it (core/mouse.h)
otherwise
  wheel, primary screen                  -> move the scrollback view
  wheel, alternate screen                -> cursor keys, if ?1007 is on
  middle click                           -> paste
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

## `InputHandler`

Qt key events to VT bytes, with xterm's modifier encoding (`CSI 1 ; mod A`), so
Shift+Arrow and Ctrl+Arrow are distinguishable from the bare key. Cursor keys
switch to the SS3 form (`ESC O A`) when the application has set `DECCKM`.

Covered by `tests/test_input.cpp`, including the check that shell control keys
(Ctrl+C/D/W/R/Z/L/A/E/U and Tab) are **not** bound to application shortcuts.

---


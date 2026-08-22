# Terminal emulation


## `Cell`, `Color` and `Pen`

A cell is 16 bytes and trivially copyable:

```cpp
struct Cell {
    char32_t ch;      // one code point
    Color    fg, bg;  // symbolic, 4 bytes each
    uint16_t flags;   // bold, italic, underline, inverse, …
};
```

`Color` is a tagged 4-byte value:

| Kind | Meaning | Set by |
|---|---|---|
| `Default` | whatever the palette calls default | SGR 39 / 49, initial state |
| `Indexed` | one of 256 palette slots | SGR 30–37, 40–47, 90–97, 100–107, 38;5;N, 48;5;N |
| `Rgb` | a literal 24-bit colour | SGR 38;2;R;G;B, 48;2;R;G;B |

Storing "default" symbolically rather than resolving it at parse time is what
lets a theme change repaint correctly without rewriting the grid, and is what
makes `bg != defaultBackground` a meaningful test in the renderer.

The `Pen` is the current graphic rendition — the colours and flags that newly
printed characters inherit. SGR sequences mutate the pen; they never touch the
grid.

Two of the flags are *structural* rather than renditions, and nothing draws
either of them:

| Flag | On which cell | Means |
|---|---|---|
| `CellFlagWideTrailer` | the second column of a double-width character | placeholder: occupies a column, carries no glyph of its own |
| `CellFlagWrapped` | the cell in the **last** column of a row | the text ran into the right margin and continues on the next row |

`CellFlagWrapped` is the seam, and it is what tells a soft wrap from a hard line
break apart. Without it a resize cannot rewrap old text, a search cannot match
across the join, and copying a command line that wrapped yields two lines no
shell will accept. It lives in a cell rather than in a per-row table so that it
travels with the row for free: the scrollback encoding already carries flags,
resize already copies cells, and scrolling rotates row *indices* rather than
content. Anything that rewrites or erases that cell clears it, which is the right
answer — erasing the end of a line does break the wrap.

## `Screen`

Pure terminal state: no parsing, no Qt widgets, no rendering.

Rows live in a flat `std::vector<Cell>` addressed through an indirection table
(`rowMap_`). Scrolling rotates row *indices* rather than copying cell data, so
`scrollUp`, `insertLines` and `deleteLines` are index permutations:

```cpp
void Screen::scrollUp(int count, const Pen& pen) {
    auto first = rowMap_.begin() + scrollTop_;
    auto last  = rowMap_.begin() + scrollBottom_ + 1;
    std::rotate(first, first + n, last);
    for (int r = scrollBottom_ - n + 1; r <= scrollBottom_; ++r) clearRow(r, pen);
}
```

Three behaviours in here are load-bearing:

**Deferred wrap.** When a character lands in the last column the cursor stays
put and only `pendingWrap_` is set. The line break happens when the *next*
printable character arrives. Any explicit cursor movement — including `CR` —
clears the flag without wrapping. This is required by the VT specification and
relied upon by every shell prompt; see [the white block after every Enter](notable-bugs.md#the-white-block-after-every-enter).

**Erase keeps the background.** `Cell::erase(pen)` retains the pen's background
colour but drops other rendition. That is how TUI applications paint full-width
coloured bars with a single `EL` after setting a background.

**Scrolling region.** `scrollTop_`/`scrollBottom_` bound every scroll, insert and
delete, so `DECSTBM` works and full-screen applications can scroll a subrange.

### Scrollback

A row leaving the top of the screen is copied into `history_`, a
`std::deque<HistoryLine>` bounded by `historyLimit_`, and `viewOffset_`
says how many rows the display is scrolled back. `viewAt(row, col)` reads from
`history + viewport`; `at(row, col)` is unchanged and still means the live grid,
which is what the emulator writes to whether or not the user is looking at
history.

**History rows are stored compressed.** Held as raw cells a row costs
`cols × sizeof(Cell)` — 3200 bytes at 200 columns — so the default 10 000-line
history came to about 32 MiB *per pane*, plus a heap block each. Eight panes of a
working day was a quarter of a gigabyte of mostly blanks, and because every row
was its own allocation, closing the tab did not give it back to the OS.

`HistoryLine` (`core/history.h`) exploits two things about terminal output:

- **Most of a row is trailing blank.** A shell line is rarely as wide as the
  window. Those cells are dropped, which is free: `Screen` already reports
  columns past a stored row's width as blank, so a *default* blank is
  indistinguishable from "not stored". Trailing cells carrying a non-default
  background — the coloured bars a TUI draws — are not default blanks, and
  survive. Neither is the seam of a wrapped row, which is how a soft line break
  gets through the scrollback intact.
- **Colours change far more slowly than characters do.** Almost every row is one
  single run of attributes, so they are run-length encoded while characters are
  kept per cell, narrowed to the smallest fixed width that covers the row: one
  byte for Latin-1, two for the BMP, four otherwise.

Both halves live in one heap block, so a row still costs exactly one allocation —
which is what lets `pushHistory()` keep recycling buffers instead of calling the
allocator once per scrolled line. Measured, 10 000 rows at 200 columns:
**39.5 MiB → 2.4 MiB**, with the worst case (a different colour in every column)
still no larger than the raw cells.

The encoding is lossless, and `test_history` pins that for every kind of cell a
terminal can produce: truecolour, indexed colour, every rendition flag, CJK,
astral-plane emoji, a NUL code point, and a full-width coloured bar of blanks.

One consequence for callers: **`viewAt()` and `viewRow()` may return a pointer
into a decode buffer** that the next call to either is free to overwrite. Read
before calling again. Every caller already does — the renderer walks one row to
completion before asking for the next, which is the access pattern this is built
for — and the reference-returning signature always implied as much.

Three decisions are worth stating:

**Only the whole screen feeds the history.** `scrollUp` captures rows only when
the scrolling region is the full screen. A `DECSTBM` region is a subwindow the
application scrolls itself — vim's text area, htop's process list — and capturing
it would fill the scrollback with the same screen redrawn hundreds of times.

**The alternate screen keeps none at all.** `TerminalEmulator` sets its history
limit to zero. A full-screen application repaints rather than scrolls, so there
is nothing there worth keeping, and mixing it into the shell's history is how
other terminals end up with `vim` sessions embedded in their scrollback.

**A width change rewraps everything.** Rows dropped off the top by a vertical
shrink are pushed into the history, since from the user's point of view they
scrolled away — and a change of *width* triggers `Screen::reflow()`, which is
described in its own section below. A height-only change takes the cheap path and
does none of that work.

`ED 3` erases the saved lines and nothing else, which is xterm's definition;
applications that want both send `ED 2` first (`tput clear` is
`CSI H CSI 2 J CSI 3 J`). `RIS` discards the history along with everything else.

The view snaps back to the live screen on any output (`TerminalEmulator::write`)
and on any keystroke (`TerminalWidget::keyPressEvent`). Both are necessary: text
arriving out of sight, or an echo of what was just typed landing off-screen,
reads as a hung terminal.

### Reflow

A resize that changes the **width** rebuilds the buffer. `Screen::reflow()` walks
history and live screen in order, joins consecutive rows wherever the seam says
they are one logical line, and lays each logical line out again at the new width:

```
80 columns                        40 columns
  echo aaaa…aaaa bbbb…bbbb ⏎ (seam)  echo aaaa…aaaa ⏎ (seam)
  cccc                               bbbb…bbbb cccc
```

Four things about it are deliberate:

**Only real seams are joined.** A row that ended in a newline stays a line of its
own, whatever its length. That is the whole reason the seam is tracked: rewrapping
from the stored cells alone would run a table drawn by a TUI together into a
paragraph, and the result would be unreadable rather than merely reformatted.

**It streams.** Rows come out of the history in order, each finished logical line
is rewrapped and re-encoded straight away, and only the last screenful is expanded
back into cells at the end. The peak cost is one logical line of scratch space
rather than a second uncompressed copy of the scrollback — which at 10 000 rows
would be tens of megabytes. Measured, a full 10 000-line history reflows in about
10 ms; an unchanged width costs nothing at all.

**The cursor is tracked by its offset within its logical line**, because that is
the only thing about it a rewrap leaves alone — which row and column it lands on
is exactly what changes. The live screen is then the last `rows` rows of the
result, so a prompt at the bottom stays at the bottom, unless rewrapping pushed
the cursor above that window, in which case the window follows the cursor rather
than scrolling it out of sight.

**The alternate screen is excluded.** `TerminalEmulator` clears its reflow flag,
for the same reason it gives it no history: what is on it is a full-screen
application's own layout, drawn for the size the application was told about.
Joining htop's rows into paragraphs would be nonsense; the application redraws
instead.

A double-width character is never split across the new margin — the column it
cannot fit in is left blank, exactly as `Screen::print()` does when it meets the
same case.

### Stable line numbers

A selection anchor and a search result have to name a piece of text for longer
than an instant, and neither a view row nor an absolute row index can:

| Coordinate | Breaks when |
|---|---|
| view row | anything scrolls — the text moves up the screen |
| absolute row (`history[0]` = oldest kept) | the history evicts its oldest line, shifting every index |
| **stable line number** | only when the text itself is dropped |

A stable line number counts from the first line the screen ever captured, so
`discardedLines_` advances on every eviction and the numbering does not. The
accessors are `firstLine()`, `screenTopLine()`, `viewTopLine()`, `lastLine()` and
`lineData(line, length)`; `viewAt()` and `viewRow()` are now thin wrappers over
`lineData()`, which is the single place the history/live split is resolved.

Two consequences worth knowing:

- A number naming text that has since been dropped resolves to `nullptr` rather
  than to whatever took its place. Callers get nothing, which is the honest
  answer.
- A **reflow renumbers past the end of the old buffer**. Every number issued
  before the resize named text at the old width, and that text has just been
  re-cut into different rows; advancing the origin means such a number cannot
  quietly come to mean a line it never meant. `TerminalWidget` drops its selection
  on a width change and re-runs an open search for exactly that reason.

### Searching the scrollback

`searchScrollback()` ([`core/search.h`](../src/core/search.h)) walks the buffer one
*logical* line at a time — the same joining reflow does — and returns matches as
`SelectionRange`s, so the code that draws and copies a selection draws and copies
a match with no special case.

Per logical line it builds the text and, alongside it, the position each character
occupies. The two are separate because a double-width character is one character
in the text and two columns on the screen, and a match has to be reported in
columns: that is what makes a highlight cover both halves of a CJK character and
what lets a CJK needle match at all.

Case folding is ASCII-only and matching is literal — see
[known gaps](known-gaps.md). The match count is capped, and a search that hits the
cap says so (`SearchResults::truncated`, shown as a `+` in the prompt) rather than
reporting a total it does not have.

### Mouse reporting

Two independent settings, which is why they are two enums in
[`core/mouse.h`](../src/core/mouse.h):

| Mode | Meaning |
|---|---|
| `?9` | X10 compatibility: presses only, no modifiers, no releases |
| `?1000` | presses and releases |
| `?1002` | ... and motion while a button is held |
| `?1003` | ... and all motion |
| `?1004` | focus in / focus out (`CSI I` / `CSI O`) |
| `?1005` | UTF-8 coordinates |
| `?1006` | SGR coordinates — what modern applications ask for |
| `?1015` | urxvt coordinates |
| `?1007` | alternate scroll: the wheel drives a pager through cursor keys |

`encodeMouseReport()` turns one event into bytes, or into an empty string when
the event is not reportable in the active mode — a release under `?9`, motion
nobody asked for, a wheel "release" that does not exist. Callers can therefore
send the result unconditionally instead of re-deriving the rules.

Disabling a mode is deliberately conditional: applications enable `?1002` and
`?1003` together and then reset them one at a time, so treating any `l` as "off"
would stop reporting while the application still expects it.

## `VTParser`

A state machine modelled on Paul Williams' DEC parser, consuming `char32_t`
rather than bytes (UTF-8 decoding happens upstream; all escape syntax is ASCII,
so this costs nothing and makes multi-byte text fall out for free).

```
Ground ──ESC──► Escape ──'['──► CsiEntry ──params──► CsiParam ──final──► dispatch
   ▲               │                 │                   │
   │               ├──']'──► OscString ──BEL / ESC '\'──► dispatch
   │               ├──'P','X','^','_'──► StringIgnore
   │               └──intermediate──► EscapeIntermediate ──final──► dispatch
   └───────────────────────── printable / C0 ─────────────────────────
```

Parameters are stored flat, with omitted parameters preserved as
`CsiSequence::Omitted` so a handler can apply the correct per-command default.
Sub-parameters (the colon form in `SGR 38:2:r:g:b`) are *flagged* rather than
flattened, so both spellings work.

Points where the previous parser produced visible garbage, and what changed:

| Input | Old behaviour | Now |
|---|---|---|
| `ESC ] 7 ; … ESC \` | left the OSC state on the `ESC`, then printed the `\` into the grid | `ESC \` consumed as one ST |
| `ESC [ > 4 ; 2 m` | `>` treated as a final byte; `4;2m` printed as text | private marker recognised and ignored |
| `ESC [ ! p`, `ESC [ 2 SP q` | intermediate byte treated as final; remainder printed | intermediate bytes recognised |
| `ESC [ 3 8 ; 5 ; 2 0 8 m` | each parameter matched separately, so the colour was dropped | parsed as one extended-colour spec |
| 20-digit parameter | signed overflow | clamped |

## `TerminalEmulator`

Implements `VTHandler` and supplies all the semantics. It owns the pen, a primary
and an alternate `Screen`, and the DEC mode flags.

Supported sequences:

| Category | Sequences |
|---|---|
| C0 | BEL, BS, HT, LF, VT, FF, CR (SO/SI accepted, ignored) |
| Cursor | CUU/CUD/CUF/CUB (`A`–`D`), CNL/CPL (`E`,`F`), CHA/HPA (`G`,`` ` ``), VPA (`d`), CUP/HVP (`H`,`f`), CHT/CBT (`I`,`Z`), VPR/HPR (`e`,`a`) |
| Erase | ED (`J`) modes 0–3 (mode 3 erases the *saved lines* only, as in xterm), EL (`K`) modes 0–2, ECH (`X`) |
| Edit | ICH (`@`), DCH (`P`), IL (`L`), DL (`M`) |
| Scroll | SU (`S`), SD (`T`), DECSTBM (`r`) |
| Rendition | SGR (`m`): 0–9, 21–29, 30–37, 38, 39, 40–47, 48, 49, 90–97, 100–107, both `;` and `:` extended forms |
| Modes | DECCKM (?1), DECAWM (?7), DECTCEM (?25), alternate buffer (?1047/?1048/?1049), bracketed paste (?2004), LNM (20) |
| Mouse | X10 (?9), click (?1000), drag (?1002), any-motion (?1003), focus events (?1004), UTF-8/SGR/urxvt coordinates (?1005/?1006/?1015), alternate scroll (?1007) |
| Cursor shape | DECSCUSR (`CSI n SP q`) |
| Reports | DSR 5, DSR 6 (CPR), DA1 |
| ESC | IND, NEL, RI, DECSC/DECRC (`7`/`8`), RIS (`c`), charset selection (accepted, ignored) |
| OSC | 0/2 (window title), 4 and 104 (palette entries), 10/11/12 and 110/111/112 (default fg, bg, cursor) — all of them settable *and* queryable — and 52 (clipboard); others parsed and dropped |

Replies (`DSR`, `DA1`) go out through a `ReplySink` callback that
`TerminalSession` wires back to the pty. Title changes and the bell use the same
callback pattern. One `std::function` per genuinely distinct concern, rather than
the twelve action callbacks the emulator used to register.

`LF` deliberately does **not** imply a carriage return unless `LNM` is set. The
old code folded CR into LF unconditionally, which hid missing-CR bugs and broke
plain index movement.

## Grapheme clusters and emoji presentation

A terminal receives a grapheme cluster one code point at a time, and it is the
*whole* sequence that says how wide the cell is and whether it holds a colour
emoji. `TerminalEmulator::continueCluster()` decides, for each incoming code
point, whether it starts a new cell or retrofits the previous one.

Two things make this necessary.

**Dual-form code points.** U+26A0 is a narrow monochrome warning sign; U+26A0
followed by U+FE0F is a double-width colour emoji; followed by U+FE0E it is
forced back to text. The selector arrives *after* the character has already been
placed, so `Screen::adjustLastCell()` exists to widen or narrow a cell after the
fact, moving the cursor and the wide-trailer with it. Selectors used to be
dropped as zero-width marks, which made the two forms indistinguishable.

A selector is only honoured on an Extended_Pictographic base
(`isExtendedPictographic`), so a stray U+FE0F after a letter cannot widen it into
two columns.

**Multi-code-point sequences.** These are all one cluster and one double-width
cell:

| Sequence | Example |
|---|---|
| zero-width joiner | `U+1F468 U+200D U+1F4BB` — man technologist |
| skin-tone modifier | `U+1F44D U+1F3FD` |
| regional indicator pair | `U+1F1EA U+1F1F8` — a flag |
| keycap | `U+0031 U+FE0F U+20E3` |
| tag sequence | `U+1F3F4` + six tag characters — a subdivision flag |

Printing one cell per code point made a joined emoji sprawl across four or eight
columns and left the cursor in the wrong place. A control character or any cursor
movement ends the cluster, since one cannot span either.

The cell keeps only its *base* code point; the continuations are consumed. That
is a deliberate limit: rendering `👨‍💻` as its single combined glyph requires
GSUB ligature substitution — text shaping — and FreeType alone cannot do it. The
base emoji is drawn instead, in the right number of columns. See
[known gaps](known-gaps.md).

## Colours are owned per session

`TerminalEmulator` holds two palettes: `basePalette_`, seeded from `Config` when
the session starts, and `palette_`, the live one. `OSC 4/10/11/12` mutate the
live palette; `OSC 104/110/111/112` restore individual entries from the base.

Ownership matters here. The palette deliberately does **not** live in `Config`,
because these sequences let a running application retheme *its own* terminal —
one pane changing its background must not disturb another. `TerminalWidget`
therefore reads `session_->palette()`, not `Config::instance().palette()`, both
for the grid and for the frame's clear colour.

Because cells store a palette *index* rather than a resolved colour
([`Cell`](#cell-color-and-pen)), an `OSC 4` arriving after text is already on
screen recolours that text on the next repaint. Tools like `base16-shell` depend
on exactly that.

Queries are the other half. Neovim sends `OSC 11 ; ?` at start-up to discover
whether the terminal is light or dark, and with no answer it has to guess — which
gets a light colour scheme wrong. Replies use the X11 `rgb:rrrr/gggg/bbbb` form
that xterm uses, and `parseColorSpec()` accepts `#rgb`, `#rrggbb`,
`#rrrgggbbb`, `#rrrrggggbbbb`, `rgb:r/g/b` with 1–4 hex digits per component,
and colour names.

`DECSCUSR` (`CSI n SP q`) is handled alongside, because editors use it to signal
their mode — a bar while inserting, a block otherwise. The request wins over the
user's configured `cursor.style` while it is in effect; `CSI 0 SP q` hands
control back. Note that the space *intermediate* is what identifies the
sequence: `CSI 5 q` without it is something else entirely.

## `OSC 52`: the clipboard

```
OSC 52 ; Pc ; <base64>   put text on the selection Pc names
OSC 52 ; Pc ; ?          ask what is on it
```

`Pc` is a selection name — `c` for the clipboard, `p` or `s` for the primary
selection; empty means the clipboard. Only the first name is acted on, as in every
terminal that implements this: the alternative is one escape sequence scattering
text across several selections.

This is how a program on the far side of an ssh connection copies to the *local*
clipboard, and the reason `tmux save-buffer` and an editor's yank can reach it at
all. Base64 lives in [`core/base64.h`](../src/core/base64.h) — the only place
RaTTY needs it — and decoding is tolerant of missing padding and embedded
newlines, because real senders differ, but refuses anything that is not base64
rather than pasting rubbish.

**Both directions are sinks, and both are policy.** `TerminalEmulator` holds a
writer and a reader callback and does nothing at all when one is not installed —
which is precisely how a terminal without OSC 52 support behaves. `TerminalSession`
turns them into Qt-facing handlers and `TerminalWidget` installs only the ones the
configuration allows:

| Direction | Default | Why |
|---|---|---|
| write (`clipboard.osc52_write`) | on | the useful case; the worst it can do is replace what is on the clipboard |
| read (`clipboard.osc52_read`) | **off** | anything that can write to the terminal could otherwise exfiltrate whatever was last copied, passwords included |

The reader is a `bool(char, std::string&)`: returning false declines, and an
unanswered query is indistinguishable from a terminal that never supported the
sequence, which is what a well-behaved application already handles.

One parser detail follows from this: the OSC string bound is 64 KiB rather than
the 4 KiB a title needs, because an OSC 52 payload is a whole selection. Past the
bound the tail is dropped, the base64 fails to decode, and the request is
ignored — the right outcome for a clipboard that did not arrive whole.

## `TerminalSession`

Everything between the pty file descriptor and the grid, with no rendering and no
widget code: the pty, the `QSocketNotifier`, the emulator and the byte pump.
Extracting it is what let `TerminalWidget` shrink to a view.

`drainPty()` reads in a bounded loop — up to 32 reads of 64 KiB — rather than one
read per notifier activation. A command producing megabytes of output otherwise
costs one event-loop round trip and one repaint per 4 KiB. The bound stops a
runaway producer from starving the UI.

Paste goes through `sendPaste()`, which translates `LF` to `CR` (Enter delivers
CR) and wraps the payload in `ESC[200~` / `ESC[201~` when the application has
enabled bracketed paste.

## `PTY`

RAII wrapper around `forkpty`. Things it now gets right that it did not before:

- **`TERM` is set** (`xterm-256color`, plus `COLORTERM=truecolor`). Nothing set
  it before, so behaviour depended on the launching environment — a shell started
  from Finder or a `.desktop` file saw no `TERM` and fell back to `dumb`, with no
  colour and no cursor movement at all.
- **The shell is a login shell** (`argv[0]` prefixed with `-`), matching
  Terminal.app and kitty. Without it `~/.zprofile` never runs and `PATH` is
  missing Homebrew.
- **`LINES`/`COLUMNS` are unset** in the child so the pty's `winsize` is the only
  authority, and signal dispositions are reset so the shell starts clean.
- **Read outcomes are distinguished.** `ReadResult` separates data, `EAGAIN`,
  end-of-file and real errors. The old `ssize_t` return conflated "no data right
  now" with "the shell exited"; `EIO`, which is how a pty master reports a
  departed slave on Linux and the BSDs, was treated as a failure.
- **`hasChildExited()` is idempotent.** It used to call `waitpid` from a `const`
  method on every poll, so the first call consumed the exit status and the
  destructor could no longer reap.
- **`resize()` no longer sends `SIGWINCH` by hand.** `TIOCSWINSZ` already signals
  the slave's foreground process group; the manual `kill` targeted the shell
  rather than the foreground job, which is wrong under job control.

---


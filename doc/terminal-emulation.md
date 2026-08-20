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
| Erase | ED (`J`) modes 0–3, EL (`K`) modes 0–2, ECH (`X`) |
| Edit | ICH (`@`), DCH (`P`), IL (`L`), DL (`M`) |
| Scroll | SU (`S`), SD (`T`), DECSTBM (`r`) |
| Rendition | SGR (`m`): 0–9, 21–29, 30–37, 38, 39, 40–47, 48, 49, 90–97, 100–107, both `;` and `:` extended forms |
| Modes | DECCKM (?1), DECAWM (?7), DECTCEM (?25), alternate buffer (?1047/?1048/?1049), bracketed paste (?2004), LNM (20) |
| Cursor shape | DECSCUSR (`CSI n SP q`) |
| Reports | DSR 5, DSR 6 (CPR), DA1 |
| ESC | IND, NEL, RI, DECSC/DECRC (`7`/`8`), RIS (`c`), charset selection (accepted, ignored) |
| OSC | 0/2 (window title), 4 and 104 (palette entries), 10/11/12 and 110/111/112 (default fg, bg, cursor) — all of them settable *and* queryable; others parsed and dropped |

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


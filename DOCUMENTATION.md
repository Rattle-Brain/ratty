# RaTTY — Architecture and Internals

Version 0.2.0

This document describes how RaTTY is put together, why the pieces are separated
the way they are, and where the current limits lie. It is written for someone
about to change the code.

---

## Table of contents

1. [Design principles](#1-design-principles)
2. [Layered architecture](#2-layered-architecture)
3. [Data flow: bytes to pixels](#3-data-flow-bytes-to-pixels)
4. [The core layer](#4-the-core-layer)
5. [The render layer](#5-the-render-layer)
6. [The UI layer](#6-the-ui-layer)
7. [Configuration](#7-configuration)
8. [Two bugs worth understanding](#8-two-bugs-worth-understanding)
9. [Building, running and testing](#9-building-running-and-testing)
10. [Known gaps](#10-known-gaps)

---

## 1. Design principles

Four rules shape the code. Most of the structure follows from them.

**The core knows nothing about pixels.** Everything under `src/core/` models a
terminal: a grid of cells, a cursor, an escape-sequence parser. It does not
include a single OpenGL or QtWidgets header. That is why the terminal test suite
runs in a fraction of a second with no GPU, no window and no shell.

**Syntax and semantics are separate.** `VTParser` recognises the *shape* of an
escape sequence and nothing else. `TerminalEmulator` decides what a recognised
sequence *means*. The parser holds no terminal state; earlier revisions had it
tracking the current text attributes, which meant terminal state lived in two
places and drifted.

**One source of truth per fact.** The default foreground colour is defined in
exactly one place (`Palette`). Cells store *symbolic* colours — "default",
"palette slot 208", "this RGB triple" — and only the palette turns them into
pixels. When three separate classes each hard-coded their own idea of "default
background", a custom background in the config made every cell paint an
opaque rectangle in the old colour.

**Physical pixels everywhere in the renderer.** No part of the drawing code
works in logical/device-independent units. This is not stylistic: mixing the two
is exactly what made text blurry (see [§8](#8-two-bugs-worth-understanding)).

---

## 2. Layered architecture

```
                     ┌──────────────────────────────────┐
   src/ui/           │ MainWindow                       │  tabs, shortcuts
                     │   └─ SplitContainer (tree)       │  pane layout
                     │        └─ TerminalWidget         │  QOpenGLWidget
                     └───────────┬──────────┬───────────┘
                                 │          │
                 ┌───────────────┘          └──────────────┐
                 ▼                                         ▼
   src/render/  ┌──────────────────────────┐   src/core/  ┌────────────────────┐
                │ TerminalRenderer         │              │ TerminalSession    │
                │   grid → draw calls      │              │   pty + I/O pump   │
                ├──────────────────────────┤              ├────────────────────┤
                │ GLRenderer               │              │ TerminalEmulator   │
                │   batching, layers       │              │   VT semantics     │
                ├─────────────┬────────────┤              ├─────────┬──────────┤
                │ GlyphAtlas  │FontManager │              │ Screen  │VTParser  │
                │  texture    │ FreeType   │              │  grid   │ syntax   │
                └─────────────┴────────────┘              ├─────────┴──────────┤
                                                          │ PTY  Palette  Cell │
   src/config/  ┌──────────────────────────┐              └────────────────────┘
                │ Config (singleton)       │
                └──────────────────────────┘
```

### Dependency rules

| Layer     | May depend on                         | Must not depend on          |
|-----------|---------------------------------------|-----------------------------|
| `core/`   | Qt Core/Gui (`QColor`, `QObject`), libc | OpenGL, QtWidgets, `render/` |
| `render/` | `core/`, OpenGL, FreeType, Qt Gui     | `ui/`, `config/`            |
| `ui/`     | everything                            | —                           |
| `config/` | `core/`                               | `render/`, `ui/`            |

Two of these are worth calling out because they were violated before and are
easy to violate again:

- `config/` must not include `render/`. `CursorStyle` therefore lives in
  `core/cursor.h`, so that a setting can be expressed without dragging the whole
  OpenGL stack into the settings parser.
- `render/` must not read `Config`. The renderer is handed a `Palette`,
  `FontMetrics` and a `Layout`; it does not look settings up for itself. That
  keeps it testable and makes the data flow one-directional.

### File map

| File | Responsibility |
|---|---|
| `core/cell.h` | `Cell`, `Color`, `Pen`, rendition flags. 16-byte POD, no Qt. |
| `core/palette.h/.cpp` | The 256-colour palette and the default fg/bg/cursor. Resolves symbolic colours. |
| `core/screen.h/.cpp` | The grid, cursor, pending-wrap flag, scrolling region, editing operations. |
| `core/vt_parser.h/.cpp` | ECMA-48 state machine. Emits parsed sequences to a `VTHandler`. |
| `core/terminal_emulator.h/.cpp` | Implements `VTHandler`; owns the pen, the primary and alternate screens, and DEC modes. |
| `core/terminal_session.h/.cpp` | Owns the pty, the socket notifier and the byte pump. Emits Qt signals. |
| `core/pty.h/.cpp` | `forkpty` wrapper: shell lookup, environment, resize, teardown. |
| `core/utf8.h` | Incremental UTF-8 decoder (survives chunk boundaries) and encoder. |
| `core/unicode.h` | Column widths, and the emoji properties that decide presentation. |
| `core/cursor.h` | `CursorStyle`, shared by config and renderer. |
| `render/font_manager.h/.cpp` | FreeType faces per style, plus the fallback chain; rasterizes at an explicit pixel size. |
| `render/box_drawing.h/.cpp` | Geometric line and block glyphs (U+2500–U+259F). |
| `render/glyph_atlas.h/.cpp` | Single `GL_RGBA8` texture, shelf packing, glyph cache. |
| `render/gl_renderer.h/.cpp` | Layered vertex batching, shaders, orthographic projection. |
| `render/terminal_renderer.h/.cpp` | Grid geometry and the grid→draw-call loop. |
| `ui/terminal_widget.h/.cpp` | `QOpenGLWidget`; DPI handling, events, paint. |
| `ui/split_container.h/.cpp` | Binary pane tree over `QSplitter`. |
| `ui/main_window.h/.cpp` | Tabs, shortcut dispatch, window title. |
| `ui/tab_bar.h/.cpp` | The self-drawn tab bar, and the `QTabWidget` that hosts it. |
| `config/chrome.h/.cpp` | Chrome colours, derived from the palette when unset. |
| `config/theme.h/.cpp` | The theme catalogue, and the staged `PaletteOverrides`. |
| `ui/input_handler.h/.cpp` | Qt key events → VT input bytes. |
| `config/config.h/.cpp` | Layered YAML settings and keybindings. |

---

## 3. Data flow: bytes to pixels

### Output path (shell → screen)

```
  shell writes to the pty slave
        │
        ▼
  QSocketNotifier fires on the master fd
        │
        ▼
  TerminalSession::drainPty()
        │   reads up to 32 × 64 KiB per event
        ▼
  TerminalEmulator::write(bytes)
        │
        ├─► Utf8Decoder  bytes → char32_t, retaining any partial sequence
        │
        ▼
  VTParser::advance(code points)
        │   pure syntax: Ground / Escape / CSI / OSC / …
        │
        ├─► VTHandler::print(ch)            printable character
        ├─► VTHandler::control(c0)          BS, HT, LF, CR, BEL, …
        ├─► VTHandler::csiDispatch(seq)     CSI … final
        ├─► VTHandler::escDispatch(i, f)    ESC … final
        └─► VTHandler::oscDispatch(n, data) OSC n ; data ST
              │
              ▼
        TerminalEmulator  (semantics: SGR → pen, modes, replies)
              │
              ▼
        Screen  (cells, cursor, scrolling)  ── revision() bumped
              │
              ▼
        TerminalSession emits screenChanged()
              │
              ▼
        TerminalWidget::update()  →  paintGL()
              │
              ▼
        TerminalRenderer::paint(screen, palette, layout, options)
              │
              ├─► GLRenderer::fillBackground(...)   layer 1
              ├─► GLRenderer::drawGlyph(...)        layer 2
              └─► GLRenderer::fillOverlay(...)      layer 3
                        │
                        ▼
              GLRenderer::endFrame()
                  flush layer 1 → flush layer 2 → flush layer 3
```

The three layers are the whole reason cell backgrounds no longer hide their own
characters. Draw order is a property of the API, not of the order in which the
grid loop happens to emit calls.

### Input path (keyboard → shell)

```
  QKeyEvent
     │
     ▼
  TerminalWidget::keyPressEvent
     │
     ├─ Config::isBound(sequence)? ── yes ─► event->ignore()
     │                                        │  propagates up the widget chain
     │                                        ▼
     │                                   MainWindow::keyPressEvent → handleAction()
     │
     └─ no ─► InputHandler::keyEventToBytes(event, applicationCursorKeys)
                    │
                    ▼
              TerminalSession::sendInput(bytes) → PTY::write → shell
```

The `isBound` check is what makes application shortcuts work at all. The widget
previously accepted *every* key event, so nothing ever reached `MainWindow` and
no keybinding in the config file could fire.

---

## 4. The core layer

### 4.1 `Cell`, `Color` and `Pen`

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

### 4.2 `Screen`

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
relied upon by every shell prompt; see [§8.2](#82-the-white-block-after-every-enter).

**Erase keeps the background.** `Cell::erase(pen)` retains the pen's background
colour but drops other rendition. That is how TUI applications paint full-width
coloured bars with a single `EL` after setting a background.

**Scrolling region.** `scrollTop_`/`scrollBottom_` bound every scroll, insert and
delete, so `DECSTBM` works and full-screen applications can scroll a subrange.

### 4.3 `VTParser`

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

### 4.4 `TerminalEmulator`

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

### 4.4.1 Grapheme clusters and emoji presentation

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
[§10](#10-known-gaps).

### 4.4.2 Colours are owned per session

`TerminalEmulator` holds two palettes: `basePalette_`, seeded from `Config` when
the session starts, and `palette_`, the live one. `OSC 4/10/11/12` mutate the
live palette; `OSC 104/110/111/112` restore individual entries from the base.

Ownership matters here. The palette deliberately does **not** live in `Config`,
because these sequences let a running application retheme *its own* terminal —
one pane changing its background must not disturb another. `TerminalWidget`
therefore reads `session_->palette()`, not `Config::instance().palette()`, both
for the grid and for the frame's clear colour.

Because cells store a palette *index* rather than a resolved colour
([§4.1](#41-cell-color-and-pen)), an `OSC 4` arriving after text is already on
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

### 4.5 `TerminalSession`

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

### 4.6 `PTY`

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

## 5. The render layer

### 5.1 Physical pixels, and why it matters

This is the single most important thing in the render layer.

`QOpenGLWidget` hands `resizeGL()` the widget's size in **logical** pixels, but
sets the GL viewport to the **device-pixel** size of its backing framebuffer
immediately before every `paintGL()`. Measured on a Retina MacBook:

```
resizeGL args:   400 200   |  widget logical size: 400 200  |  devicePixelRatio: 2
paintGL viewport set by Qt: 0 0 800 400
logicalDotsPerInch: 72     |  physicalDotsPerInch: 127.5
```

So a projection built from `width()`/`height()` covers a quarter of the
framebuffer's area and the GPU stretches everything 2× to fill it. On top of
that, `logicalDotsPerInch` is **72** on macOS, so rasterizing a 12 pt font "at
screen DPI" produced a 12-pixel em box — which was then magnified to cover 24
physical pixels.

The rules that follow:

1. `GLRenderer::beginFrame()` takes the framebuffer size in device pixels and
   builds a matching orthographic projection.
2. `TerminalWidget` computes every geometry value as
   `logical × devicePixelRatioF()`.
3. The font is rasterized at `points × (logicalDpi / 72) × devicePixelRatio`,
   which is how Qt sizes its own text.
4. Glyph quads land on integer pixel coordinates, and the atlas uses
   `GL_NEAREST`. With integer positions, fragment centres land exactly on texel
   centres, so the rasterized coverage is reproduced verbatim.
5. `resizeGL()` no longer calls `glViewport()` at all — Qt has already set it,
   correctly, and the widget's logical size was the wrong value to use.
6. No multisampling. MSAA cannot improve an alpha-blended glyph quad (there is no
   geometric edge to smooth — the shape lives in the texture's alpha) and only
   adds a resolve blit.

Measured effect on the same display, rasterizing `g` from Menlo:

| | em box | cell | `g` bitmap | pixels of coverage | antialiased-edge pixels |
|---|---|---|---|---|---|
| before (12 pt @ 72 dpi, then stretched 2×) | 12 px | 7×15 | 7×10 | 70 | 56 % |
| after (13 pt × dpr 2) | 26 px | 16×32 | 13×21 | 273 | 36 % |

Roughly four times the coverage data, a smaller proportion of it spent on soft
edges, and no resampling pass on top.

Moving the window to a screen with a different ratio is handled: `resizeGL()`
compares the current scale against the one the font was last rasterized at and
re-rasterizes when they differ.

### 5.2 `FontManager`

One `FT_Face` per style (regular / bold / italic / bold-italic) of a single
monospaced family, rasterized at an explicit pixel size.

The API takes **pixels**, not points-plus-DPI. The caller already knows how many
physical pixels a cell needs, and the old point/DPI API existed mainly as a place
to feed the wrong DPI into.

Font resolution asks `fc-match` for the file *and the face index*, which matters
because macOS ships collections: all four styles of Menlo live in
`/System/Library/Fonts/Menlo.ttc` at indices 0–3. A style whose face cannot be
found is synthesized with `FT_GlyphSlot_Embolden` / `FT_GlyphSlot_Oblique`.

#### Resolution order, and why it is careful

`loadFamily()` takes a *list* of families and tries them in order:

1. **Each configured preference**, accepted only if fontconfig resolves it to
   that same family. This check is essential: `fc-match` never fails, it
   substitutes — asking for a font that is not installed returns something else
   entirely. On this machine `fc-match "No Such Font"` answers **Verdana**, a
   proportional font, which is unusable in a character grid. `FontFile::family`
   carries the resolved name so the caller can tell.
2. **The platform's monospaced default**, where substitution is welcome because
   the platform's answer *is* the intended fallback. The query adds fontconfig's
   `:spacing=100` constraint so the answer is actually monospaced.
3. **A per-platform list of known font paths**, for a system with no fontconfig
   at all.

Getting the platform default right needed care too. `QFontDatabase::systemFont(
QFontDatabase::FixedFont).family()` returns the generic `"monospace"`, which is
exactly the right thing to hand to fontconfig — but passing it through
`QFontInfo` first resolves it against the font engine, which on macOS answers
`.AppleSystemUIFont`. Feeding *that* to fontconfig produced Verdana again. The
family name is therefore taken straight off the `QFont`, with
`QFontDatabase::isFixedPitch()` as the secondary source.

Finally, every candidate is verified after loading: `regularFaceIsMonospaced()`
compares the unscaled advances of `i` and `W` and rejects the face if they
differ. Font metadata can lie; two glyphs of visibly different width cannot. Even
an explicit request for a proportional family is refused and falls through to the
system monospace, which is the right call for a terminal.

#### The fallback chain

No single monospaced font covers what a terminal has to draw, so the primary
family is backed by a lazily grown list of others.

The motivating case is concrete. A patched "Nerd Font" build can carry twelve
thousand glyphs — every Powerline separator and file-type icon — and still have
**no box-drawing characters at all**, because the family it was patched from
never had them. Every TUI builds its borders, tree guides and separators out of
U+2500–U+257F, so without fallback a full-screen editor renders as a field of
empty `.notdef` boxes. Colour emoji are a second case: they only ever live in a
separate font, and one that stores bitmaps rather than outlines.

`resolveFaceSet()` answers "which family serves this code point", once per code
point, and caches the answer (including misses, since discovery shells out):

1. The primary family, if it has the glyph.
2. Families named in `font.fallback`, then the platform's monospaced default,
   then a list of known colour-emoji families. Loaded on first need.
3. Otherwise, ask fontconfig which font covers the code point
   (`:charset=<hex>`, monospaced-preferred).

Steps 2 and 3 both **verify coverage after loading** rather than trusting the
answer, and both reject placeholder families. That check is not paranoia:
`fc-match ":charset=1F600"` on macOS answers `.LastResort`, a font whose glyphs
are literally empty boxes, and a charset query for box drawing answers
proportional Verdana.

The platform monospace deliberately sits ahead of the emoji fonts, because it is
what supplies the arrows, geometric shapes and check marks a patched icon font
most often lacks.

Fallback faces are rescaled by `matchFallbackSize()` so their line height matches
the primary cell. Different families draw a different proportion of the em, and
leaving them at the same em size left glyphs a pixel or two short of the cell.

#### Colour glyphs

A colour emoji font is bitmap-only with fixed strikes. `FT_LOAD_COLOR` (and
crucially *not* `FT_LOAD_NO_BITMAP`) yields a `FT_PIXEL_MODE_BGRA` bitmap;
`FT_Set_Char_Size` picks the nearest strike and scales it, so no manual strike
selection is needed. Colour faces are sized from
`min(cellHeight, 2 × cellWidth)` because emoji are double-width and span two
cells — sizing them from the line height alone made them bleed into the next row.

FreeType hands back *premultiplied* BGRA. `rasterizeFrom()` un-premultiplies and
swaps to RGBA so colour and coverage glyphs share one straight-alpha blend mode,
and marks the result with `GlyphBitmap::isColor`.

#### Presentation-aware resolution

`GlyphPresentation` (`Auto` / `Text` / `Emoji`) is threaded from the cell's
`CellFlagEmojiPresentation` through `drawGlyph()` and the atlas key down to
`resolveFaceSet()`, because the same code point can legitimately be cached twice
— once monochrome, once in colour.

Presentation sets the search *order*, not a hard filter: if no font of the
preferred kind has the glyph, one of the other kind still beats a `.notdef` box.
Two details earn their keep:

- A colour request **skips the primary font**. The primary is the monospaced text
  font, and its flat glyph is exactly what the selector asked us not to use.
- fontconfig discovery runs **inside** the strict pass. No monospaced font on a
  stock macOS carries U+26A0, so a text-presentation request would otherwise
  settle for the colour emoji — precisely what U+FE0E asks us not to do.

`FaceSet::hasRenderableGlyph()` requires the face to actually *draw* something,
not merely to have a cmap entry. Colour emoji fonts map regional indicators and
keycap digits to **empty** glyphs, because the real flag or keycap is only
reachable by shaping the whole sequence; choosing such a face would render
nothing at all. With the check, a keycap falls through to the plain digit and a
flag to a visible box.

Rasterization uses `FT_LOAD_TARGET_LIGHT` with `FT_RENDER_MODE_LIGHT`: light
hinting snaps stems vertically without touching horizontal metrics, which is what
a monospaced grid needs.

`computeMetrics()` derives the cell from a representative glyph (`M`, `0`, `x`)
rather than `max_advance`, because many monospaced fonts carry oversized advances
for box-drawing or fullwidth glyphs, which would leave a visible gap between
columns. Any leftover line gap is split evenly above and below so glyphs sit
optically centred.

### 5.2.1 Box drawing is geometric, not from a font

Line and block characters (U+2500–U+259F) are drawn from the cell geometry by
`render/box_drawing.cpp`, ahead of any font lookup.

They have to *tile*: a vertical line must meet the one in the row below with no
seam, and a horizontal line must span the cell exactly. No font can guarantee
that once a fallback is involved, because families disagree about how much of the
em their box glyphs occupy — the fallback arrives a pixel or two short and borders
look dashed. Scaling the fallback to compensate only trades gaps for overhang.

A line character is fully described by the weight of its four arms
(none/light/heavy/double), which turns ~150 code points into one table and one
draw routine. Each arm runs from its cell edge *past* the centre by half the
perpendicular stroke, so corners and tees have no notch. Blocks, eighths,
quadrants and the three shades are handled separately; shades use an ordered 2×2
dither, which reads as an even tone at cell sizes.

The output is an ordinary 8-bit coverage mask, so it caches in the atlas and
renders through exactly the same path as a font glyph. Rounded corners
(U+256D–U+2570) are drawn square, and the diagonals (U+2571–U+2573) fall through
to the font.

### 5.3 `GlyphAtlas`

One `GL_RGBA8` texture with a shelf (row-based) packing allocator and a
`codepoint | style → CachedGlyph` cache. A whole screen of text is one draw call.

RGBA rather than a single coverage channel, because colour emoji have to live
here too. A coverage mask is widened to `(255, 255, 255, coverage)` on upload and
tinted by the shader; a colour glyph is stored as-is and drawn untinted, selected
per-vertex by `CachedGlyph::isColor`. One texture and one draw call for both is
considerably simpler than maintaining two atlases, and 4 MiB for a 1024 px atlas
is not worth optimising. It also removed the `GL_TEXTURE_SWIZZLE_*` dance that
the single-channel format needed.

Filtering is `GL_NEAREST` deliberately (see §5.1). A 1-pixel gutter between
glyphs keeps rounding from ever picking up a neighbour.

Two things this class now handles that it previously only claimed to:

- **Growth is wired up.** `glyph()` grows the texture (or, at the 4096 cap, flushes
  the cache) when an allocation fails, so callers never see a "texture full"
  error. The old `grow()`/`isFull()` pair was never called from anywhere.
- **Mid-frame rebuilds are safe.** Rebuilding invalidates every UV already
  batched for the frame. A `generation()` counter lets `GLRenderer` notice,
  discard the stale batch and report `needsRepaint()`, and `TerminalWidget`
  schedules one more paint. Without this, a frame that happened to trigger growth
  would draw garbage.

The file also lost about sixty lines of duplicated `#ifdef Q_OS_MACOS` "Apple
Silicon workaround" that called the native GL entry points directly. The actual
problem those branches were working around was the missing swizzle for the
single-channel format, not Qt's wrappers, so there is now one code path.

### 5.4 `GLRenderer`

Batched 2D drawing with three explicit layers:

| Layer | API | Contents |
|---|---|---|
| 1 | `fillBackground()` | cell backgrounds, underlines, strikethrough |
| 2 | `drawGlyph()` | glyphs |
| 3 | `fillOverlay()`, `strokeOverlay()` | cursor, selection, focus indicators |

`endFrame()` flushes them bottom-up. Draw order is therefore a property of the
API rather than of the order in which the grid loop happens to submit calls —
which is the permanent fix for cell backgrounds painting over their own
characters.

`drawGlyph()` takes a code point, not a `QString`. The old `drawText(QString, …)`
was called once per cell from the grid loop, allocating a one-character `QString`
for every cell of every frame.

Vertex buffers grow geometrically on demand rather than being capped at a fixed
size, and use `StreamDraw`.

### 5.5 `TerminalRenderer`

The only place that knows how a grid maps onto pixels. `computeLayout()` derives
rows, columns and centring padding from the font metrics and the viewport;
`paint()` walks the grid and emits draw calls.

Worth noting:

- **Background runs are merged.** Horizontally adjacent cells sharing a
  background become one quad, so a full-width coloured bar costs 6 vertices
  instead of 6 per column. The common case — everything on the default
  background — emits nothing at all, because the frame was already cleared to
  that colour.
- **Bold and italic select a real font style** via `fontStyleFor(bold, italic)`.
  Previously every cell was drawn with the regular face and bold was faked by
  lightening the foreground colour, so bold text was merely brighter.
- **Wide-character trailers are skipped**, so a double-width glyph is not
  double-struck.
- **Leftover pixels are split evenly** between left/right and top/bottom, so a
  window that is not an exact multiple of the cell size does not look
  off-centre.
- **Padding insets the grid** from the window edge by `window.padding` logical
  pixels (default 4), scaled to physical pixels by the widget. The padding is
  subtracted before rows and columns are computed, and any remainder is then
  shared as above, so both edges keep their gap. Padding is clamped so it can
  never cost the last row or column on a very small window — the gap is given up
  before the content is.

---

## 6. The UI layer

### 6.1 `TerminalWidget`

A `QOpenGLWidget` that owns a `GLRenderer`, a `TerminalSession` and a
`TerminalRenderer`, and does little else: translate events, compute the layout,
paint. Process management, byte decoding and VT interpretation all live in
`TerminalSession`; the grid→pixels mapping lives in `TerminalRenderer`.

Details worth knowing before editing it:

- `resizeGL()` must **not** call `makeCurrent()`/`doneCurrent()`. Qt invokes it
  with the context already current, and releasing it leaves Qt's own resize
  handling without one. `reloadFont()`, which is called from outside a paint,
  does need the pair.
- The destructor makes the context current before releasing GL-owned objects.
- The cursor blink timer only runs when the pane has focus *and* blinking is
  enabled. It used to repaint the entire grid twice a second unconditionally.
  Incoming output resets the blink phase so the cursor stays solid while text
  arrives.
- An unfocused pane draws a hollow cursor, which is how tiling terminals signal
  "input does not go here".

### 6.2 `SplitContainer`

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

### 6.3 `TabBar`

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

### 6.4 `MainWindow`

Tabs, shortcut dispatch and the window title. `handleAction()` is a single
`switch` over `Action`, and the tab bar auto-hides when there is only one tab so
a single-terminal window looks like a terminal.

Note that both `paneSessionEnded` and `paneTitleChanged` connect to *member
functions*: `Qt::UniqueConnection` is silently rejected for lambdas, and
`installTabRoot()` may reconnect the same root more than once.

### 6.5 `InputHandler`

Qt key events to VT bytes, with xterm's modifier encoding (`CSI 1 ; mod A`), so
Shift+Arrow and Ctrl+Arrow are distinguishable from the bare key. Cursor keys
switch to the SS3 form (`ESC O A`) when the application has set `DECCKM`.

Covered by `tests/test_input.cpp`, including the check that shell control keys
(Ctrl+C/D/W/R/Z/L/A/E/U and Tab) are **not** bound to application shortcuts.

---

## 7. Configuration

Settings are **YAML**, loaded in layers, each overriding only the keys it
contains:

1. built-in defaults (`Palette`'s constructor and `Config::applyBuiltInDefaults`)
2. `:/config/default_config.yaml` — compiled into the binary from
   `src/config/default_config.yaml`
3. `~/.config/ratty/config.yaml` — the user's overlay

The layering is the important property: an overlay is not a replacement, so a
file that mentions only `font.size` changes only that — every other colour,
binding, window setting and font preference keeps its default. A config without a
`keybindings` section must not leave the application with no keybindings, and the
bundled defaults must be found regardless of the working directory.

The one place the rule is deliberately not literal is keybindings, where naming
an *action* releases the keys it inherited; see
[Action ownership](#action-ownership).

Two settings interact, and the shipped defaults are arranged so the interaction
stays predictable: `colors.cursor` is deliberately **absent** from the bundled
file, so that it follows `colors.foreground`. Had the default set it explicitly, a
user who changed only the foreground would keep a cursor in the old colour —
close to invisible on an inverted theme.

YAML rather than JSON because a file people edit by hand wants comments, and
needs neither quoting of every key nor comma discipline. The parsing uses
`yaml-cpp`, kept out of every other translation unit by a nested
`Config::Parser` declared in the header and defined in the implementation file —
a nested class has access to its enclosing class's private members, so no
friendship is needed.

#### The one YAML sharp edge

`#` starts a comment, so `background: #1e1e1e` parses as an *empty value*, not a
colour. Every scalar reader returns `std::optional`, and the colour reader
recognises this specific case and says so:

```
Config: colour background is empty - hex colours must be quoted in YAML, e.g. "#1e1e1e"
```

The shipped default quotes every colour and documents the rule at the top of the
file. Silently reading an empty value here would paint the terminal black.

#### Failure behaviour

Each layer is independent and each key optional, so a broken overlay degrades
predictably rather than half-applying:

| Input | Result |
|---|---|
| YAML syntax error | whole overlay discarded, with line and column reported |
| top level is not a mapping | overlay discarded |
| empty file | no-op |
| unusable scalar (`size: "abc"`) | that key skipped and named; the rest of the file still applies |
| out-of-range value | clamped (`MIN_FONT_SIZE`..`MAX_FONT_SIZE`, padding, opacity) |
| unknown action name | reported and skipped |
| a `config.json` left over from before | reported as no longer read, with the path to move it to |

```yaml
font:
  family: [DroidSansMono Nerd Font, Menlo]   # or a single name
  fallback: []
  size: 13

cursor:
  style: block        # block | hollow | underline | bar
  blink: true

colors:
  background: "#1e1e1e"
  foreground: "#dcdcdc"
  cursor: "#dcdcdc"
  red: "#cd3131"
  bright_red: "#f14c4c"

window:
  width: 1280
  height: 720
  padding: 4
  opacity: 1.0
  fullscreen: false

keybindings:
  ctrl+shift+t: new_tab
  ctrl+shift+w: none    # removes a default binding
```

- All 16 base ANSI colours are overridable by name (`black`, `red`, …,
  `bright_white`). Slots 16–255 are generated (6×6×6 cube plus greyscale ramp).
- `font.family` accepts a single name **or an array tried in order**. The array
  form is how a config can name a preferred font and still degrade gracefully on
  a machine where it is not installed; see
  [§5.2](#52-fontmanager) for the resolution rules. `"Monospace"` or `""` means
  "ask the platform".
- `font.fallback` names families to consult for code points the primary font
  lacks, ahead of automatic discovery. Left empty, the platform's monospaced font
  and any installed colour-emoji font are used.
- `window.padding` is the gap between the text and the window edge, in logical
  pixels.
- Cursor styles: `block`, `hollow`, `underline`, `bar`.
- Binding an action to `"none"` **removes** a default binding — the only way for
  a user overlay to unbind something it did not create.
- Action names live in one table (`kActionNames`) used for both directions of the
  string↔enum mapping, instead of a hand-maintained `switch` and `if`-chain.

Key sequences accept `ctrl`/`control`, `shift`, `alt`/`option`,
`meta`/`super`/`cmd`, named keys (`up`, `pageup`, `escape`, …), function keys
(`f1`…`f12`), and spelled-out punctuation (`plus`, `minus`, `underscore`,
`backslash`, `bracketleft`, …) — the last because `ctrl+shift++` cannot be split
on `+` unambiguously.

### Two keybinding sets

The configuration carries `keybindings` and `keybindings_macos`, and exactly one
is active. `mac_os_bindings` accepts `true`, `false`, or `auto` (the default),
which follows the platform.

Resolution is deliberately **two-phase**. The parser fills both sets as it reads
each layer, and `resolveKeybindings()` picks one only after every layer has been
read — because `mac_os_bindings` may appear in any layer, and in any position
within a file, and streaming the decision would make the result depend on key
order. Each set is overlaid independently, so `none` removals and additions apply
to whichever set they were written in.

#### Action ownership

Bindings are staged per document into a `BindingLayer` rather than written
straight through, so `mergeBindings()` can see every action a document assigns
before deciding what to displace.

For the **user's** layer, any action it assigns is treated as fully described by
that layer, and the keys inherited for it are dropped first. This is what makes

```yaml
keybindings_macos:
  ctrl+shift+w: split_vertical
```

mean "split_vertical is now Ctrl+Shift+W" rather than "Ctrl+Shift+W *also* splits
vertically" — otherwise the default `⌘⇧D` would keep working and two keys would
do the same thing, which is not what a user writing that line intends.

The bundled defaults are merged *without* the rule, so they can legitimately
offer several keys for one action (`ctrl+shift+e` and `ctrl+shift+backslash` both
split horizontally). A user wanting two keys lists both.

#### Themes, and why colours are staged

A theme is a configuration fragment holding a `colors:` section, shipped under
`:/themes`. That means themes need no parser of their own and a user can read one
to learn the format; the catalogue is enumerated from the resource system rather
than a second hard-coded list, so adding a theme is a file plus a line in
`themes.qrc`.

The interesting part is ordering. `theme:` is itself a setting, so which theme is
active is not known until every layer has been read — and by then the user's own
`colors:` entries have already been seen. Applying colours as they are parsed
would make the outcome depend on whether `theme:` happened to appear above or
below `colors:` in the file.

So colours are **staged**, not applied. Each layer's colours go into a
`PaletteOverrides` for that layer, and `resolvePalette()` merges them in a fixed
order once everything has been read:

```
Palette()  ->  built-in layer  ->  theme  ->  user
```

`theme: nord` plus `colors: {red: ...}` therefore gives Nord with one colour
changed, whichever comes first in the file. Chrome is staged the same way by
`resolveChrome()`.

`PaletteOverrides::mergeInto()` also carries the cursor rule: a stated foreground
moves the cursor with it unless the cursor is stated too. Without that, switching
to a light theme would leave the cursor in the dark theme's colour.

An unknown theme name is reported with the list of available ones and then
discarded, leaving the built-in palette — which is complete, so the terminal is
still usable.

#### Reporting a config that cannot work

Two situations would otherwise leave a user convinced the file is not being read
at all, so both are reported:

- the active set is empty while the other is not — the other is used
- the user's layer wrote only to the *inactive* set, naming the section they
  should have edited instead

If the macOS set is active but empty while the other one is not, the other is used
and a warning is logged: a config that edits the inactive set would otherwise
appear to do nothing at all.

### `cmd` and `ctrl` mean the same thing everywhere

Qt swaps Control and Meta on macOS by default: `Qt::ControlModifier` is the
Command key and `Qt::MetaModifier` is physical Control. For a terminal that is
backwards in the worst way — `InputHandler` maps `Qt::ControlModifier` to C0
control characters, so **Command+C sent SIGINT and physical Ctrl+C did nothing**
— and it would make a `cmd+t` binding fire on Ctrl+T.

`main()` therefore sets `Qt::AA_MacDontSwapCtrlAndMeta`, after which
`Qt::ControlModifier` is always physical Control and `Qt::MetaModifier` is always
Command. Verified:

```
default:                  Qt::ControlModifier + T -> ⌘T    Qt::MetaModifier + T -> ⌃T
AA_MacDontSwapCtrlAndMeta: Qt::ControlModifier + T -> ⌃T    Qt::MetaModifier + T -> ⌘T
```

### Layout tolerance

Qt reports either the unshifted key or the shifted symbol for the same physical
key, depending on platform and layout: `Ctrl+Shift+1` arrives as `Key_1` on one
machine and `Key_Exclam` on another. A binding matched only against the literal
combination would therefore work on some keyboards and not others.

`Config::lookupAction(const QKeyEvent*)` handles this by retrying with the key's
shift partner (`1`↔`!`, `\`↔`|`, `-`↔`_`, `=`↔`+`, …). The retry happens **only**
when Shift is held: without Shift there is no ambiguity about which symbol was
meant, and rewriting unshifted keys would risk turning `Ctrl+C` into a shortcut.

A consequence worth keeping in mind when editing the defaults: two actions must
never be bound to the two halves of the same physical key, because which one wins
would then depend on the user's keyboard. The default bindings put font sizing on
`+`/`-` and splits on letters (plus `\` as an alias) for exactly this reason.

Font sizing needs care for the same reason, since "plus" is not one key event.
`⌘=`, `⌘⇧=` (which types `+`) and a numeric-keypad `⌘+` are three different
combinations, so the macOS set binds `cmd+equal`, `cmd+shift+equal` and
`cmd+plus`, with `cmd+minus` and `cmd+shift+minus` mirroring it. All six are
covered by `tests/test_input.cpp`.

`Config::save()` no longer exists. It was a no-op that logged "not yet
implemented" while `closeEvent` dutifully wrote the window size into it;
persisting geometry is listed in `todo-ratty.md` instead of being pretended at.

---

## 8. Two bugs worth understanding

Both of these were reported as visual defects and both turned out to have precise
mechanical causes. They are documented because the reasoning generalises.

### 8.1 Blurry text

**Symptom.** Text noticeably softer than kitty at the same size.

**Cause.** Four compounding factors, all variations on "logical pixels used where
physical pixels were meant":

1. `GLRenderer::beginFrame(width(), height())` built the projection from the
   widget's **logical** size, while Qt had set the viewport to the
   **device-pixel** size. On a `devicePixelRatio` of 2 the entire scene was
   stretched 2× by the GPU.
2. The font was rasterized at `screen->logicalDotsPerInch()`, which is **72** on
   macOS, so a 12 pt font got a 12-pixel em box — a quarter of the data needed to
   fill the 24 physical pixels it was then stretched across.
3. The atlas used `GL_LINEAR`, so that stretch was a resampling pass rather than
   a clean magnification.
4. `setSamples(4)` requested 4× MSAA for a pure 2D alpha-blended pass, adding a
   resolve blit that helped nothing.

`resizeGL()` also called `glViewport(0, 0, w, h)` with the logical size. That one
was harmless only by accident: Qt overwrites the viewport before each `paintGL()`.

**Fix.** §5.1. Measured result: ~4× the glyph coverage data and no resampling.

### 8.2 The white block after every Enter

**Symptom**, exactly as reported:

```
<white block>
terminal@prompt: somecommand
somecommand output
<white block>
terminal@prompt:
```

**Investigation.** Capturing what zsh actually writes to the pty after Enter:

```
\x1b[1m\x1b[7m%\x1b[27m\x1b[1m\x1b[0m<79 spaces>\r \r
\x1b]2;…\x07\x1b]7;file://…\x1b\\
\r\x1b[0m\x1b[27m\x1b[24m\x1b[J<prompt>
```

That first line is zsh's `PROMPT_SP` end-of-line marker. It prints an inverse
`%`, pads with spaces to the right margin, then does `\r` `space` `\r` to erase
the marker. The trick depends entirely on **deferred wrap**: in a correct
terminal, filling the last column leaves the cursor on that same row, so the
`\r space \r` erases the `%` and the prompt is drawn over it.

**Cause.** Two independent defects composing:

1. **`Screen` wrapped eagerly.** `putCharAtCursor` set `cursorCol_ = 0;
   cursorRow_++` the instant a character filled the last column. So the padding
   spaces advanced the cursor a row, the erase landed one row too low, and the
   inverse `%` survived — plus an extra row was consumed on every prompt.
2. **Rectangles were flushed after text.** `endFrame()` called
   `flushTextBatch()` and then `flushRectBatch()`, so any cell with a
   non-default background painted over its own glyph. The marker cell's inverse
   attribute made its background the near-white foreground colour, and the `%`
   glyph underneath was covered — turning an inverse `%` into a featureless
   white block.

**Verification.** Replaying that byte stream through the old core reproduces it
exactly — one opaque block per Enter, each on its own row:

```
 0 |echo hola                    |
 1 |<opaque block>               |
 2 |dalvarez@MM repos/ratty (main) > |
 3 |<opaque block>               |
 4 |dalvarez@MM repos/ratty (main) > |
--> opaque blocks drawn over text: 3
```

The same stream through the new core gives zero opaque cells and one row per
prompt. Pinned by `tests/test_terminal.cpp`
(`testDeferredWrap`, `testZshPromptArtifact`).

**Fix.** Deferred wrap in `Screen` ([§4.2](#42-screen)) and layered draw order in
`GLRenderer` ([§5.4](#54-glrenderer)).

While in the same area, three more defects in the same byte stream were fixed:
the `ESC \` string terminator printed a stray `\` into the grid; `\x1b[38;5;208m`
selected no colour at all; and a UTF-8 sequence split across two pty reads became
replacement characters.

---

## 9. Building, running and testing

### Dependencies

- CMake ≥ 3.16
- A C++20 compiler (Apple Clang 15+, Clang 16+, GCC 12+)
- Qt 6 — Core, Gui, Widgets, OpenGL, OpenGLWidgets
- FreeType ≥ 2
- yaml-cpp ≥ 0.7
- OpenGL 3.3 core profile
- `fontconfig` (`fc-match`) is *recommended*; without it a built-in list of font
  paths is used

```bash
# macOS
brew install cmake qt@6 freetype fontconfig yaml-cpp

# Debian / Ubuntu
sudo apt install build-essential cmake qt6-base-dev libfreetype6-dev \
                 libgl1-mesa-dev fontconfig libyaml-cpp-dev
```

### Build

```bash
cmake -S . -B build
cmake --build build -j
./build/ratty
```

`CMAKE_BUILD_TYPE` defaults to `Release`. The build is warning-clean under
`-Wall -Wextra -Wpedantic`.

The project no longer pins a compiler. It used to default
`CMAKE_CXX_COMPILER` to `/opt/homebrew/opt/llvm@20/bin/clang++`, which fails on
any machine without that exact Homebrew formula.

### Tests

```bash
cmake -S . -B build -DRATTY_BUILD_TESTS=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

| Suite | Covers |
|---|---|
| `test_terminal` | deferred wrap, the zsh prompt artifact, OSC termination and colour control, CSI parsing, scrolling regions, the alternate buffer, SGR colours, erase semantics, emoji presentation selectors, grapheme clustering, UTF-8 chunk splitting, wide characters, resize, device reports |
| `test_input` | both keybinding sets resolving against real `QKeyEvent`s, set exclusivity, layout tolerance, shell control keys staying unbound, VT input encoding |
| `test_splits` | pane tree surgery: nothing destroyed that should survive, nothing left invisible, directional navigation |
| `test_config` | the real load path against a sandboxed HOME: overlay semantics, colours, keybinding add/remove, `mac_os_bindings` resolution, every shipped theme's completeness and chrome coherence, theme-versus-override precedence in both file orders, the unquoted-colour trap, malformed files, clamping |
| `test_tabbar` | style and position parsing, chrome derivation on dark *and light* palettes, bar thinness, tab metrics, and that every style paints something |
| `test_render` | grid padding maths, box-drawing tiling, fallback coverage of the characters a TUI draws, text-vs-emoji font selection, font preference order, and the guarantee that no resolution path yields a proportional font |

They run under `QT_QPA_PLATFORM=offscreen` and need no GPU. `tests/check.h` is a
three-function harness, not a framework.

One CMake subtlety, since it caused a confusing failure: a `.qrc` compiled into a
**static** library registers itself from a global initializer that the linker
discards, because nothing references it — the resources then silently do not
exist. `RATTY_RESOURCES_ABS` is therefore compiled into each executable rather
than into `ratty_lib`.

---

## 10. Known gaps

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
([§4.4.1](#441-grapheme-clusters-and-emoji-presentation)), but the exact sequence
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

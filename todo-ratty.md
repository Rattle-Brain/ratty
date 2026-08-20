# RaTTY — state of the art and roadmap

Last updated for **v0.2.0**.

This file tracks what works, what is broken, and what comes next. Items marked
🔍 have a design note in
[DOCUMENTATION.md § 10](DOCUMENTATION.md#10-known-gaps) explaining why the gap is
where it is.

---

## Where the project stands

RaTTY is usable as a daily driver for shell work. The rendering path is correct
on HiDPI displays, the VT parser handles what a modern shell and most TUI
applications emit, and the terminal model is separated cleanly enough from Qt and
OpenGL to be tested headlessly.

The two things a user notices as missing are **scrollback** and **text
selection**.

```
Foundations ████████████████████ done
VT emulation ██████████████████░░ good enough for shells + most TUIs
Rendering    ████████████████████ done (sharp, HiDPI-correct, emoji, box drawing)
UI shell     ████████████████░░░░ tabs + splits work; no selection
Polish       ████████░░░░░░░░░░░░ no scrollback, no mouse, no fallback fonts
```

---

## ✅ Done

### Foundations
- [x] `forkpty` PTY wrapper with the user's **login** shell, `TERM` and
      `COLORTERM` set, `LINES`/`COLUMNS` cleared, signal dispositions reset
- [x] Non-blocking master fd driven by `QSocketNotifier`, drained in a bounded
      loop rather than one read per event
- [x] Read outcomes distinguished: data / `EAGAIN` / EOF (including `EIO`) / error
- [x] Session teardown that reaps the child exactly once
- [x] Qt6 window with an OpenGL 3.3 core context

### Terminal emulation
- [x] ECMA-48 state machine: Ground / Escape / intermediates / CSI / OSC /
      DCS-SOS-PM-APC, with private markers and intermediate bytes recognised
      rather than printed
- [x] Parser holds **no** terminal state — syntax and semantics are separate
      classes
- [x] **Deferred (pending) line wrap** — the VT-correct behaviour shell prompts
      depend on
- [x] Scrolling region (`DECSTBM`), `IL`/`DL`/`ICH`/`DCH`/`ECH`, `SU`/`SD`
- [x] Alternate screen buffer (`?1047`/`?1048`/`?1049`) so vim and less do not
      destroy the shell's screen
- [x] Full cursor movement set, `ED`/`EL` all modes, `DECSC`/`DECRC`, `RIS`
- [x] DEC modes: `DECCKM`, `DECAWM`, `DECTCEM`, bracketed paste, `LNM`
- [x] `DSR` 5/6 and `DA1` replies routed back to the shell
- [x] SGR: 16-colour, bright, 256-colour and 24-bit truecolour, in both the `;`
      and `:` spellings
- [x] `OSC 4`/`104` palette entries and `OSC 10`/`11`/`12` + `110`/`111`/`112`
      default colours, settable **and** queryable, owned per session
- [x] `DECSCUSR` application cursor shape
- [x] Erase retains the pen's background (coloured bars work)
- [x] `OSC 0`/`2` window title; `ESC \` (ST) correctly consumed
- [x] Incremental UTF-8 decoding across pty read boundaries
- [x] Double-width character layout with trailer cells
- [x] Emoji presentation selectors (`U+FE0F` / `U+FE0E`), including widening and
      narrowing a cell that has already been placed
- [x] Grapheme clustering for emoji sequences -- joiners, skin tones, regional
      indicator pairs, keycaps and tag sequences all occupy one cell
- [x] O(1) scroll via a row indirection table

### Rendering
- [x] FreeType rasterization at an explicit **physical** pixel size
- [x] HiDPI correctness: device-pixel projection, device-pixel layout,
      `devicePixelRatio`-scaled font, integer-snapped quads, `GL_NEAREST`
- [x] Re-rasterization when the window moves to a screen with a different ratio
- [x] Light hinting (`FT_LOAD_TARGET_LIGHT`) for crisp stems
- [x] Cell metrics from a representative glyph, not `max_advance`
- [x] Four font styles per family, resolved through `fc-match` **with face
      index** (macOS `.ttc` collections), synthesized when a face is missing
- [x] Font preference *list*, falling back to the system's configured monospaced
      font, with substitution detection so a missing family can never yield a
      proportional face
- [x] Font **fallback chain**: code points the primary font lacks are served from
      configured families, the platform monospace, or a fontconfig charset
      lookup -- each verified to actually cover the code point
- [x] Colour emoji, from the emoji font's own bitmap strikes, in a shared RGBA
      atlas with a per-vertex tint flag
- [x] Box-drawing and block characters (U+2500-U+259F) drawn geometrically, so
      they tile exactly whatever font is in use
- [x] Configurable window padding, clamped so it never costs a row or column
- [x] Single `GL_R8` atlas, shelf packing, swizzled to `(R,R,R,1)`
- [x] Atlas growth actually wired up, and safe when it happens mid-frame
- [x] Layered draw order — backgrounds, then glyphs, then overlays
- [x] Background run merging (one quad per run, not per cell)
- [x] Per-codepoint glyph API — no `QString` allocation per cell per frame
- [x] MSAA removed; it could not help alpha-blended glyph quads
- [x] One GL code path (the duplicated macOS "workaround" branches are gone)

### UI
- [x] Tabs, with the tab bar auto-hidden when there is only one
- [x] Recursive split panes over `QSplitter`, with correct Qt ownership on both
      split and close
- [x] Directional focus movement between panes
- [x] Keybindings that actually fire — the terminal widget defers bound sequences
      to the window instead of swallowing every key
- [x] xterm modifier encoding (`CSI 1;mod A`) and `DECCKM` SS3 forms
- [x] Bracketed paste, `LF`→`CR` translation on paste
- [x] Live font resizing across every pane in every tab
- [x] Platform keybinding sets: `keybindings_macos` (Command) and `keybindings`
      (Ctrl+Shift), selected by `mac_os_bindings: auto | true | false`
- [x] Font-size shortcuts on both sets, covering every key event "plus" and
      "minus" can arrive as
- [x] Cursor styles (block / hollow / underline / bar); blink only when focused
- [x] Window title from `OSC`, per-tab

### Project
- [x] Layered **YAML** config: built-in -> bundled resource -> user overlay, with
      per-key optional overrides and a named diagnostic for every failure mode
- [x] A user config overrides only what it states; naming an action releases the
      default keys for it, and editing the inactive keybinding set is reported
- [x] Full 256-colour palette, 16 base colours overridable by name
- [x] `"none"` unbinds a default keybinding
- [x] Headless test suites (terminal / input / splits) with `ctest`
- [x] Warning-clean under `-Wall -Wextra -Wpedantic`
- [x] No pinned compiler path in `CMakeLists.txt`
- [x] Empty placeholder translation units removed

---

## 🐛 Fixed in v0.2.0

Recorded because the causes are instructive; full write-ups in
[DOCUMENTATION.md § 8](DOCUMENTATION.md#8-two-bugs-worth-understanding).

- [x] **Blurry text.** The projection was built from the widget's *logical* size
      while Qt had set the viewport to *device* pixels, so the scene was
      stretched 2× on Retina; the font was rasterized at macOS's 72 logical DPI,
      giving a 12-pixel em box for a 12 pt font; the atlas filtered `GL_LINEAR`;
      and 4× MSAA was requested for a 2D alpha-blended pass. Now ~4× the glyph
      coverage data with no resampling.
- [x] **A white block above every prompt.** Two defects composing: `Screen`
      wrapped eagerly, so zsh's `PROMPT_SP` end-of-line marker was never erased
      (and an extra row was consumed per prompt); and rectangles were flushed
      *after* text, so the marker's inverse background painted over its own `%`
      glyph, turning it into a featureless block.
- [x] **Stray `\` in the grid.** `ESC \` (string terminator) dropped out of the
      OSC state on the `ESC` and printed the `\`. zsh emits `OSC 7 … ST` before
      every prompt.
- [x] **256-colour and truecolour ignored.** `SGR 38;5;N` was fed through a flat
      per-parameter switch, so `38` matched nothing and `5`/`N` were interpreted
      as unrelated attributes.
- [x] **Mojibake on large output.** `QString::fromUtf8` was called per 4 KiB pty
      read, so any multi-byte character split across the boundary became
      replacement characters.
- [x] **Coloured-background text was invisible.** Background rectangles were
      drawn on top of glyphs.
- [x] **No keybinding ever worked.** `TerminalWidget::keyPressEvent` accepted
      every event, so nothing reached `MainWindow`.
- [x] **A config file without a `keybindings` section removed all keybindings.**
      The loader cleared the table, then inserted only what the file listed.
- [x] **Bundled defaults were often not found.** They were loaded through the
      relative path `src/config/default_config.json`.
- [x] **Closing a split could destroy the surviving pane.** Only the logical
      parent pointer was cleared before `deleteLater()`, while the sibling was
      still a Qt child of the doomed splitter.
- [x] **A reattached pane could stay invisible.** `QSplitter::insertWidget()`
      will not re-show a widget carrying `WA_WState_ExplicitShowHide`, which
      every former tab page does.
- [x] **`hasChildExited()` consumed the exit status.** It called `waitpid` from a
      `const` method on every poll, so cleanup could no longer reap.
- [x] **`TERM` was never set**, so a shell launched outside a terminal fell back
      to `dumb`.
- [x] **Bold was fake.** Every cell was drawn with the regular face and bold was
      approximated by lightening the colour.
- [x] **Default colours were defined in three places** that disagreed; a custom
      background made every cell paint an opaque rectangle in the old colour.
- [x] **`resize()` sent `SIGWINCH` by hand** to the shell rather than letting
      `TIOCSWINSZ` signal the foreground process group.
- [x] **`Config::save()` was a no-op** that `closeEvent` wrote window geometry
      into.
- [x] **A missing font family silently became a proportional font.** `fc-match`
      substitutes rather than failing, so a typo or an uninstalled font produced
      Verdana in a character grid. Compounded by
      `QFontInfo(systemFont(FixedFont)).family()` answering `.AppleSystemUIFont`
      on macOS, which fontconfig also substituted with Verdana — so even the
      *fallback* was proportional.
- [x] **`DECSCUSR` was ignored**, so an editor's insert-mode cursor never
      changed shape.
- [x] **`OSC 11` queries went unanswered**, leaving Neovim to guess whether the
      terminal was light or dark.
- [x] **On macOS, Command+C sent SIGINT and Ctrl+C did nothing.** Qt swaps
      Control and Meta on that platform by default, and `InputHandler` maps
      `Qt::ControlModifier` to the C0 control characters. Fixed with
      `Qt::AA_MacDontSwapCtrlAndMeta`, which also makes `cmd+` bindings possible.
- [x] **A patched icon font turned a TUI into a field of empty boxes.** There was
      no fallback chain, and `DroidSansMono Nerd Font` -- like the family it was
      patched from -- has no box-drawing characters, which is what every TUI
      draws its borders with. Fixed by drawing those geometrically and by adding
      the fallback chain for everything else.
- [x] **Emoji were empty boxes**, since colour emoji live in a separate bitmap
      font and neither the fallback chain nor an RGBA atlas existed to hold
      them.
- [x] **Emoji sequences sprawled across four to eight columns.** Joiners, skin
      tones, regional indicators and tag characters were each printed as their own
      cell, so a joined emoji occupied several and left the cursor misplaced.
- [x] **`U+FE0F` / `U+FE0E` had no effect**, being dropped with the other
      zero-width marks, so the text and emoji forms of a dual-form code point
      were indistinguishable.
- [x] **A colour emoji font's component glyphs are empty.** Regional indicators
      and keycap digits exist in its cmap only as shaping inputs, so selecting
      that face drew nothing at all; coverage is now checked by rendering rather
      than by the cmap.
- [x] **A pinned compiler path** (`/opt/homebrew/opt/llvm@20/bin/clang++`) broke
      configuration on any other machine.

---

## 🎯 Next up

Roughly in the order that gives the most user-visible benefit per unit of work.

### 1. Scrollback buffer 🔍
The single most missed feature.

- [ ] Turn `Screen`'s row indirection table into a ring buffer with history
- [ ] `scrollUp` pushes the evicted row into history instead of clearing it
- [ ] A view offset, so rendering reads `history + viewport`
- [ ] Mouse wheel and `Shift+PageUp`/`PageDown` move the offset
      (the actions are already bound and currently inert)
- [ ] Any new output, and any keypress, snaps back to the live view
- [ ] Configurable line limit; `Ctrl+Shift+K` clears it
- [ ] Reflow on resize, or an explicit decision not to reflow

### 2. Text selection and clipboard 🔍
- [ ] Selection range in `TerminalWidget`; mouse press/drag/release
- [ ] Word (double-click) and line (triple-click) selection
- [ ] Rectangular selection with a modifier
- [ ] Render using the overlay layer and `Palette::selectionBackground`
      (both already exist)
- [ ] Grid→string conversion handling wide characters and trailing blanks
- [ ] `Ctrl+Shift+C`; optional copy-on-select; primary selection on X11
- [ ] `OSC 52` clipboard access

### 3. Mouse reporting 🔍
- [ ] `?1000` (click), `?1002` (drag), `?1003` (any motion)
- [ ] `?1006` SGR extended coordinates
- [ ] `?1004` focus in/out events
- [ ] Pass through to the application when active; keep a modifier for local
      selection

---

## 📋 Backlog

### Terminal emulation
- [ ] Combining marks composed onto their base 🔍 — absorbed correctly today, but
      drawing `e` + U+0301 as `é` needs shaping
- [ ] Text shaping (HarfBuzz) 🔍 — would render joined emoji, flags, keycaps and
      skin-tone variants as their real combined glyphs, and enable ligatures
- [ ] Generate the `Emoji_Presentation` / `Extended_Pictographic` tables from
      `emoji-data.txt` rather than transcribing them 🔍
- [ ] Retain the full grapheme cluster per cell, not just its base code point —
      needed before text selection can copy an emoji sequence intact
- [ ] DEC line-drawing charset (`ESC ( 0`) — currently accepted and ignored
- [ ] Tab stops: `HTS`, `TBC` (tabs are hard-coded to every 8 columns)
- [ ] Origin mode (`DECOM`), left/right margins (`DECLRMM`)
- [ ] Soft reset (`DECSTR`), `DECRQM` mode queries
- [ ] Overline (SGR 53), underline styles and colours (`4:3`, `58`)
- [ ] `OSC 8` hyperlinks; `OSC 13`–`19` (highlight/pointer colours); `OSC 133`
      prompt marks; `OSC 52` clipboard

### Rendering
- [ ] Gamma-correct glyph blending 🔍 — light-on-dark text is currently slightly thin
- [ ] Damage tracking 🔍 — `Screen::revision()` is the hook; per-row dirty flags next
- [ ] Powerline separators drawn geometrically too (box drawing already is), so
      they tile regardless of which font supplies them
- [ ] Cursor-cell text redrawn in the background colour, for an opaque block
      cursor instead of a translucent one
- [ ] Background image / true window transparency
- [ ] Ligatures via HarfBuzz (deliberately low priority for a grid terminal)
- [ ] Sixel and kitty graphics protocols

### UI
- [ ] Persist window geometry 🔍
- [ ] Search within the scrollback (depends on §1)
- [ ] Drag a splitter and have the pty resize live (works, but unthrottled)
- [ ] Move panes between tabs; detach a pane into a new window
- [ ] URL detection and click-to-open
- [ ] Visual bell as an alternative to the audible one
- [ ] Tab context menu: rename, duplicate, close others
- [ ] Config reload without a restart (file watcher)

### Project health
- [ ] Decide the fate of `src/utils/retcodes.h` 🔍 — 273 lines nothing includes
- [ ] `.app` bundle for macOS and a `.desktop` file for Linux
- [ ] CI: build on macOS and Linux, run `ctest`
- [ ] Fuzz `VTParser` against random byte streams
- [ ] A benchmark for throughput (`cat` of a large file) and frame time
- [ ] `clang-format` configuration matching the existing style
- [ ] Test the render layer — needs an offscreen GL context, so currently
      uncovered

---

## Non-goals

- **Windows support.** RaTTY is built on `forkpty` and Unix job control.
- **A configuration language.** JSON, not a scripting runtime.
- **Tmux-style session persistence.** Use tmux.
- **A plugin system.** Not at this size.

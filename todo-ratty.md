# RaTTY — state of the art and roadmap

Last updated after **text selection, the clipboard, scrollback reflow and
scrollback search** landed on top of v0.3.0.

This file tracks what works, what is broken, and what comes next. Items marked
🔍 have a design note in
[doc/known-gaps.md](doc/known-gaps.md) explaining why the gap is
where it is.

---

## Where the project stands

RaTTY is usable as a daily driver for shell work. The rendering path is correct
on HiDPI displays, the VT parser handles what a modern shell and most TUI
applications emit, and the terminal model is separated cleanly enough from Qt and
OpenGL to be tested headlessly.

Scrollback and mouse reporting landed in v0.3.0, along with configurable start
directories, a configuration reload that needs no restart, and per-pane dimming.
Since then the last big gap has closed: text can be **selected** with the mouse
and copied, the scrollback is **rewrapped** on a width change instead of being
truncated, and it can be **searched**. All three rest on one new piece of
information — a bit recording that a row ran into the right margin rather than
ending in a newline — which is what tells a soft wrap from a hard line break
apart.

What is left is smaller and more particular: text shaping, so a joined emoji
draws as one glyph rather than its base; and a keybinding set for Linux that was
written by transliterating the macOS one and deserves a rethink of its own.

```
Foundations ████████████████████ done
VT emulation ███████████████████░ shells, most TUIs, mouse-driven ones included
Rendering    ████████████████████ done (sharp, HiDPI-correct, emoji, box drawing)
UI shell     ███████████████████░ tabs, splits, selection, search
Polish       █████████████████░░░ scrollback, mouse, clipboard done; shaping missing
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
- [x] **Scrollback**: rows leaving the top of the screen are kept in a bounded
      history, with a view offset so rendering reads `history + viewport`. Only
      full-screen scrolls contribute -- a `DECSTBM` region is a subwindow the
      application manages itself -- and the alternate screen keeps none, so a
      `vim` session never ends up in the shell's history. Rows dropped by a
      vertical shrink are kept too. `ED 3` erases the saved lines only (xterm's
      definition, which is a behaviour change: it used to clear the display too);
      `RIS` discards them
- [x] Any output and any keystroke snap the view back to the live screen
- [x] **Wrap seam** (`CellFlagWrapped`): a row that ran into the right margin is
      marked as continuing on the next one, so a soft wrap can be told from a
      hard line break. Carried in the cell at the last column, which is what
      makes it survive the compressed scrollback, a resize and O(1) scrolling for
      free. It is what reflow, search and copying a wrapped command line all
      needed, and what none of them could be written without
- [x] **Reflow on resize**: a width change takes the buffer apart into logical
      lines and lays them out again, history included, so a narrower window
      rewraps old text rather than truncating it and a wider one joins the pieces
      back up. Rows that ended in a newline are never joined, so a table drawn by
      a TUI stays a table; the alternate screen is excluded outright, since what
      is on it is an application's own layout. Streamed rather than materialised
      twice: a full 10 000-line history reflows in about 10 ms and costs one
      logical line of scratch space
- [x] **Stable line numbers**: a line is named by a number that counts from the
      first line ever captured, so a selection anchor or a search result stays on
      its text while output scrolls and while the history evicts underneath it.
      An absolute row index cannot do that, and a view row certainly cannot
- [x] `OSC 52` clipboard access, in both directions, each gated by configuration.
      Writing is on -- it is how an editor's yank reaches the local clipboard over
      ssh -- and reading is off, because it lets anything that can write to the
      terminal exfiltrate whatever was last copied
- [x] **Mouse reporting**: `?9` (X10), `?1000` (click), `?1002` (drag), `?1003`
      (any motion), with `?1005`/`?1006`/`?1015` coordinate encodings, `?1004`
      focus in/out and `?1007` alternate scroll. Disabling a mode only takes
      effect when it names the mode actually in force, since applications enable
      several and reset them one at a time

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
- [x] Custom thin tab bar drawn by RaTTY, in five styles (`minimal`,
      `underline`, `blocks`, `pills`, `powerline`), positioned top or bottom
      (default bottom), sized from the terminal font, with a painted close
      affordance and middle-click close
- [x] Chrome colours derived from the terminal palette, so a theme need not
      restate them; luminance-aware so light themes work too
- [x] Recursive split panes over `QSplitter`, with correct Qt ownership on both
      split and close
- [x] Directional focus movement between panes
- [x] Keybindings that actually fire — the terminal widget defers bound sequences
      to the window instead of swallowing every key
- [x] xterm modifier encoding (`CSI 1;mod A`) and `DECCKM` SS3 forms
- [x] Bracketed paste, `LF`→`CR` translation on paste
- [x] Live font resizing across every pane in every tab
- [x] Two default keybinding files, `keybindings/macos.yaml` (`cmd`) and
      `keybindings/linux.yaml` (`super`), auto-selected by platform and
      overridable with `mac_os_bindings: auto | true | false`; a test asserts the
      two resolve identically so they cannot drift
- [x] Font-size shortcuts covering every key event "plus" and "minus" can arrive
      as, plus a Shift-insensitive fallback for layouts where the digits are the
      shifted symbols
- [x] Cursor styles (block / hollow / underline / bar); blink only when focused
- [x] **Text selection** with the mouse: press / drag / release, double-click for
      a word (path- and URL-shaped, followed across a wrap seam), triple-click for
      a whole logical line, `alt`+drag for a rectangle. A drag held past the top
      or bottom edge keeps scrolling. Highlighted with the theme's
      `selection_background` in the *background* layer, under the glyphs, so the
      selected text keeps its full contrast
- [x] Selection reads as text with the trailer half of a double-width character
      skipped, padding to the window edge dropped, and rows joined across a wrap
      seam -- so a command line that wrapped comes back as the one line a shell
      will accept. The scrollback is selectable, and a selection survives both
      scrolling and a screenful of output
- [x] `cmd`/`super`+`c` copies; `clipboard.copy_on_select` puts a finished
      selection on the clipboard as well; the primary selection is always set
      where the platform has one, which is what middle-click pastes
- [x] **Scrollback search**: a prompt drawn over the bottom row, incremental,
      every match tinted and the current one brighter, `Return`/`Shift+Return` to
      step back and forth, `Escape` to close with the match left selected. Matches
      run across a wrap seam but never across a real line break
- [x] **Scroll-position indicator** on the right edge while the view is scrolled
      back, so it is visible that what is on screen is not live
- [x] Mouse wheel scrolls the scrollback, `Shift+PageUp`/`PageDown` move a page,
      `cmd`/`super`+`k` clears it; fractional trackpad notches accumulate
- [x] The wheel drives a pager on the alternate screen through cursor keys, so
      `less` and `man` scroll
- [x] Shift bypasses an application's mouse grab, so the scrollback and
      middle-click paste stay reachable inside a mouse-driven TUI
- [x] Window title from `OSC`, per-tab

### Project
- [x] Layered **YAML** config: built-in -> bundled resource -> user overlay, with
      per-key optional overrides and a named diagnostic for every failure mode
- [x] A user config overrides only what it states; naming an action releases the
      default keys for it, and editing the inactive keybinding set is reported
- [x] Full 256-colour palette, 16 base colours overridable by name
- [x] `scrollback: lines / multiplier` and `mouse: alternate_scroll` settings
- [x] Eleven built-in colour themes as YAML resources (`ratty-dark`,
      `ratty-light`, `nord`,
      `dracula`, `gruvbox-dark`/`-light`, `solarized-dark`/`-light`,
      `tokyo-night`, `catppuccin-mocha`, `one-dark`), selected by `theme:`
- [x] Colours staged per layer and merged built-in -> theme -> user, so a theme
      plus a per-colour override works in either file order
- [x] `"none"` unbinds a default keybinding
- [x] `scrollback: indicator` and `clipboard: copy_on_select / osc52_write /
      osc52_read` settings
- [x] Headless test suites (terminal / history / reflow / selection / search /
      mouse / input / splits) with `ctest`, plus GL-backed ones for the parts that
      only exist once a pane has a context (`test_select_gl` drives press, drag,
      release, double and triple click, an `alt` drag and the search prompt, and
      reads the result back off the clipboard)
- [x] Warning-clean under `-Wall -Wextra -Wpedantic`
- [x] No pinned compiler path in `CMakeLists.txt`
- [x] Empty placeholder translation units removed

---

## 🐛 Fixed in v0.2.0

Recorded because the causes are instructive; full write-ups in
[doc/notable-bugs.md](doc/notable-bugs.md).

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
- [x] **Splitting a pane blanked the terminal.** Reparenting a `QOpenGLWidget`
      destroys its GL context and calls `initializeGL()` again, and that function
      created the `TerminalSession` -- so every split silently killed the running
      shell and started an empty one in its place. The session is now created
      once; only the renderer is rebuilt, and its resources are released from
      `QOpenGLContext::aboutToBeDestroyed` while the outgoing context is still
      current. Covered by `test_splits_gl`, which needs a real GL context -- the
      reason the offscreen suite could never have caught it.
- [x] **...and splitting still blanked the window inside a tab.** Promoting a new
      root reparents the old page out of the tab widget's stacked layout, and
      `QTabWidget` answers a page leaving by removing its tab -- so the count
      dropped to zero and `installTabRoot()`'s `index >= tabCount()` guard
      returned without inserting anything, leaving the tab widget with no page.
      Testing `SplitContainer` on its own could not see this; the suite now
      drives `MainWindow` with real key events.
- [x] **A third pane crushed the other two into a 100 px strip.** `replaceChild()`
      read back the parent splitter's sizes to preserve its ratio, but the pane
      being split had already been detached -- so the splitter held one widget,
      reported one size, and the `size() == 2` guard skipped `setSizes()`
      entirely. A widget `QSplitter` inserts with no size of its own is clamped
      to its minimum, which is exactly `TerminalWidget`'s 200x100. The tree was
      correct throughout, which is why only a geometry assertion could catch it.
- [x] **Splitting did not focus the new pane, and closing focused nothing.** Both
      halves were the same mistake: the tree surgery set Qt focus, and then
      `installTabRoot()` reparented the tree into the tab widget's stacked
      layout, which clears it. Surgery now moves only RaTTY's own focus marker,
      which survives reparenting, and `MainWindow` applies Qt focus afterwards.
      Closing consults a focus history so the caret returns to the pane the user
      came from, not to the promoted sibling's leftmost leaf.
- [x] **`~` could not be typed at all, and neither could any accent.** `~` is a
      dead key on the Spanish layout (Option+n-tilde on macOS, AltGr+n-tilde on
      Linux): the key event carries no text, and the platform input method
      delivers the composed result as a `QInputMethodEvent` -- to a widget that
      has set `WA_InputMethodEnabled`, which `TerminalWidget` had not. The
      composition was dropped, along with every accent and every input method.
- [x] **Option and AltGr were treated as Meta.** The part of the layout's third
      level that is *not* a dead key (`|`, `@`, `[`, `]`, `€`) arrives as text
      with `Qt::AltModifier` set, and was ESC-prefixed as if it were a Meta key.
      Told apart now by comparing the character against `key()`, which Qt
      reports with no modifiers applied.
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
- [x] **`tree`'s indentation was a field of empty boxes.** It indents with two
      U+00A0 NO-BREAK SPACEs per level, and a *blank* glyph is indistinguishable
      from a *missing* one to a coverage test that demands ink -- a test that has
      to demand ink, since an emoji font's regional indicators are empty shaping
      glyphs. Every font therefore "failed" to cover U+00A0 and the primary
      font's `.notdef` box was drawn instead. Fixed with `isSpaceSeparator()`,
      consulted by both the renderer and `FontManager::rasterize()`.
- [x] **A TUI's file-type icons were empty boxes, while kitty showed them on the
      same machine.** Not a system difference: kitty *ships*
      `SymbolsNerdFontMono-Regular.ttf` inside its app bundle. A scan of all 371
      installed font files, every face, finds only `.LastResort` for U+E8EB and
      U+F375, so no fallback strategy could have found them. RaTTY now bundles the
      same MIT-licensed font as a Qt resource, loaded through
      `FT_New_Memory_Face` and adopted as the last loaded fallback
- [x] **Icon code points drew boxes even when an installed font had them.**
      Charset discovery asked `fc-match`, which never filters: for a code point
      nothing good covers it answers `.LastResort`, a font of empty boxes, and
      refusing that answer ended the search. Now enumerated with
      `fc-list :charset=`, monospaced candidates first
- [x] **A pinned compiler path** (`/opt/homebrew/opt/llvm@20/bin/clang++`) broke
      configuration on any other machine.

---

## 🎯 Next up

Roughly in the order that gives the most user-visible benefit per unit of work.

### 1. Rethink the default keybindings for Linux 🔍
The Linux set was written by transliterating the macOS one — `cmd` became
`super` and nothing else was reconsidered — and it shows. On Linux the habits
are different, and a few of the current choices are actively wrong there:

- [ ] `ctrl+shift+c` is `close_split`, which on Linux is the near-universal
      **copy** shortcut. Copy currently lives on `super+c` instead. That was left
      alone rather than quietly moved, because closing a pane is muscle memory
      too — but as it stands the one binding a Linux user will reach for without
      thinking closes their pane
- [ ] `ctrl+shift+v` is `split_vertical`, which is the same problem for the same
      reason: on Linux that is **paste**
- [ ] `super+<letter>` is the window manager's territory on most desktops
      (GNOME takes `super`, `super+l`, the digits, and more), so a set built on
      `super` is fighting whatever is running. `ctrl+shift+<letter>` is the
      convention for a terminal's own actions
- [ ] Decide whether the two files should stay *equivalent* at all. The test that
      asserts they resolve identically is what has kept them in step — and is
      also what makes fixing the above a two-file change with a test to update.
      Diverging deliberately is fine; diverging by accident is not
- [ ] Whatever is chosen, keep the invariant that no shell control key
      (`Ctrl+C`/`D`/`U`/`W`/`R`/`Z`/`L`/`A`/`E`, `Tab`) is ever taken for an
      application action, which `tests/test_input.cpp` asserts

### 2. Text shaping (HarfBuzz) 🔍
The largest remaining visible gap, and the one several smaller items are waiting
behind: a joined emoji, a flag, a keycap and a skin-tone variant each occupy the
right number of columns and draw their *base* glyph. Combining marks are absorbed
correctly but not composed. Selection copies what the cell holds, which is the
base code point — so retaining the full grapheme cluster per cell (below) is
part of the same piece of work.

---

## 📋 Backlog

### Terminal emulation
- [ ] Combining marks composed onto their base 🔍 — absorbed correctly today, but
      drawing `e` + U+0301 as `é` needs shaping (see *Next up*)
- [ ] Generate the `Emoji_Presentation` / `Extended_Pictographic` tables from
      `emoji-data.txt` rather than transcribing them 🔍
- [ ] Retain the full grapheme cluster per cell, not just its base code point —
      now visible rather than theoretical: copying a joined emoji yields its base
      code point, because that is all the cell holds
- [ ] DEC line-drawing charset (`ESC ( 0`) — currently accepted and ignored
- [ ] Tab stops: `HTS`, `TBC` (tabs are hard-coded to every 8 columns)
- [ ] Origin mode (`DECOM`), left/right margins (`DECLRMM`)
- [ ] Soft reset (`DECSTR`), `DECRQM` mode queries
- [ ] Pixel-resolution mouse reporting (`?1016`), which only graphics protocols
      need
- [ ] Overline (SGR 53), underline styles and colours (`4:3`, `58`)
- [ ] `OSC 8` hyperlinks; `OSC 13`–`19` (highlight/pointer colours); `OSC 133`
      prompt marks

### Rendering
- [ ] Scale a fallback glyph that is wider than its cell. A CJK font's
      private-use glyph is a full square em, so an icon served from one bleeds
      into the next column 🔍
- [ ] Gamma-correct glyph blending 🔍 — light-on-dark text is currently slightly thin
- [ ] Damage tracking 🔍 — `Screen::revision()` is the hook; per-row dirty flags next
- [ ] Powerline separators drawn geometrically too (box drawing already is), so
      they tile regardless of which font supplies them
- [ ] Cursor-cell text redrawn in the background colour, for an opaque block
      cursor instead of a translucent one — the same second pass would let a
      selection restate its foreground colour rather than relying on the theme's
      selection background having enough contrast against the text under it
- [ ] Background image / true window transparency
- [ ] Ligatures via HarfBuzz (deliberately low priority for a grid terminal)
- [ ] Sixel and kitty graphics protocols

### UI
- [ ] Persist window geometry 🔍
- [ ] Drag a splitter and have the pty resize live (works, but unthrottled) —
      now worth more than it was, since a width change rewraps the whole
      scrollback: about 10 ms for a full 10 000-line history, paid per resize
      event rather than per drag
- [ ] Draw the input-method preedit string under the cursor. It is accepted and
      composed correctly, but an in-progress composition is invisible until it
      commits, so a CJK candidate window has nothing to sit against
- [ ] Word- and line-editing keys assume an emacs-mode line editor. A `vi`-mode
      shell, or one that binds the xterm forms itself, would rather have
      `CSI 1;3D` than `ESC b`; there is no way to ask for it
- [ ] Move panes between tabs; detach a pane into a new window
- [ ] URL detection and click-to-open — double-click already selects a URL whole,
      since word selection keeps the punctuation one is made of
- [ ] Search: a regular-expression mode, and case folding beyond ASCII
- [ ] Shift+click to extend an existing selection. Shift is spoken for as the
      mouse-grab override, so this needs a second thought rather than a binding
- [ ] Select-all, and a keybinding to select the output of the last command
      (which needs `OSC 133` prompt marks first)
- [ ] Visual bell as an alternative to the audible one
- [ ] Tab context menu: rename, duplicate, close others
- [ ] A "+" affordance on the tab bar for opening a tab by mouse
- [ ] Config reload without a restart (file watcher)

### Project health
- [ ] Decide the fate of `src/utils/retcodes.h` 🔍 — 273 lines nothing includes
- [ ] `.app` bundle for macOS and a `.desktop` file for Linux
- [ ] CI: build on macOS and Linux, run `ctest`
- [ ] Fuzz `VTParser` against random byte streams
- [ ] A benchmark for throughput (`cat` of a large file) and frame time
- [ ] `clang-format` configuration matching the existing style
- [ ] Re-resolving the font on every split costs a few `fc-match` subprocess
      calls, because the rebuilt renderer starts with an empty font cache. Worth
      a shared resolution cache if it becomes noticeable.
- [ ] Widen `test_splits_gl` to cover tab drag-reorder, which also reparents
- [ ] Test the render layer's drawing output, not just its geometry — would need
      pixel comparison against a reference

---

## Non-goals

- **Windows support.** RaTTY is built on `forkpty` and Unix job control.
- **A configuration language.** JSON, not a scripting runtime.
- **Tmux-style session persistence.** Use tmux.
- **A plugin system.** Not at this size.

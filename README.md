<h1>
<p align="center">
  <img src="resources/images/ratty-logo.ico" alt="Ratty Logo" width="220">
  <br>RaTTY
</h1>
  <p align="center">
    A GPU-accelerated terminal emulator built with modern C++ and OpenGL.
  </p>
</p>

## Overview

RaTTY is a Unix terminal emulator focused on crisp text and GPU-accelerated
rendering. Glyphs are rasterized with FreeType at physical pixel resolution,
packed into a single-channel texture atlas, and drawn as one batched pass — a
whole screen of text is one draw call.

**Platform support**

- ✅ macOS (including HiDPI/Retina)
- ✅ Linux (X11 and Wayland via Qt)
- ❌ Windows — not supported, no plans to add support

### Current status — v0.2.0

Usable for day-to-day shell work. Full-screen TUI applications mostly work;
scrollback and selection do not exist yet.

**Working**

- PTY session management with the user's login shell
- VT100/VT220/xterm escape sequence parsing (see
  [DOCUMENTATION.md](DOCUMENTATION.md#44-terminalemulator) for the exact list)
- 16-colour, 256-colour and 24-bit truecolour, both `;` and `:` SGR forms
- Application-driven theming: `OSC 4`/`10`/`11`/`12` set *and* answer queries, so
  editors can retheme the terminal and detect whether it is light or dark
- Application-driven cursor shape (`DECSCUSR`), as editors use to signal mode
- **Font fallback chain** — code points the main font lacks are served from the
  system monospaced font, and colour emoji from whatever emoji font is installed
- **Colour emoji**, rendered from the emoji font's own bitmaps
- **Emoji presentation selectors** — `U+FE0F` / `U+FE0E` choose between the
  colour emoji and the monochrome text form of a dual-form character
- **Emoji sequences occupy one cell** — joined sequences, skin tones, flags,
  keycaps and tag sequences take two columns, not one per code point
- **Box-drawing characters drawn geometrically**, so TUI borders tile with no
  seams regardless of which font is in use
- Bold, faint, italic, underline, strikethrough, inverse and invisible —
  bold and italic select real font faces, with synthesis when a face is missing
- Deferred (VT-correct) line wrapping, scrolling regions, alternate screen buffer
- Incremental UTF-8 decoding and double-width (CJK/emoji) character layout
- HiDPI-correct rendering: glyphs rasterized at physical pixel size
- Tabs and recursive split panes with directional focus movement
- YAML configuration with a layered override model, custom palette, keybindings
- Bracketed paste, window-title reporting, cursor-position reports
- Live font resizing

**Not yet implemented**

- Scrollback buffer
- Text selection and copy
- Mouse reporting for applications
- Text shaping, so a joined emoji shows its base rather than the combined glyph
- Combining marks composed onto their base, ligatures
- Sixel or kitty graphics

See [todo-ratty.md](todo-ratty.md) for the roadmap and
[DOCUMENTATION.md § 10](DOCUMENTATION.md#10-known-gaps) for why each gap is where
it is.

## Dependencies

- **CMake** ≥ 3.16
- **A C++20 compiler** — Apple Clang 15+, Clang 16+ or GCC 12+
- **Qt6** — Core, Gui, Widgets, OpenGL, OpenGLWidgets
- **FreeType** ≥ 2
- **yaml-cpp** ≥ 0.7 for configuration parsing
- **OpenGL** 3.3 core profile
- **fontconfig** (`fc-match`) — recommended; without it a built-in list of font
  paths is used
- **util** — `forkpty`; part of libc on Linux, `libutil` on macOS/BSD

### macOS

```bash
brew install cmake qt@6 freetype fontconfig yaml-cpp
```

### Linux

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake qt6-base-dev libfreetype6-dev \
                 libgl1-mesa-dev fontconfig libyaml-cpp-dev

# Fedora
sudo dnf install cmake gcc-c++ qt6-qtbase-devel freetype-devel \
                 mesa-libGL-devel fontconfig yaml-cpp-devel

# Arch
sudo pacman -S cmake gcc qt6-base freetype2 fontconfig yaml-cpp
```

## Building

```bash
git clone <repository-url> ratty
cd ratty
cmake -S . -B build
cmake --build build -j
./build/ratty
```

`CMAKE_BUILD_TYPE` defaults to `Release`. To install:

```bash
cmake --install build --prefix /usr/local
```

### Running the tests

```bash
cmake -S . -B build -DRATTY_BUILD_TESTS=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

The suites cover terminal semantics, key encoding, pane-tree surgery, the render
layer's geometry and font resolution, and configuration loading. They run headless
(`QT_QPA_PLATFORM=offscreen`) and need no GPU.

## Configuration

RaTTY reads `~/.config/ratty/config.yaml`. Every key is optional — the file is an
*overlay* on the built-in defaults, so you only write what you want to change:

```yaml
font:
  size: 15
colors:
  background: "#101418"
```

That is a complete, valid config. The full set of keys:

```yaml
font:
  # Tried in order; the first installed family wins. If none are installed,
  # RaTTY uses the font the system has set as its monospaced default.
  family:
    - DroidSansMono Nerd Font
    - JetBrains Mono
  # For characters the main font lacks. Rarely needed - the system monospaced
  # font and any installed colour emoji font are already used.
  fallback: []
  size: 13

cursor:
  style: block        # block | hollow | underline | bar
  blink: true

colors:
  background: "#1e1e1e"
  foreground: "#dcdcdc"
  cursor: "#dcdcdc"
  selection_background: "#6495ed80"

  black:          "#000000"
  red:            "#cd3131"
  green:          "#0dbc79"
  yellow:         "#e5e510"
  blue:           "#2472c8"
  magenta:        "#bc3fbc"
  cyan:           "#11a8cd"
  white:          "#e5e5e5"
  bright_black:   "#666666"
  bright_red:     "#f14c4c"
  bright_green:   "#23d18b"
  bright_yellow:  "#f5f543"
  bright_blue:    "#3b8eea"
  bright_magenta: "#d670d6"
  bright_cyan:    "#29b8db"
  bright_white:   "#ffffff"

window:
  width: 1280
  height: 720
  padding: 4          # gap in logical pixels between text and window edge
  opacity: 1.0
  fullscreen: false

mac_os_bindings: auto     # auto | true | false

# Only the active set is read; `auto` picks by platform.
keybindings_macos:
  cmd+t: new_tab
  cmd+d: split_horizontal
  cmd+k: none             # removes a default binding

keybindings:
  ctrl+shift+t: new_tab
  ctrl+shift+e: split_horizontal
  ctrl+shift+k: none
```

> **Quote your colours.** In YAML `#` starts a comment, so `background: #101418`
> is an empty value, not a colour. RaTTY notices and tells you, but the quotes
> are the fix.

Notes:

- **`font.family`** accepts a single name or an array of names tried in order.
  The default is `DroidSansMono Nerd Font`; if it is not installed, RaTTY uses
  whatever font the system has configured as its monospaced default. A requested
  family only counts if it is genuinely installed — fontconfig will happily
  substitute a *proportional* font for a name it does not recognise, and RaTTY
  rejects any face whose `i` and `W` have different advances.
- **`font.fallback`** lists families to consult for characters the main font does
  not have. You rarely need to set it: RaTTY already falls back to the system
  monospaced font and to any installed colour-emoji font. It matters if you want
  a *specific* font to supply, say, box drawing.
- **`window.padding`** is the gap in logical pixels between the text and the
  window edge. Default 4; set `0` for flush-to-the-edge text.
- Cursor styles: `block`, `hollow`, `underline`, `bar`. An application's
  `DECSCUSR` request takes precedence while it is in effect.
- Binding an action to `"none"` removes a default binding.
- The bundled defaults live in
  [`src/config/default_config.yaml`](src/config/default_config.yaml), which is
  commented and worth reading.

### About patched "Nerd Fonts"

A patched icon font typically adds Powerline separators and file-type icons to an
existing family — but it does not add anything that family was already missing.
`DroidSansMono Nerd Font`, for instance, has twelve thousand glyphs and **no
box-drawing characters**, because Droid Sans Mono never had them. Since every TUI
draws its borders from those, such a font on its own renders a full-screen editor
as a field of empty boxes.

RaTTY handles this in two ways, so it is not something you need to configure:
box-drawing and block characters are drawn geometrically from the cell (which
also makes them tile perfectly), and anything else missing is taken from the
fallback chain.

### Matching a colour scheme

Terminal applications draw most of the screen by *erasing* it and relying on the
terminal's own default background — that is a normal optimisation, not a bug. So
if your editor's theme has a background of, say, `#1f1f26`, set the same value in
RaTTY:

```yaml
colors:
  background: "#1f1f26"
```

RaTTY answers `OSC 11` queries, so Neovim and friends can also read that value
back and pick their light or dark variant accordingly.

### Default keybindings

There are two sets, and exactly one is active. `mac_os_bindings` decides which:

```yaml
mac_os_bindings: auto     # auto (default) | true | false
```

`auto` follows the platform — the Command set on macOS, the Ctrl+Shift set
elsewhere. Only the active set is read, so on macOS edit `keybindings_macos` and
elsewhere edit `keybindings`.

| Action | macOS | Linux / Windows |
|---|---|---|
| New tab | `⌘T` | `Ctrl+Shift+T` |
| Close tab | `⌘W` | `Ctrl+Shift+W` |
| Next / previous tab | `⌘⇧→` / `⌘⇧←` | `Ctrl+Shift+→` / `←` |
| Go to tab *n* | `⌘1`…`⌘9` | `Ctrl+Shift+1`…`9` |
| Split left/right | `⌘D` | `Ctrl+Shift+E` or `Ctrl+Shift+\` |
| Split top/bottom | `⌘⇧D` | `Ctrl+Shift+O` |
| Close pane | `⌘⇧W` | `Ctrl+Shift+D` |
| Move focus between panes | `⌘⌥↑` / `⌘⌥↓` | `Ctrl+Shift+↑` / `↓` |
| Copy / paste | `⌘C` / `⌘V` | `Ctrl+Shift+C` / `V` |
| Increase font size | `⌘+` / `⌘=` | `Ctrl+Shift++` |
| Decrease font size | `⌘-` | `Ctrl+Shift+-` |
| Reset font size | `⌘0` | `Ctrl+Shift+0` |
| Clear scrollback | `⌘K` | `Ctrl+Shift+K` |
| Quit | `⌘Q` | `Ctrl+Shift+Q` |
| Toggle fullscreen | `⌘⌃F` or `F11` | `F11` |

Either way the shell keeps every plain `Ctrl` key: `Ctrl+C`, `Ctrl+D`, `Ctrl+W`,
`Ctrl+R`, `Ctrl+Z`, `Ctrl+L` and the rest reach the program running in the
terminal, not RaTTY.

In a config file `cmd` is always the Command key and `ctrl` is always the
physical Control key. Qt normally swaps the two on macOS, which would make
`Command+C` send an interrupt instead of copying; RaTTY turns that off so both
modifiers mean the same thing on every platform.

Key names accept `ctrl`, `shift`, `alt`/`option`, `meta`/`super`/`cmd`, named
keys (`up`, `pageup`, `escape`, `f1`…`f12`) and spelled-out punctuation
(`plus`, `minus`, `underscore`, `backslash`, `bracketleft`, …).

Shifted keys are matched tolerantly: Qt reports either the digit or the shifted
symbol for the same physical key depending on platform and layout, so
`ctrl+shift+1` fires whether the event arrives as `1` or `!`. Bindings on letter
keys are still the most portable choice.

## Architecture at a glance

```
src/
├── core/      terminal model — no OpenGL, no widgets
│              Cell, Palette, Screen, VTParser, TerminalEmulator,
│              TerminalSession, PTY, UTF-8, char widths
├── render/    FontManager (+ fallback chain), GlyphAtlas, GLRenderer,
│              TerminalRenderer, box_drawing
├── ui/        TerminalWidget, SplitContainer, MainWindow, InputHandler
└── config/    Config (layered YAML)
```

The layering is deliberate and enforced by convention: `core/` includes no
OpenGL or QtWidgets header, which is why terminal behaviour is testable without a
GPU. `render/` never reads `Config` — it is handed a palette and a layout.

[DOCUMENTATION.md](DOCUMENTATION.md) covers the internals: the data flow from pty
bytes to pixels, the glyph atlas, the HiDPI rules that keep text sharp, and the
Qt ownership hazards in the pane tree.

## Contributing

- Match the surrounding style: 4-space indent, `snake_case` files,
  `PascalCase` types, `camelCase_` private members.
- Keep the layering intact. If `core/` needs something from `render/`, the design
  is wrong.
- Add a test when you fix a behavioural bug. `tests/check.h` is deliberately
  tiny; there is no framework to learn.
- Builds must stay warning-clean under `-Wall -Wextra -Wpedantic`.

## License

See [LICENSE](LICENSE).

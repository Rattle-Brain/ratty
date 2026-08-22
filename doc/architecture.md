# Architecture


```
                     ┌──────────────────────────────────┐
   src/ui/           │ MainWindow                       │  tabs, shortcuts
                     │   ├─ TerminalCanvas              │  ONE GPU surface
                     │   │                              │  per window
                     │   └─ SplitContainer (tree)       │  pane layout
                     │        └─ TerminalWidget         │  QWidget: state and
                     └───────────┬──────────┬───────────┘  events, never paints
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
                └─────────────┴────────────┘              ├─────────┼──────────┤
                                                          │ History │ PTY      │
   src/config/  ┌──────────────────────────┐              │ packed  │ Palette  │
                │ Config (singleton)       │              │ rows    │ Cell     │
                └──────────────────────────┘              ├─────────┼──────────┤
                                                          │Selection│ Search   │
                                                          │ ranges  │ matching │
                                                          └─────────┴──────────┘
```

The one structural thing to know: **a pane is not a GPU surface.** Every pane in
a window draws through the single `TerminalCanvas`, which owns the only
`GLRenderer` and the only `GlyphAtlas` in the process. `TerminalWidget` is a
plain `QWidget` that holds terminal state and handles events but never paints —
it is stacked *under* the canvas, and exists so that `QSplitter` keeps doing the
layout and Qt keeps doing focus, the keyboard and input methods. See
[rendering](rendering.md#one-surface-per-window) for why.

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
| `core/screen.h/.cpp` | The grid, cursor, pending-wrap flag, scrolling region, editing operations, the scrollback with its view offset, stable line numbers, and reflow on a width change. |
| `core/history.h/.cpp` | One scrollback row, compressed: trailing blanks dropped, attributes run-length encoded, characters narrowed. |
| `core/selection.h/.cpp` | Selection ranges in stable line numbers, word and line expansion, highlight geometry, and grid→string conversion. No mouse, no Qt. |
| `core/search.h/.cpp` | Scrollback search over *logical* lines, returning matches as selection ranges. |
| `core/base64.h` | The one encoding the terminal protocol needs, for `OSC 52`. |
| `core/vt_parser.h/.cpp` | ECMA-48 state machine. Emits parsed sequences to a `VTHandler`. |
| `core/terminal_emulator.h/.cpp` | Implements `VTHandler`; owns the pen, the primary and alternate screens, and DEC modes. |
| `core/terminal_session.h/.cpp` | Owns the pty, the socket notifier and the byte pump. Emits Qt signals. |
| `core/pty.h/.cpp` | `forkpty` wrapper: shell lookup, environment, resize, teardown. |
| `core/utf8.h` | Incremental UTF-8 decoder (survives chunk boundaries) and encoder. |
| `core/unicode.h` | Column widths, and the emoji properties that decide presentation. |
| `core/cursor.h` | `CursorStyle`, shared by config and renderer. |
| `core/mouse.h/.cpp` | Mouse tracking and encoding modes, and the report wire format. |
| `render/font_manager.h/.cpp` | FreeType faces per style, plus the fallback chain (including the bundled symbols font); rasterizes at an explicit pixel size. |
| `render/box_drawing.h/.cpp` | Geometric line and block glyphs (U+2500–U+259F). |
| `render/glyph_atlas.h/.cpp` | Single `GL_RGBA8` texture, shelf packing, glyph cache. |
| `render/gl_renderer.h/.cpp` | Layered vertex batching, shaders, orthographic projection. |
| `render/terminal_renderer.h/.cpp` | Grid geometry, the grid→draw-call loop, selection and search highlights, the search prompt and the scroll indicator. |
| `ui/terminal_widget.h/.cpp` | One pane: session, layout, events, IME, the selection drag state machine and the search prompt's keyboard. A plain `QWidget` — it owns no GPU surface and does not paint. |
| `ui/terminal_canvas.h/.cpp` | The single GL surface per window. Draws every pane into its own viewport; forwards mouse events to the widgets underneath. |
| `ui/split_container.h/.cpp` | Binary pane tree over `QSplitter`. |
| `ui/main_window.h/.cpp` | Tabs, shortcut dispatch, window title. |
| `ui/tab_bar.h/.cpp` | The self-drawn tab bar, and the `QTabWidget` that hosts it. |
| `config/chrome.h/.cpp` | Chrome colours, derived from the palette when unset. |
| `config/theme.h/.cpp` | The theme catalogue, and the staged `PaletteOverrides`. |
| `ui/input_handler.h/.cpp` | Qt key events → VT input bytes. |
| `config/config.h/.cpp` | Layered YAML settings and keybindings. |

---


# Architecture


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


# ✅ **ROADMAP — from zero to full terminal emulator**

## **PHASE 0 — Foundation (Before you write code)**

These decisions prevent disasters later.

### **0.1 Choose rendering backend**

Pick **one**:

* **OpenGL 3.3** (recommended: fast, portable, stable)
* Vulkan (overkill)
* CPU-only (slower, harder for HiDPI)

💡 Kitty uses OpenGL.

### **0.2 Choose dependency strategy**

Ideal:

* **FreeType** (font rasterization)
* **HarfBuzz** (text shaping, ligatures — optional early)
* **libuv** or epoll/kqueue (event loop)
* **cairo/pango** (optional if going CPU rendering route)
* **yaml-cpp / inih** for config

---

# ✅ **PHASE 1 — Minimal Terminal (MVP, just to see text)**

**Goal:** Show characters from a PTY on the screen.

### **1. Open a PTY**

Implement:

* `forkpty()` on Linux/macOS
* Set raw mode
* Spawn `/bin/bash` or user’s shell
* Nonblocking read/write

### **2. Build event loop**

Single-threaded:

* Poll PTY for input
* Poll GUI window for input
* Poll timers (blink cursor)

Use:

* `select()` initially
* Upgrade to epoll/kqueue later

### **3. Create a window**

Using OpenGL or SDL2 or GLFW:

* Create window
* Create GL context
* Handle key events
* Render plain colored rectangle

### **4. Load a font**

With FreeType:

* Load font face
* Rasterize ASCII glyphs (no shaping yet)
* Create simple atlas texture
* Render text via textured quads

### **5. Display PTY output**

* Read bytes from PTY
* Append them to a 2D char grid buffer
* Render as basic grid of glyphs
* Handle newline, backspace

**This is your first working terminal.**
It’s extremely basic but proves the architecture.

---

# ✅ **PHASE 2 — Actual Terminal Emulation**

**This is where it becomes a *real* terminal.**

### **6. Implement a VT escape sequence parser**

Support:

* CSI (cursor movement)
* OSC (title setting)
* SGR (colors)
* DEC private modes
* Alternate screen buffer
* Insert/delete lines
* Clearing regions

This is 50% of terminal complexity.

### **7. Add scrollback**

* Implement a scrollback ring buffer
* Reflow (optional early)

### **8. Add color support**

* 16-color
* 256-color
* TrueColor (24-bit)

### **9. Add basic mouse support**

* xterm mouse reporting
* selection (click + drag)

---

# ✅ **PHASE 3 — Rendering Improvements**

### **10. Add glyph caching**

* Cache rasterized glyphs
* Rebuild atlas dynamically

### **11. HiDPI support**

* Detect DPI
* Scale fonts
* Adjust cursor/render scale

### **12. Add bold/italic/underline**

Via:

* separate font faces
* or synthetic styling

### **13. Add blinking cursor + shapes**

Block
Underline
Beam

---

# ✅ **PHASE 4 — Config System + Shortcuts**

### **14. Implement config loader**

Use:

* YAML
* TOML
* INI

Support:

* Colors
* Font
* Keybindings
* Layout settings

### **15. Add keybinding engine**

Map:

* Ctrl+Shift+Enter
* Ctrl+Alt+Arrows
* etc.

---

# ✅ **PHASE 5 — Multiplexer (splits/tabs)**

This is Kitty’s “killer feature”.

### **16. Split-window layout engine**

Support:

* horizontal split
* vertical split
* dynamic resize

### **17. Add tab support**

* List of tab objects
* Tab switching
* Tab naming (OSC 2)

### **18. Per-split PTY**

Each split must have its own PTY and its own terminal emulator instance.

---

# ✅ **PHASE 6 — Performance Optimization**

### **19. Partial redraw / dirty rectangles**

Never redraw full screen unless needed.

### **20. Faster Unicode**

* Grapheme cluster handling
* width tables
* combining characters

### **21. Improve scrollback memory**

Switch from:

* vector<string>
  to:
* ring buffer of glyph rows
* compressed history

---

# ✅ **PHASE 7 — Kitty-level Features**

### **22. True ligatures (HarfBuzz)**

Complex, but beautiful.

### **23. Sprite-based animations**

Kitty supports animated cursors and emoji.

### **24. Image protocol or Sixel**

Your choice.

### **25. GPU-based text rendering pipeline**

For ultra-low-latency drawing.

---

# 🌲 **Directory Structure (Ideal, Scalable)**

```
terminal/
├── src/
│   ├── core/
│   │   ├── pty.cpp
│   │   ├── event_loop.cpp
│   │   ├── terminal_state.cpp      # screen buffer + scrollback
│   │   ├── vt_parser.cpp
│   │   ├── input.cpp
│   │   └── clipboard.cpp
│   │
│   ├── render/
│   │   ├── renderer.cpp            # high level render loop
│   │   ├── gl_renderer.cpp         # OpenGL code
│   │   ├── glyph_cache.cpp
│   │   ├── font.cpp                # FreeType loading
│   │   ├── atlas.cpp
│   │   └── shaders/
│   │       ├── text.vert
│   │       └── text.frag
│   │
│   ├── ui/
│   │   ├── window.cpp              # GLFW/SDL code
│   │   ├── tabs.cpp
│   │   ├── splits.cpp
│   │   └── keybindings.cpp
│   │
│   ├── config/
│   │   ├── config.cpp
│   │   └── default_config.yaml
│   │
│   ├── utils/
│   │   ├── logging.cpp
│   │   ├── utf8.cpp
│   │   └── unicode_width.cpp
│   │
│   └── main.cpp
│
├── include/
│   ├── pty.hpp
│   ├── renderer.hpp
│   ├── font.hpp
│   ├── vt_parser.hpp
│   ├── terminal_state.hpp
│   ├── config.hpp
│   ├── window.hpp
│   └── utils.hpp
│
├── third_party/
│   ├── freetype/
│   ├── harfbuzz/
│   └── yaml-cpp/
│
├── assets/
│   ├── fonts/
│   └── themes/
│
├── tests/
│
└── CMakeLists.txt
```

This structure avoids spaghetti, scales well, and supports multiple render backends later.


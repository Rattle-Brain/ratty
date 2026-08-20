# Building and testing


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
| `test_splits_gl` | splits and closes against a **real GL context**, both directly on `SplitContainer` and through `MainWindow` with real key events: the shell survives reparenting, the tab keeps its page, and every pane still draws. Skips itself when no context is available |
| `test_config` | the real load path against a sandboxed HOME: overlay semantics, colours, keybinding add/remove, `mac_os_bindings` resolution, every shipped theme's completeness and chrome coherence, theme-versus-override precedence in both file orders, the unquoted-colour trap, malformed files, clamping |
| `test_tabbar` | style and position parsing, chrome derivation on dark *and light* palettes, bar thinness, tab metrics, and that every style paints something |
| `test_render` | grid padding maths, box-drawing tiling, fallback coverage of the characters a TUI draws, text-vs-emoji font selection, font preference order, and the guarantee that no resolution path yields a proportional font |

All but one run under `QT_QPA_PLATFORM=offscreen` and need no GPU.
`tests/check.h` is a three-function harness, not a framework.

`test_splits_gl` is the exception, and the reason it exists is worth stating
twice over. The offscreen platform cannot create an OpenGL context, so
`test_splits` could never have caught a bug in what happens to a pane's context
when it is reparented. And testing `SplitContainer` in isolation could not catch
what a `QTabWidget` does when its page is reparented away. Two separate bugs hid
in exactly those two blind spots, so the suite now drives the **whole** path:
`MainWindow`, real key events, a real context, and a pixel check that the panes
are not blank. It skips itself, rather than failing, when no context can be
created, so a headless CI run stays green.

One CMake subtlety, since it caused a confusing failure: a `.qrc` compiled into a
**static** library registers itself from a global initializer that the linker
discards, because nothing references it — the resources then silently do not
exist. `RATTY_RESOURCES_ABS` is therefore compiled into each executable rather
than into `ratty_lib`.

---


# Building and testing


### Dependencies

- CMake ≥ 3.16
- A C++20 compiler (Apple Clang 15+, Clang 16+, GCC 12+)
- Qt 6.2 or newer — Core, Gui, Widgets, OpenGL, OpenGLWidgets. Nothing here
  requires a later Qt on purpose: 6.2 is what the oldest supported LTS distros
  ship, and newer APIs are used behind a `QT_VERSION` check or not at all
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
| `test_render` | grid padding maths, box-drawing tiling, fallback coverage of the characters a TUI draws, text-vs-emoji font selection, font preference order, and the guarantee that no resolution path yields a proportional font. Needs a colour emoji font installed (`fonts-noto-color-emoji` on Debian), or the emoji-presentation cases fail |
| `test_history` | that the compressed scrollback encoding is lossless for every kind of cell — truecolour, indexed, every rendition flag, CJK, astral-plane emoji, a NUL code point, a full-width coloured bar — that it is never larger than the raw cells, and that `Screen` still reads history back correctly |
| `test_scroll_gl` | the scrollback view against a real GL context |
| `test_canvas_input` | that mouse input survives the shared canvas: dragging a divider and clicking a pane both work through the native window stacked over them, and the canvas does not cover the tab bar. Skips itself when no context is available |

Most run under `QT_QPA_PLATFORM=offscreen` and need no GPU; the three GL suites
(`test_splits_gl`, `test_scroll_gl`, `test_canvas_input`) use the real platform
plugin and skip themselves when no context can be created.
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

### Checking the Linux build

macOS will not catch Linux breakage: libc++ pulls in transitive includes that
libstdc++ does not, and Homebrew's Qt is many versions ahead of what any LTS
distro ships. Both have broken the build before. A container is enough:

```bash
docker run --rm -v "$PWD":/src:ro -w /tmp debian:bookworm bash -c '
  apt-get update && apt-get install -y build-essential cmake ninja-build \
      qt6-base-dev libgl1-mesa-dev libfreetype6-dev libfontconfig1-dev \
      libyaml-cpp-dev xvfb libgl1-mesa-dri fonts-dejavu-core fonts-noto-color-emoji
  cp -r /src /tmp/ratty && cd /tmp/ratty && rm -rf build
  cmake -S . -B build -G Ninja -DRATTY_BUILD_TESTS=ON && cmake --build build -j
  Xvfb :99 -screen 0 1600x1000x24 & sleep 3
  DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 QT_QPA_PLATFORM=xcb ctest --test-dir build'
```

Mesa's `llvmpipe` provides OpenGL 4.5 in software, so the GL suites run for real
rather than skipping — which is the point, since the shared canvas is a native
child window and that is exactly what differs between platforms.

Two things to know if it fails to configure:

- Debian's yaml-cpp 0.7 exports its target as `yaml-cpp`, not
  `yaml-cpp::yaml-cpp`. Newer versions export both.
- Debian bookworm ships Qt **6.4.2**, Debian trixie 6.8, Ubuntu 24.04 LTS 6.4.2.
  Anything newer than 6.2 that the code wants has to be version-guarded.

One CMake subtlety, since it caused a confusing failure: a `.qrc` compiled into a
**static** library registers itself from a global initializer that the linker
discards, because nothing references it — the resources then silently do not
exist. `RATTY_RESOURCES_ABS` is therefore compiled into each executable rather
than into `ratty_lib`.

---


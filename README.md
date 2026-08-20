<h1>
<p align="center">
  <img src="resources/images/ratty-logo.ico" alt="RaTTY" width="200">
  <br>RaTTY
</h1>
<p align="center">
  <em>A GPU-accelerated terminal emulator for people who like Unix.</em>
</p>

---

RaTTY draws text with FreeType and OpenGL: glyphs are rasterized at physical
pixel resolution, packed into one texture atlas, and drawn in a single batched
pass. A whole screen of text is one draw call. It has tabs, split panes,
scrollback, mouse support for the applications that want it, colour themes,
truecolour, colour emoji, a bundled Nerd Fonts symbols font so a TUI's file-type
icons render on a bare machine, and a tab bar thin enough not to insult the
terminal it sits next to.

It is a rat, and it speaks TTY. The name was not agonised over.

## Platform support

| | | |
|---|---|---|
| ✅ | **macOS** | Retina-correct, and where it gets used daily |
| ✅ | **Linux** | X11 and Wayland, through Qt |
| ❌ | **Windows** | No. |

That last row is not a "not yet". RaTTY is built on `forkpty`, POSIX job control,
process groups and signals — Windows has none of them. Its console is a
genuinely different model wearing a terminal costume, and ConPTY papers over the
gap without closing it. Supporting it would mean writing a second program that
happens to share a logo.

Windows already ships a perfectly good terminal. Use that one, with my blessing
and a faint sense of pity.

## Quick start

```bash
brew install cmake qt@6 freetype fontconfig yaml-cpp     # macOS
# sudo apt install build-essential cmake qt6-base-dev libfreetype6-dev \
#                  libgl1-mesa-dev fontconfig libyaml-cpp-dev          # Debian

cmake -S . -B build
cmake --build build -j
./build/ratty
```

Note it is `cmake --build`, not bare `make` — there is no committed Makefile.
Full details, including how to run the test suites, are in
[doc/building.md](doc/building.md).

## Configuring it

Drop a file at `~/.config/ratty/config.yaml`. It is an *overlay* on the defaults,
so you write only what you want to change:

```yaml
theme: gruvbox-dark
font:
  size: 15
scrollback:
  lines: 20000
tab_bar:
  style: pills
  position: bottom
```

That is a complete, valid config. Ten themes ship with it, keybindings pick
themselves based on your OS, and everything you do not mention keeps its default.

> **One YAML trap:** `#` starts a comment, so hex colours need quoting —
> `background: "#1e1e1e"`. RaTTY notices and tells you, but the quotes are the fix.

See [doc/configuration.md](doc/configuration.md) for every setting and
[doc/keybindings.md](doc/keybindings.md) for the shortcuts.

## Documentation

Everything lives in [**doc/**](doc/index.md):

- [Overview](doc/overview.md) — what it is, and the rules the code follows
- [Architecture](doc/architecture.md) — layers, dependencies, file map
- [Lifecycle](doc/lifecycle.md) — bytes to pixels, keystrokes to bytes
- [Terminal emulation](doc/terminal-emulation.md) — the grid, the parser, the sequences
- [Rendering](doc/rendering.md) — fonts, atlas, HiDPI, emoji, box drawing
- [Widgets, panes and the tab bar](doc/ui.md)
- [Configuration](doc/configuration.md) · [Keybindings](doc/keybindings.md)
- [Platform notes](doc/platform-notes.md) — where the operating systems disagree
- [Building and testing](doc/building.md)
- [Known gaps](doc/known-gaps.md) — what is missing, and why
- [Bugs worth understanding](doc/notable-bugs.md) — post-mortems of the interesting ones

Planned work lives in [todo-ratty.md](todo-ratty.md).

## Contributing

Patches welcome. A few things that will make yours easy to accept:

- **Match the surrounding style.** 4-space indent, `snake_case` filenames,
  `PascalCase` types, `camelCase_` private members.
- **Keep the layering intact.** `src/core/` includes no OpenGL and no QtWidgets —
  that is what makes terminal behaviour testable without a GPU. If `core/` seems
  to need something from `render/`, the design is wrong, not the rule.
- **Add a test when you fix a behavioural bug.** `tests/check.h` is three
  functions and no framework, so there is nothing to learn first.
- **Builds stay warning-clean** under `-Wall -Wextra -Wpedantic`.
- **Explain *why* in comments**, not what. The code already says what.

If you are fixing something in the UI, please run it and look at it. Two bugs in
this repo's history passed their tests and were still visibly broken, and both are
written up in [doc/notable-bugs.md](doc/notable-bugs.md) so the next person does
not repeat them.

## Licence

See [LICENSE](LICENSE).

`resources/fonts/SymbolsNerdFontMono-Regular.ttf` is from
[Nerd Fonts](https://github.com/ryanoasis/nerd-fonts) v3.5.0, MIT licensed
(© Ryan L McIntyre); its licence travels with it in
[`resources/fonts/LICENSE-SymbolsNerdFont.txt`](resources/fonts/LICENSE-SymbolsNerdFont.txt).
It is bundled for the same reason kitty and Ghostty bundle it: a TUI's file-type
icons are private-use code points that no stock system font carries.

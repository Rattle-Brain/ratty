# RaTTY documentation

Start with the [overview](overview.md); the rest can be read in any order.

## Understanding it

| Document | What is in it |
|---|---|
| [Overview](overview.md) | What RaTTY is, and the four design rules the code follows |
| [Architecture](architecture.md) | The layers, the dependency rules between them, and a map of every file |
| [Lifecycle](lifecycle.md) | How a byte from the shell becomes a pixel, how a keystroke becomes a byte, and the OpenGL context lifetime that trips people up |

## The parts

| Document | What is in it |
|---|---|
| [Terminal emulation](terminal-emulation.md) | The grid, the parser, the escape sequences supported, grapheme clustering |
| [Rendering](rendering.md) | Fonts and fallback, the glyph atlas, HiDPI correctness, box drawing, colour emoji |
| [Widgets, panes and the tab bar](ui.md) | The pane tree, the self-drawn tab bar, chrome colours |

## Using it

| Document | What is in it |
|---|---|
| [Configuration](configuration.md) | The YAML format, how layering works, every setting |
| [Default configuration](default-config.md) | What ships in `default_config.yaml`, and why each default is what it is |
| [Keybindings](keybindings.md) | The two platform sets, key naming, layout tolerance |
| [Platform notes](platform-notes.md) | Where macOS, Linux and Windows differ — and where a platform lies to you |
| [Building and testing](building.md) | Dependencies, the build, and how to run the suites |

## Honest limits

| Document | What is in it |
|---|---|
| [Known gaps](known-gaps.md) | What is missing, and why each gap is where it is |
| [Bugs worth understanding](notable-bugs.md) | Post-mortems of the defects that were interesting rather than merely annoying |

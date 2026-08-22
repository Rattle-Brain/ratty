# Overview

RaTTY is a GPU-accelerated terminal emulator for Unix, written in C++20 with Qt6
and OpenGL. Glyphs are rasterized with FreeType at physical pixel resolution,
packed into one texture atlas, and drawn in a single batched pass — a whole
screen of text is one draw call.

This document is the "why it is shaped like this" companion to the rest of
[`doc/`](index.md). Start here, then follow whichever thread you need.

## What works

- PTY session management with the user's login shell
- VT100/VT220/xterm escape sequences ([details](terminal-emulation.md))
- 16-colour, 256-colour and 24-bit truecolour, in both SGR spellings
- Bold, faint, italic, underline, strike, inverse, invisible — bold and italic
  select real font faces
- Deferred (VT-correct) line wrapping, scrolling regions, alternate screen
- Incremental UTF-8 decoding; double-width characters; colour emoji; emoji
  presentation selectors and multi-code-point sequences
- Box drawing rendered geometrically, so borders tile exactly
- A bundled Nerd Fonts symbols font, so a TUI's file-type icons render on a
  machine with no icon font installed
- Scrollback, with the wheel, `Shift+PageUp`/`PageDown` and a configurable limit —
  rewrapped on a width change, searchable, and with a position indicator while it
  is scrolled back
- Text selection with the mouse (word, line and rectangular), copy and paste, the
  primary selection where the platform has one, and `OSC 52` so a program across
  an ssh connection can reach the local clipboard
- Mouse reporting for applications (`?1000`/`?1002`/`?1003`, SGR and legacy
  encodings), focus events, and alternate scroll so the wheel drives a pager
- HiDPI-correct rendering ([why that is hard](rendering.md#physical-pixels-and-why-it-matters))
- Tabs, recursive split panes, a self-drawn tab bar in five styles — every pane
  in a window shares one GPU surface, so a split costs about a megabyte of
  terminal state and no graphics memory at all
  ([why](rendering.md#one-surface-per-window))
- Layered YAML configuration, ten colour themes, platform keybinding sets

## What does not

Text shaping, so a joined emoji draws its base glyph and a selection copies the
base code point rather than the whole cluster. Scrollback search folds case for
ASCII only and matches literal text. And the default Linux keybindings are the
macOS ones with `cmd` spelled `super`, which puts two actions where that platform
expects copy and paste — capability is not the problem, habit is. See
[known gaps](known-gaps.md) for the full list and, more usefully, *why* each one is
where it is.

## Design principles

Four rules shape the code. Most of the structure follows from them.

**The core knows nothing about pixels.** Everything under `src/core/` models a
terminal: a grid of cells, a cursor, an escape-sequence parser. It does not
include a single OpenGL or QtWidgets header. That is why the terminal test suite
runs in a fraction of a second with no GPU, no window and no shell.

**Syntax and semantics are separate.** `VTParser` recognises the *shape* of an
escape sequence and nothing else. `TerminalEmulator` decides what a recognised
sequence *means*. The parser holds no terminal state; earlier revisions had it
tracking the current text attributes, which meant terminal state lived in two
places and drifted.

**One source of truth per fact.** The default foreground colour is defined in
exactly one place (`Palette`). Cells store *symbolic* colours — "default",
"palette slot 208", "this RGB triple" — and only the palette turns them into
pixels. When three separate classes each hard-coded their own idea of "default
background", a custom background in the config made every cell paint an
opaque rectangle in the old colour.

**Physical pixels everywhere in the renderer.** No part of the drawing code
works in logical/device-independent units. This is not stylistic: mixing the two
is exactly what made text blurry (see [bugs worth understanding](notable-bugs.md)).

---


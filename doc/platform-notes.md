# Platform notes

Everything here is a place where the same code has to behave differently, or
where a platform tells a convenient lie. Each one cost real debugging time, so
they are written down.

## macOS

### Qt swaps Ctrl and Command

By default `Qt::ControlModifier` means the **Command** key on macOS and
`Qt::MetaModifier` means the physical **Control** key. For a terminal that is
backwards in the worst possible way: `InputHandler` maps `Qt::ControlModifier` to
the C0 control characters, so **Command+C sent SIGINT and physical Ctrl+C did
nothing**.

`main()` sets `Qt::AA_MacDontSwapCtrlAndMeta`, after which both modifiers mean
the same thing on every platform. Measured:

```
default:                   Qt::ControlModifier + T -> ⌘T    Qt::MetaModifier + T -> ⌃T
AA_MacDontSwapCtrlAndMeta: Qt::ControlModifier + T -> ⌃T    Qt::MetaModifier + T -> ⌘T
```

See [keybindings](keybindings.md) for what that means in a config file.

### Logical DPI is 72, not 96

`QScreen::logicalDotsPerInch()` reports **72** on macOS and 96 on most of
X11/Wayland. Rasterizing a 12 pt font "at screen DPI" therefore produced a
12-pixel em box on a display whose physical DPI was 127.5. Combined with a
projection built from logical rather than device pixels, that is what made text
blurry. Font sizing is `points × (logicalDpi / 72) × devicePixelRatio`; see
[rendering](rendering.md#physical-pixels-and-why-it-matters).

### The system "fixed" font is not fixed

`QFontDatabase::systemFont(QFontDatabase::FixedFont).family()` returns the
generic `"monospace"`, which is exactly right to hand to fontconfig. Passing it
through `QFontInfo` first resolves it against the font engine, which answers
`.AppleSystemUIFont` — a **proportional** UI font. Feeding that to fontconfig
substituted Verdana, and the terminal grid was laid out with variable-width
glyphs.

### Fonts live in collections

All four styles of Menlo live in one `.ttc` at face indices 0–3, so font lookup
has to carry the face index, not just the path.

### Colour emoji are a bitmap-only font

`Apple Color Emoji.ttc` is not scalable: it has nine fixed strikes and yields
`FT_PIXEL_MODE_BGRA` bitmaps under `FT_LOAD_COLOR`. It also maps regional
indicators and keycap digits to **empty** glyphs, because the real flag or keycap
is only reachable by shaping the whole sequence — so "has a cmap entry" is not the
same as "will draw something". See
[rendering](rendering.md#colour-glyphs).

### ⌘H may never arrive

macOS reserves Command+H for "Hide application". A Qt app without a menu bar may
or may not receive it. If `⌘H` (previous tab) does nothing for you, rebind it.

## Linux and the BSDs

- The Super key is `Qt::MetaModifier`, the same modifier `cmd` maps to — which is
  why the two default keybinding files are equivalent and differ only in
  spelling.
- `fc-match` is expected to exist. Without fontconfig, RaTTY falls back to a
  per-platform list of known font paths so it still starts.
- A pty master reports `EIO` rather than end-of-file when the slave is gone; both
  are treated as a normal session end.

## Windows

Not supported. Not planned. There is no branch.

RaTTY is built on `forkpty`, POSIX job control, process groups and signals.
Windows has none of those. Its console is a genuinely different model wearing a
terminal costume, and ConPTY papers over the gap without closing it — supporting
it would mean writing a second program that happens to share a logo and a colour
scheme.

If you want a terminal on Windows, there is a perfectly good one shipped with the
operating system. Use that.

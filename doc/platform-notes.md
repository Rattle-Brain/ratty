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

### A held key offers accents instead of repeating

macOS has two readings of "the user is holding a key down": repeat the character,
or show a menu of accented variants. The second — "press and hold" — wins for any
view that takes part in the text input system, and `TerminalWidget` has to take
part, because that is the only way a dead-key `~` or an accent ever arrives (see
`Qt::WA_InputMethodEnabled` in its constructor).

The cost was key repeat: holding `j` produced one `j`. A terminal wants repeat
unambiguously — nothing in a shell or a TUI is served by an accent picker, and the
diacritics people actually type on a Spanish or French layout come from *dead
keys*, which are the input method's business and are unaffected. The two
mechanisms are independent.

Qt exposes no way to say this, so `src/platform/platform_mac.mm` says it to
AppKit directly, before `QApplication` exists:

```objc
[[NSUserDefaults standardUserDefaults] registerDefaults:@{
    @"ApplePressAndHoldEnabled" : @NO
}];
```

`registerDefaults:` rather than `setObject:forKey:` on purpose. It writes into
the registration domain, which lives only in this process: no file is created,
no other application is affected, and a user who has deliberately run
`defaults write -g ApplePressAndHoldEnabled -bool true` still has the last word,
because the global domain outranks the registration one. If you are that user and
you want repeat in RaTTY as well, clear the global value:

```
defaults delete -g ApplePressAndHoldEnabled
```

This is the only Objective-C++ in the project, and the only reason for
`src/platform/` to exist at all.

### Touching the GPU costs ~120 MB, whatever you do

Worth knowing before anyone files a bug about RaTTY's memory, or tries to fix it
by changing graphics API.

On Apple Silicon the driver charges a large fixed toll for a process's *first
draw*. Measured with a pure Metal program — no Qt, no RaTTY:

```
setup complete (device, queue, shader library, pipeline, 1024² texture)    4.4 MB
after frame 1 (256×256 render target)                                    123.0 MB
after frame 2..6                                                         123.0 MB
```

Same figure for a 256×256 target as for a full-screen one, and the same for
OpenGL. It is not the atlas, not the window, and not anything the application
allocates: it is the driver's per-process working set appearing the moment work
is first submitted. Nothing at application level removes it, and **Metal would
not remove it either** — that measurement *is* Metal.

So the floor for any GPU-accelerated Qt application on this platform is roughly
120 MB of driver plus ~28 MB of Qt and AppKit. What RaTTY controls is everything
above that line, which is why the surface architecture is the way it is; see
[one surface per window](rendering.md#one-surface-per-window).

Two related traps on the same platform:

- Physical footprint is what Activity Monitor shows, and it **drops sharply when
  a window is occluded** as the driver releases memory. A before/after comparison
  is only meaningful if both are sampled with the window in the same visibility
  state. RSS does not include this memory at all, so the two metrics disagree by
  a wide margin.
- Qt's `QRhi` headers (`<rhi/qrhi.h>`) are not shipped by Homebrew's Qt, so
  `QRhiWidget` is exported but unusable. Do not plan a port around it without
  checking that first.

## Linux and the BSDs

- The Super key is `Qt::MetaModifier`, the same modifier `cmd` maps to — which is
  why the two default keybinding files are equivalent and differ only in
  spelling.
- `fc-match` is expected to exist. Without fontconfig, RaTTY falls back to a
  per-platform list of known font paths so it still starts.
- A pty master reports `EIO` rather than end-of-file when the slave is gone; both
  are treated as a normal session end.
- **libstdc++ is stricter about transitive includes than libc++.** A header that
  compiles on macOS because something else dragged in `<cstddef>` will fail here.
  The same applies to Qt version drift: `QImage::flipped()` is Qt 6.9+, while
  Debian stable ships 6.4, so the deprecated-but-universal spelling or a hand
  rolled equivalent is the portable choice. Build in a container before claiming
  Linux support (see [building](building.md#checking-the-linux-build)).
- The shared canvas is a native child window (`createWindowContainer`). Verified
  on **X11**: the full suite passes under Xvfb with Mesa's software renderer. On
  **Wayland** the canvas behaviour itself is verified — input forwarding, divider
  dragging, click-to-focus and tab-bar stacking all pass — but only against a
  *headless* compositor, which never presents a window and so cannot exercise
  anything that depends on a frame having been drawn. A headed Wayland session is
  untested.

## Windows

Not supported. Not planned. There is no branch.

RaTTY is built on `forkpty`, POSIX job control, process groups and signals.
Windows has none of those. Its console is a genuinely different model wearing a
terminal costume, and ConPTY papers over the gap without closing it — supporting
it would mean writing a second program that happens to share a logo and a colour
scheme.

If you want a terminal on Windows, there is a perfectly good one shipped with the
operating system. Use that.

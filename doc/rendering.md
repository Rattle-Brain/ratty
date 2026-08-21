# Rendering


## One surface per window

A window has exactly one GPU surface: `TerminalCanvas`, a `QOpenGLWindow`
embedded in the widget tree with `QWidget::createWindowContainer()` and stacked
over the tab page. It owns the only `GLRenderer` and the only `GlyphAtlas`, and
draws every visible pane into its own viewport. Panes are plain `QWidget`s
underneath it that never paint.

Panes used to be `QOpenGLWidget`s, one GL context and one 4 MiB atlas each. Two
things were wrong with that, and both are worth understanding before anyone
reaches for a per-pane surface again.

**A `QOpenGLWidget` does not draw to the window.** It draws to a framebuffer
object of its own, which Qt then composites into the window's backing store. On
macOS that composite runs through the GL-on-Metal shim as a full-window texture
upload on *every flush*:

```
QWidgetRepaintManager::flush() → QPlatformBackingStore::rhiFlush()
  → QBackingStoreDefaultCompositor::flush() → QRhiGles2::endFrame()
    → glTexSubImage2D → AppleMetalOpenGLRenderer → AGXTexture   [14.4 MB]
```

A native `QOpenGLWindow` renders straight into a layer the window server
composites. There is no Qt backing-store round trip at all.

**And every extra pane repeated the whole arrangement.** Measured on an M-series
Mac at 1280×720, same harness, panes opened with the split keybinding:

| panes | one `QOpenGLWidget` each | one shared canvas |
|---|---|---|
| 1 | 280 MB | **258 MB** |
| 2 | 295 MB | 265 MB |
| 4 | 318 MB | 265 MB |
| 8 | 355 MB | **266 MB** |

Roughly **10.7 MB per extra pane → 1.07 MB**, and what is left is CPU-side
terminal state, not GPU memory. A pane in a tab that is not showing is not
visible, so it is not drawn at all and costs nothing.

Consequences that fall out of the design, and that any change here has to keep:

- **The canvas receives the mouse, because a native child window sits above its
  siblings whatever the widget stack says.** It hands events back by hit-testing
  the widget underneath — the pane, or the `QSplitter` handle between two of
  them — and a press latches its target until the release, so dragging a divider
  keeps working once the pointer leaves the two pixels it occupies.
- **Keyboard and input methods are untouched.** The canvas never takes focus
  (`Qt::NoFocus`), so key and IME events go straight to the focused pane widget
  as they always did.
- **The dividers come for free.** The canvas clears the whole surface to the
  split separator colour and lets the panes paint over it; the only pixels left
  showing are the gaps between panes. They cannot drift out of step with the
  pane rectangles the way a separately drawn line could.
- **The canvas covers the tab page, not the tab widget.** Sized to the whole tab
  widget it would hide the tab bar — and since the bar only appears with the
  second tab, the mistake would not show until then. `test_canvas_input` pins
  this, along with divider dragging and click-to-focus.
- **Blending must not touch destination alpha.** See
  [notable bugs](notable-bugs.md#a-dimmed-pane-made-the-whole-window-translucent).
- **The viewport is established at the top of `paintGL()`.** Each pane narrows
  it to its own rectangle, so at the end of a frame it describes whichever pane
  was drawn last; inheriting that would clip every frame after the first into a
  corner of the window.

There is one cost, and it is not a small one on macOS: a pane that is never
painted also never gets a chance to do paint-time work. Starting the shell was
hung off the first frame for exactly one commit, and a compositor that does not
present the window — a headless Wayland session, a window opened minimised —
left the terminal with no shell in it. Session start is now posted to the event
loop after layout instead. Do not move it back.


## Physical pixels, and why it matters

This is the single most important thing in the render layer.

The canvas is handed a viewport in **device** pixels, while Qt reports widget
geometry in **logical** ones. Measured on a Retina MacBook:

```
widget logical size: 400 200  |  devicePixelRatio: 2  |  framebuffer: 800 400
logicalDotsPerInch: 72        |  physicalDotsPerInch: 127.5
```

So a projection built from `width()`/`height()` covers a quarter of the
framebuffer's area and the GPU stretches everything 2× to fill it. On top of
that, `logicalDotsPerInch` is **72** on macOS, so rasterizing a 12 pt font "at
screen DPI" produced a 12-pixel em box — which was then magnified to cover 24
physical pixels.

The rules that follow:

1. `GLRenderer::beginFrame()` takes a viewport in device pixels and builds a
   matching orthographic projection. The pane's origin becomes `(0, 0)`, so a
   pane draws in its own coordinate space and knows nothing about where on the
   shared surface it landed.
2. `TerminalWidget` computes every geometry value as
   `logical × devicePixelRatioF()`.
3. The font is rasterized at `points × (logicalDpi / 72) × devicePixelRatio`,
   which is how Qt sizes its own text. It is rasterized **once per window**, by
   the canvas: one window is on one screen, so there is a single answer, and a
   change re-lays-out every pane.
4. Glyph quads land on integer pixel coordinates, and the atlas uses
   `GL_NEAREST`. With integer positions, fragment centres land exactly on texel
   centres, so the rasterized coverage is reproduced verbatim.
5. No multisampling. MSAA cannot improve an alpha-blended glyph quad (there is no
   geometric edge to smooth — the shape lives in the texture's alpha) and only
   adds a resolve blit.

Measured effect on the same display, rasterizing `g` from Menlo:

| | em box | cell | `g` bitmap | pixels of coverage | antialiased-edge pixels |
|---|---|---|---|---|---|
| before (12 pt @ 72 dpi, then stretched 2×) | 12 px | 7×15 | 7×10 | 70 | 56 % |
| after (13 pt × dpr 2) | 26 px | 16×32 | 13×21 | 273 | 36 % |

Roughly four times the coverage data, a smaller proportion of it spent on soft
edges, and no resampling pass on top.

Moving the window to a screen with a different ratio is handled:
`TerminalCanvas::refreshFont()` compares the current scale against the one the
font was last rasterized at, re-rasterizes when they differ, and re-lays-out
every pane in the window.

## `FontManager`

One `FT_Face` per style (regular / bold / italic / bold-italic) of a single
monospaced family, rasterized at an explicit pixel size.

The API takes **pixels**, not points-plus-DPI. The caller already knows how many
physical pixels a cell needs, and the old point/DPI API existed mainly as a place
to feed the wrong DPI into.

Font resolution asks `fc-match` for the file *and the face index*, which matters
because macOS ships collections: all four styles of Menlo live in
`/System/Library/Fonts/Menlo.ttc` at indices 0–3. A style whose face cannot be
found is synthesized with `FT_GlyphSlot_Embolden` / `FT_GlyphSlot_Oblique`.

#### Resolution order, and why it is careful

`loadFamily()` takes a *list* of families and tries them in order:

1. **Each configured preference**, accepted only if fontconfig resolves it to
   that same family. This check is essential: `fc-match` never fails, it
   substitutes — asking for a font that is not installed returns something else
   entirely. On this machine `fc-match "No Such Font"` answers **Verdana**, a
   proportional font, which is unusable in a character grid. `FontFile::family`
   carries the resolved name so the caller can tell.
2. **The platform's monospaced default**, where substitution is welcome because
   the platform's answer *is* the intended fallback. The query adds fontconfig's
   `:spacing=100` constraint so the answer is actually monospaced.
3. **A per-platform list of known font paths**, for a system with no fontconfig
   at all.

Getting the platform default right needed care too. `QFontDatabase::systemFont(
QFontDatabase::FixedFont).family()` returns the generic `"monospace"`, which is
exactly the right thing to hand to fontconfig — but passing it through
`QFontInfo` first resolves it against the font engine, which on macOS answers
`.AppleSystemUIFont`. Feeding *that* to fontconfig produced Verdana again. The
family name is therefore taken straight off the `QFont`, with
`QFontDatabase::isFixedPitch()` as the secondary source.

Finally, every candidate is verified after loading: `regularFaceIsMonospaced()`
compares the unscaled advances of `i` and `W` and rejects the face if they
differ. Font metadata can lie; two glyphs of visibly different width cannot. Even
an explicit request for a proportional family is refused and falls through to the
system monospace, which is the right call for a terminal.

#### The fallback chain

No single monospaced font covers what a terminal has to draw, so the primary
family is backed by a lazily grown list of others.

The motivating case is concrete. A patched "Nerd Font" build can carry twelve
thousand glyphs — every Powerline separator and file-type icon — and still have
**no box-drawing characters at all**, because the family it was patched from
never had them. Every TUI builds its borders, tree guides and separators out of
U+2500–U+257F, so without fallback a full-screen editor renders as a field of
empty `.notdef` boxes. Colour emoji are a second case: they only ever live in a
separate font, and one that stores bitmaps rather than outlines.

`resolveFaceSet()` answers "which family serves this code point", once per code
point, and caches the answer (including misses, since discovery shells out):

1. The primary family, if it has the glyph.
2. Families named in `font.fallback`, then the platform's monospaced default,
   then a list of known colour-emoji families, then the **bundled symbols font**.
   Loaded on first need.
3. Otherwise, ask fontconfig which fonts cover the code point
   (`fc-list :charset=<hex>`), monospaced candidates first.

Steps 2 and 3 both **verify coverage after loading** rather than trusting the
answer, and both reject placeholder families. That check is not paranoia:
`fc-match ":charset=1F600"` on macOS answers `.LastResort`, a font whose glyphs
are literally empty boxes, and a charset query for box drawing answers
proportional Verdana.

Step 3 uses `fc-list`, not `fc-match`, and the difference matters: **only
`fc-list` filters**. `fc-match` always answers *something* — given a charset
nothing good covers, its best guess is the placeholder — so rejecting that one
answer used to end the search, and a private-use icon that an installed font
really did carry was drawn as `.notdef` anyway. `fc-list` returns every font
whose charset contains the code point, so the placeholder is one candidate among
several. Monospaced candidates are tried first so a fallback's advance matches
the grid, and the list is capped, since a common code point can be claimed by two
hundred fonts and each candidate tried opens a face.

A blank glyph is the one thing coverage testing cannot see. `hasRenderableGlyph`
requires actual ink — it has to, because a colour-emoji font maps regional
indicators to empty shaping-only glyphs, and selecting that face drew nothing at
all. But a space *is* legitimately empty, so the chain reports that no font
covers U+00A0 and the `.notdef` box follows. `isSpaceSeparator()` (Unicode's Zs
category) settles it in two places: `TerminalRenderer` never asks for a glyph for
one, and `FontManager::rasterize` returns an empty bitmap rather than `.notdef` if
it is asked. `tree` indents with two NO-BREAK SPACEs per level, which is how its
output became a field of rectangles.

The platform monospace deliberately sits ahead of the emoji fonts, because it is
what supplies the arrows, geometric shapes and check marks a patched icon font
most often lacks.

##### The bundled symbols font

`resources/fonts/SymbolsNerdFontMono-Regular.ttf` is compiled into the binary and
adopted as the last loaded fallback, through `FT_New_Memory_Face` (the `FaceSet`
owns the bytes, because FreeType does not copy them).

It is there because a TUI's file-type icons are **private-use code points**, and
no stock font carries them. On a current macOS the only face claiming U+E8EB —
`nvim-web-devicons`' YAML icon — is `.LastResort`, a font of literal empty boxes;
a scan of all 371 installed font files, every face, finds nothing else. This is
the entire reason kitty renders those icons where a terminal that trusts the
system's fonts does not: kitty ships
`Contents/Resources/kitty/fonts/SymbolsNerdFontMono-Regular.ttf`. Ghostty does the
same. The font is MIT licensed, which is what makes that practical.

It goes last among the loaded families, so a font the user names in
`font.fallback` and the platform monospace both win, and it is skipped entirely
when it *is* the primary family. It carries symbols only — no Latin, no box
drawing, not even a space — so it cannot take over a character another font should
be serving, which a test asserts.

Fallback faces are rescaled by `matchFallbackSize()` so their line height matches
the primary cell. Different families draw a different proportion of the em, and
leaving them at the same em size left glyphs a pixel or two short of the cell.

#### Colour glyphs

A colour emoji font is bitmap-only with fixed strikes. `FT_LOAD_COLOR` (and
crucially *not* `FT_LOAD_NO_BITMAP`) yields a `FT_PIXEL_MODE_BGRA` bitmap;
`FT_Set_Char_Size` picks the nearest strike and scales it, so no manual strike
selection is needed. Colour faces are sized from
`min(cellHeight, 2 × cellWidth)` because emoji are double-width and span two
cells — sizing them from the line height alone made them bleed into the next row.

FreeType hands back *premultiplied* BGRA. `rasterizeFrom()` un-premultiplies and
swaps to RGBA so colour and coverage glyphs share one straight-alpha blend mode,
and marks the result with `GlyphBitmap::isColor`.

#### Presentation-aware resolution

`GlyphPresentation` (`Auto` / `Text` / `Emoji`) is threaded from the cell's
`CellFlagEmojiPresentation` through `drawGlyph()` and the atlas key down to
`resolveFaceSet()`, because the same code point can legitimately be cached twice
— once monochrome, once in colour.

Presentation sets the search *order*, not a hard filter: if no font of the
preferred kind has the glyph, one of the other kind still beats a `.notdef` box.
Two details earn their keep:

- A colour request **skips the primary font**. The primary is the monospaced text
  font, and its flat glyph is exactly what the selector asked us not to use.
- fontconfig discovery runs **inside** the strict pass. No monospaced font on a
  stock macOS carries U+26A0, so a text-presentation request would otherwise
  settle for the colour emoji — precisely what U+FE0E asks us not to do.

`FaceSet::hasRenderableGlyph()` requires the face to actually *draw* something,
not merely to have a cmap entry. Colour emoji fonts map regional indicators and
keycap digits to **empty** glyphs, because the real flag or keycap is only
reachable by shaping the whole sequence; choosing such a face would render
nothing at all. With the check, a keycap falls through to the plain digit and a
flag to a visible box.

Rasterization uses `FT_LOAD_TARGET_LIGHT` with `FT_RENDER_MODE_LIGHT`: light
hinting snaps stems vertically without touching horizontal metrics, which is what
a monospaced grid needs.

`computeMetrics()` derives the cell from a representative glyph (`M`, `0`, `x`)
rather than `max_advance`, because many monospaced fonts carry oversized advances
for box-drawing or fullwidth glyphs, which would leave a visible gap between
columns. Any leftover line gap is split evenly above and below so glyphs sit
optically centred.

## Box drawing is geometric, not from a font

Line and block characters (U+2500–U+259F) are drawn from the cell geometry by
`render/box_drawing.cpp`, ahead of any font lookup.

They have to *tile*: a vertical line must meet the one in the row below with no
seam, and a horizontal line must span the cell exactly. No font can guarantee
that once a fallback is involved, because families disagree about how much of the
em their box glyphs occupy — the fallback arrives a pixel or two short and borders
look dashed. Scaling the fallback to compensate only trades gaps for overhang.

A line character is fully described by the weight of its four arms
(none/light/heavy/double), which turns ~150 code points into one table and one
draw routine. Each arm runs from its cell edge *past* the centre by half the
perpendicular stroke, so corners and tees have no notch. Blocks, eighths,
quadrants and the three shades are handled separately; shades use an ordered 2×2
dither, which reads as an even tone at cell sizes.

The output is an ordinary 8-bit coverage mask, so it caches in the atlas and
renders through exactly the same path as a font glyph. Rounded corners
(U+256D–U+2570) are drawn square, and the diagonals (U+2571–U+2573) fall through
to the font.

## `GlyphAtlas`

One `GL_RGBA8` texture with a shelf (row-based) packing allocator and a
`codepoint | style → CachedGlyph` cache. A whole screen of text is one draw call.

RGBA rather than a single coverage channel, because colour emoji have to live
here too. A coverage mask is widened to `(255, 255, 255, coverage)` on upload and
tinted by the shader; a colour glyph is stored as-is and drawn untinted, selected
per-vertex by `CachedGlyph::isColor`. One texture and one draw call for both is
considerably simpler than maintaining two atlases, and 4 MiB for a 1024 px atlas
is not worth optimising. It also removed the `GL_TEXTURE_SWIZZLE_*` dance that
the single-channel format needed.

Filtering is `GL_NEAREST` deliberately (see [physical pixels](#physical-pixels-and-why-it-matters)). A 1-pixel gutter between
glyphs keeps rounding from ever picking up a neighbour.

Two things this class now handles that it previously only claimed to:

- **Growth is wired up.** `glyph()` grows the texture (or, at the 4096 cap, flushes
  the cache) when an allocation fails, so callers never see a "texture full"
  error. The old `grow()`/`isFull()` pair was never called from anywhere.
- **Mid-frame rebuilds are safe.** Rebuilding invalidates every UV already
  batched for the frame. A `generation()` counter lets `GLRenderer` notice,
  discard the stale batch and report `needsRepaint()`, and `TerminalWidget`
  schedules one more paint. Without this, a frame that happened to trigger growth
  would draw garbage.

The file also lost about sixty lines of duplicated `#ifdef Q_OS_MACOS` "Apple
Silicon workaround" that called the native GL entry points directly. The actual
problem those branches were working around was the missing swizzle for the
single-channel format, not Qt's wrappers, so there is now one code path.

## `GLRenderer`

Batched 2D drawing with three explicit layers:

| Layer | API | Contents |
|---|---|---|
| 1 | `fillBackground()` | cell backgrounds, underlines, strikethrough |
| 2 | `drawGlyph()` | glyphs |
| 3 | `fillOverlay()`, `strokeOverlay()` | cursor, selection, focus indicators |

`endFrame()` flushes them bottom-up. Draw order is therefore a property of the
API rather than of the order in which the grid loop happens to submit calls —
which is the permanent fix for cell backgrounds painting over their own
characters.

`drawGlyph()` takes a code point, not a `QString`. The old `drawText(QString, …)`
was called once per cell from the grid loop, allocating a one-character `QString`
for every cell of every frame.

Vertex buffers grow geometrically on demand rather than being capped at a fixed
size, and use `StreamDraw`.

## `TerminalRenderer`

The only place that knows how a grid maps onto pixels. `computeLayout()` derives
rows, columns and centring padding from the font metrics and the viewport;
`paint()` walks the grid and emits draw calls.

Worth noting:

- **Background runs are merged.** Horizontally adjacent cells sharing a
  background become one quad, so a full-width coloured bar costs 6 vertices
  instead of 6 per column. The common case — everything on the default
  background — emits nothing at all, because the frame was already cleared to
  that colour.
- **Bold and italic select a real font style** via `fontStyleFor(bold, italic)`.
  Previously every cell was drawn with the regular face and bold was faked by
  lightening the foreground colour, so bold text was merely brighter.
- **Wide-character trailers are skipped**, so a double-width glyph is not
  double-struck.
- **Leftover pixels are split evenly** between left/right and top/bottom, so a
  window that is not an exact multiple of the cell size does not look
  off-centre.
- **Padding insets the grid** from the window edge by `window.padding` logical
  pixels (default 4), scaled to physical pixels by the widget. The padding is
  subtracted before rows and columns are computed, and any remainder is then
  shared as above, so both edges keep their gap. Padding is clamped so it can
  never cost the last row or column on a very small window — the gap is given up
  before the content is.

---


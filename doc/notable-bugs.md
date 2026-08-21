# Bugs worth understanding

Each of these was reported as a visual defect and turned out to have a precise
mechanical cause. They are written up because the reasoning generalises, and
because two of them hid in places the test suite structurally could not look.

Both of these were reported as visual defects and both turned out to have precise
mechanical causes. They are documented because the reasoning generalises.

## Blurry text

**Symptom.** Text noticeably softer than kitty at the same size.

**Cause.** Four compounding factors, all variations on "logical pixels used where
physical pixels were meant":

1. `GLRenderer::beginFrame(width(), height())` built the projection from the
   widget's **logical** size, while Qt had set the viewport to the
   **device-pixel** size. On a `devicePixelRatio` of 2 the entire scene was
   stretched 2× by the GPU.
2. The font was rasterized at `screen->logicalDotsPerInch()`, which is **72** on
   macOS, so a 12 pt font got a 12-pixel em box — a quarter of the data needed to
   fill the 24 physical pixels it was then stretched across.
3. The atlas used `GL_LINEAR`, so that stretch was a resampling pass rather than
   a clean magnification.
4. `setSamples(4)` requested 4× MSAA for a pure 2D alpha-blended pass, adding a
   resolve blit that helped nothing.

`resizeGL()` also called `glViewport(0, 0, w, h)` with the logical size. That one
was harmless only by accident: Qt overwrites the viewport before each `paintGL()`.

**Fix.** See [physical pixels](rendering.md#physical-pixels-and-why-it-matters). Measured result: ~4× the glyph coverage data and no resampling.

## The white block after every Enter

**Symptom**, exactly as reported:

```
<white block>
terminal@prompt: somecommand
somecommand output
<white block>
terminal@prompt:
```

**Investigation.** Capturing what zsh actually writes to the pty after Enter:

```
\x1b[1m\x1b[7m%\x1b[27m\x1b[1m\x1b[0m<79 spaces>\r \r
\x1b]2;…\x07\x1b]7;file://…\x1b\\
\r\x1b[0m\x1b[27m\x1b[24m\x1b[J<prompt>
```

That first line is zsh's `PROMPT_SP` end-of-line marker. It prints an inverse
`%`, pads with spaces to the right margin, then does `\r` `space` `\r` to erase
the marker. The trick depends entirely on **deferred wrap**: in a correct
terminal, filling the last column leaves the cursor on that same row, so the
`\r space \r` erases the `%` and the prompt is drawn over it.

**Cause.** Two independent defects composing:

1. **`Screen` wrapped eagerly.** `putCharAtCursor` set `cursorCol_ = 0;
   cursorRow_++` the instant a character filled the last column. So the padding
   spaces advanced the cursor a row, the erase landed one row too low, and the
   inverse `%` survived — plus an extra row was consumed on every prompt.
2. **Rectangles were flushed after text.** `endFrame()` called
   `flushTextBatch()` and then `flushRectBatch()`, so any cell with a
   non-default background painted over its own glyph. The marker cell's inverse
   attribute made its background the near-white foreground colour, and the `%`
   glyph underneath was covered — turning an inverse `%` into a featureless
   white block.

**Verification.** Replaying that byte stream through the old core reproduces it
exactly — one opaque block per Enter, each on its own row:

```
 0 |echo hola                    |
 1 |<opaque block>               |
 2 |user@host repo (main) >      |
 3 |<opaque block>               |
 4 |user@host repo (main) >      |
--> opaque blocks drawn over text: 3
```

The same stream through the new core gives zero opaque cells and one row per
prompt. Pinned by `tests/test_terminal.cpp`
(`testDeferredWrap`, `testZshPromptArtifact`).

**Fix.** Deferred wrap in `Screen` ([`Screen`](terminal-emulation.md#screen)) and layered draw order in
`GLRenderer` ([`GLRenderer`](rendering.md#glrenderer)).

While in the same area, three more defects in the same byte stream were fixed:
the `ESC \` string terminator printed a stray `\` into the grid; `\x1b[38;5;208m`
selected no colour at all; and a UTF-8 sequence split across two pty reads became
replacement characters.

---

## Empty boxes where the blanks should be

**Symptom.** `tree` output arrived with two hollow rectangles at every
indentation level, where other terminals show a vertical guide and blank space:

```
CMakeFiles
├── 4.2.3
□□  ├── CMakeCXXCompiler.cmake
□□  ├── CMakeSystem.cmake
```

**Investigation.** Reading the bytes rather than the screen:

```
$ tree -L 3 CMakeFiles | hexdump -C
e2 94 9c e2 94 80 e2 94 80 20 34 2e 32 2e 33 0a   ├── 4.2.3
e2 94 82 c2 a0 c2 a0 20                           │ NBSP NBSP SPACE
```

`tree` 2.x indents with U+2502 and **two U+00A0 NO-BREAK SPACEs**. The `│` was
fine — box drawing is geometric — so the two boxes were the two no-break spaces.
Probing the font chain for them:

```
U+00A0  resolved=(none)  rasterized=1  12x19     <- the .notdef box
```

**Cause.** Coverage is tested with `FaceSet::hasRenderableGlyph()`, which demands
an actual outline. It has to: a colour-emoji font maps regional indicators to
empty shaping-only glyphs, and a face selected on cmap presence alone drew
nothing at all. But **a space is legitimately blank**, so every font in the chain
"failed" to cover U+00A0, and `rasterize()` fell through to its last resort — the
primary font's `.notdef` box.

The bug is that *blank* and *missing* are the same observation, and only the code
point can tell them apart.

**Fix.** `isSpaceSeparator()` in [`core/unicode.h`](../src/core/unicode.h),
Unicode's Zs category, consulted in two places: `TerminalRenderer` does not ask
for a glyph for one, and `FontManager::rasterize()` answers an empty bitmap rather
than `.notdef` if it is asked anyway. The model still stores U+00A0 as itself, so
a future selection copies the character the application actually sent.

Pinned by `tests/test_render.cpp` (`testSpacesDrawNothing`) and
`tests/test_terminal.cpp` (`testSpaceSeparators`).

---

## A third pane crushes the other two into a strip

**Symptom.** Two panes in a tab split evenly. Splitting again squeezed both of
them into a ~100 px strip along one edge while the new pane took everything else.

**Cause.** `SplitContainer::performSplit()` detaches the leaf being split from
its parent, wraps it in a new container with its new sibling, and hands that
container back to the parent through `replaceChild()`. `replaceChild()` preserved
the parent's split ratio by reading it back:

```cpp
const QList<int> sizes = splitter_->sizes();   // <- one entry, not two
splitter_->insertWidget(index, newChild);
if (sizes.size() == 2) splitter_->setSizes(sizes);
```

By then the detach had already happened, so that splitter held a **single**
widget and reported a single size. The `size() == 2` guard was therefore never
true, `setSizes()` never ran, and a widget QSplitter inserts with no size of its
own is clamped to its minimum — `TerminalWidget::setMinimumSize(200, 100)`, which
is exactly the 100 px strip that was reported.

Nothing about the *tree* was wrong, which is why the existing tests passed
throughout: they asserted topology, visibility and lifetime, never geometry.

**Fix.** Sample the geometry before the surgery destroys it. `performSplit()`
reads the parent's sizes (and the pane's own `size()`, which the detached
container cannot measure for itself — it still carries Qt's default 640×480
placeholder) up front, and passes both down. `closePane()` does the same for the
grandparent, and now takes the doomed container out of that splitter before the
promoted sibling goes in, so the splitter never transiently holds three widgets.

Pinned by `tests/test_splits.cpp` (`testNestedSplitKeepsGeometry`,
`testClosingRestoresTheSiblingsShare`), which assert pixels rather than pointers.

---

## `~` could not be typed at all

**Symptom.** `Option+ñ` on a Spanish Mac keyboard produced nothing. Neither did
`AltGr+ñ` on Linux.

**First diagnosis, and why it was wrong.** Option and AltGr *are* layout compose
keys, and RaTTY was ESC-prefixing everything that arrived with
`Qt::AltModifier` — treating the third level of the layout as a Meta key. That is
a real bug, and fixing it does make `|`, `@`, `[`, `]` and `€` typable. It does
nothing for the tilde, because it assumed the tilde arrives as text.

**Cause.** `~` is a **dead key** on both layouts. There is no character in the
key event at all: `text()` is empty, and the platform input method holds the
composition until the next keystroke resolves it, then delivers the result as a
`QInputMethodEvent`. `TerminalWidget` never set `Qt::WA_InputMethodEnabled` and
implemented neither `inputMethodEvent()` nor `inputMethodQuery()`, so the
platform had nowhere to deliver it. The same omission meant no accent (acute,
then `a`, for `á`) and no input method of any kind could reach the shell.

Two platforms failing identically was the clue worth taking seriously: the macOS
and Linux key-event paths through Qt are quite different, so a shared symptom
pointed at something neither of them was being asked to do.

**Fix.** All three pieces, in `TerminalWidget`; see
[composed input](ui.md#composed-input-dead-keys-and-input-methods). The
Alt-modifier fix stands as well — the two cover different halves of the layout.

Pinned by `tests/test_splits_gl.cpp` (`testComposedInputReachesTheShell`), which
commits `exit` into a pane the way a composition would and watches the shell act
on it. Whether the *platform* composes is beyond a test's reach; that the wiring
exists and carries bytes to the shell is not.

---

## `fc-match` always answers, so the icon search stopped at the first no

**Symptom.** File-type icons in a Telescope picker rendered as empty boxes, while
the same code points rendered as *something* in another terminal on the same
machine.

**Investigation.** The icons are private-use code points from
`nvim-web-devicons`. Asking fontconfig which fonts actually carry them:

```
$ fc-list ':charset=e855' family     # nvim's shader icon
Heiti SC | Songti SC | Hiragino Sans GB | .LastResort
$ fc-list ':charset=e8eb' family     # its yaml icon
.LastResort
```

So U+E855 was available and U+E8EB genuinely was not — but RaTTY drew a box for
both.

**Cause.** Discovery asked `fc-match ":charset=e855:spacing=100"`. `fc-match`
does not filter; it scores every font and returns its best guess, and for a
charset nothing good covers that guess is `.LastResort` — a font of literal empty
boxes, which the chain correctly refuses. Refusing it ended the search, so the
three fonts that *did* have the glyph were never considered.

**Fix.** Enumerate with `fc-list :charset=<hex>`, which filters to fonts whose
charset really contains the code point, monospaced candidates first, capped, each
still verified after loading. U+E855 now resolves to Heiti SC.

**What this does not fix.** A code point no installed font carries is still a box,
which is the honest answer — the bundled `font.fallback` now names the Nerd Fonts
symbol families so that installing one is all it takes.

Pinned by `tests/test_render.cpp` (`testDiscoveryLooksPastPlaceholderFonts`),
which asks fontconfig what the machine actually has before asserting anything.

---

## The font grows when the window moves to the other monitor

**Symptom**, exactly as reported: a terminal opened on a high-resolution display
shows its configured 12 pt. Drag the window to a lower-resolution display and the
text jumps to something nearer 18 px — but only in appearance. The configuration
still says 12, and pressing the shrink shortcut goes to 11 and *renders* 11, so
the picture snaps down by a third in one keystroke.

**Cause.** Glyphs are rasterized at an explicit **physical** pixel size, which is
the configured point size scaled by the screen's logical DPI and its device pixel
ratio (see [physical pixels](rendering.md#physical-pixels-and-why-it-matters)).
At 12 pt on a 2× display the atlas therefore holds 24 px glyphs, and that is
correct: they are drawn one texel per physical pixel into a framebuffer that is
itself 2× the widget's logical size.

Move the window to a 1× display and the framebuffer becomes 1:1 with logical
pixels — but nothing re-rasterized the font, so those same 24 px glyphs are now
drawn at 24 *logical* pixels. Twice the size asked for, while `fontSize()` still
reads 12. Changing the size then went down the one path that did call
`applyFontScale()`, which rebuilt the font correctly for the new display, so the
"fix" was really the first correct rasterization since the move.

`resizeGL()` did check for a changed ratio, and that check was right — it simply
never ran. Moving a window between displays does not resize the widget: its
logical geometry is identical on both, so Qt has no resize to report.

**Fix.** Three parts, because the first two are about noticing and the third is
about never being wrong regardless:

1. `TerminalWidget::event()` handles `QEvent::DevicePixelRatioChange` and
   `QEvent::ScreenChangeInternal`, which is Qt telling us directly.
2. One predicate names the invariant — the rasterized font matches the ratio, the
   logical DPI *and* the configured size it was built for. The logical DPI was
   previously read but not remembered, so a display differing only in DPI was
   invisible. Every path that might notice a change shares that predicate rather
   than duplicating part of it.
3. The predicate is checked before drawing, so a frame is never composed from a
   font built for a different display, whatever Qt did or did not deliver.

*Since the shared canvas, both live in `TerminalCanvas`: the font belongs to the
window, `refreshFont()` is the predicate, and a change re-lays-out every pane.
The reasoning is unchanged — only the owner moved.*

Pinned by `tests/test_splits_gl.cpp` (`testFontFollowsItsDisplay`). A second
monitor cannot be staged in a test, but the recovery mechanism can: moving the
configured size behind the widget's back leaves exactly the same inconsistency,
and the test fails without the `paintGL()` check.

---

## Tab moved between panes instead of completing a filename

**Symptom.** In a split window, pressing Tab jumped the caret to the other pane
instead of asking the shell to complete. With a single pane it worked fine, which
made it look like a split bug.

**Investigation.** Both of the obvious suspects were innocent. `InputHandler`
mapped `Key_Tab` to HT and `Key_Backtab` to CBT correctly, and
`tests/test_input.cpp` already asserted that Tab resolves to `ACTION_NONE` — no
keybinding was claiming it.

**Cause.** Qt never delivered the event. `QWidget::event()` handles Tab itself,
*before* `keyPressEvent()`:

```cpp
case QEvent::KeyPress: {
    if (!(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        if (k->key() == Qt::Key_Backtab || ...)  res = focusNextPrevChild(false);
        else if (k->key() == Qt::Key_Tab)        res = focusNextPrevChild(true);
        if (res) break;          // <-- consumed; keyPressEvent() never runs
    }
    keyPressEvent(k);
```

Every pane has `Qt::StrongFocus`, so with two of them `focusNextPrevChild(true)`
found somewhere to go, returned true, and the event was consumed. With one pane
there was nowhere to go, it returned false, and Tab fell through to
`keyPressEvent()` and worked — which is exactly why the bug appeared to be about
splits.

**Fix.** `TerminalWidget::focusNextPrevChild()` returns false unconditionally.
Refusing traversal is what lets the event reach `keyPressEvent()`. Moving between
panes is deliberately on its own bindings (`focus_left`/`right`/`up`/`down`): a
terminal cannot afford to spend Tab on window management.

Pinned by `tests/test_splits_gl.cpp`
(`testTabReachesTheShellRatherThanTheNextPane`), which splits a window and then
sends a real `Key_Tab` to the focused pane. It fails without the override. The
matching `Tab -> HT` assertion was also added to `tests/test_input.cpp`, which
had only ever covered Shift+Tab.

---

## A held key produced one character instead of repeating

**Symptom.** Holding `j` in RaTTY typed a single `j`. In kitty the same gesture
gives `jjjjjjjjjjjj`. The same for any key.

**Investigation.** Nothing in RaTTY was dropping the events. `InputHandler` does
not look at `QKeyEvent::isAutoRepeat()` at all, and it builds its output from
`event->text()`, so a repeat is encoded exactly like a first press — even Qt's key
*compression*, which can merge several presses into one event carrying `"jjj"`,
would have come out right. The events were never arriving.

**Cause.** macOS "press and hold". Holding a key in a view that participates in
the text input system offers a menu of accented variants instead of repeating,
and `TerminalWidget` participates deliberately: `Qt::WA_InputMethodEnabled` is
what makes a dead-key `~` reachable at all, and was added to fix precisely that
(see [`~` could not be typed](#-could-not-be-typed-at-all) above). One fix had
quietly bought the other bug.

The two behaviours are not the same mechanism, which is what makes this fixable
rather than a trade-off: dead keys and full input methods are composition, and
press-and-hold is a separate AppKit affordance layered on top. Turning the second
off leaves the first working.

**Fix.** `platform::enableKeyRepeat()`, called before `QApplication` is
constructed, registers `ApplePressAndHoldEnabled = NO` for this process only.
Qt has no API for it, so this is the project's one piece of Objective-C++ and the
reason `src/platform/` exists. Details, including why it registers the preference
rather than writing it, are in
[platform notes](platform-notes.md#a-held-key-offers-accents-instead-of-repeating).

`tests/test_input.cpp` pins the half that is ours: an auto-repeat event encodes
identically to a first press, for a text key and for an arrow. Whether the
platform *generates* those events cannot be asserted from a test — no test can
hold a key down — so that half is verified by holding a key.

---

## Emoji were too big, and consecutive rows overlapped

**Symptom.** A build log full of ✅ drew the check marks larger than the
surrounding text, and a column of them ran into each other vertically.

**Measurement.** At a 26 px font the cell was 16x32 with an ascender of 25, and
`U+2705` rasterized to 32x32 reporting `bearingY = +32`:

| | |
| --- | --- |
| cell | 16 x 32, ascender 25, capital height 19 |
| `U+2705` | 32 x 32, bearing +0,+32 |
| `M`, for scale | 14 x 19 |

**Three faults, found one at a time.** Each fix exposed the next, which is worth
recording because the first two looked complete.

*Placement.* A glyph is drawn at `baselineY - bearingY`. With the baseline at
`cellTop + ascender` and a bearing of 32, that is `cellTop - 7`: the emoji began
seven pixels *above the top of its own cell*, inside the row before it. Colour
fonts report their whole glyph as standing above the baseline, so the taller the
glyph the further back it reached. Colour glyphs are now positioned by their
**cell** -- centred in the two columns the emulator assigned them -- and not by
the font's bearing. An emoji font positions glyphs for a text layout engine; a
terminal has the stronger constraint.

*Size.* The target was `min(cellHeight, 2 * cellWidth)`: the whole line box, so a
32 px emoji sat beside a 19 px capital.

*And the one only a size sweep finds.* Targeting the ascender fitted at 26 px and
still overflowed at 13 px, because a colour font does not have a size -- it has
**strikes**. Apple Color Emoji ships 20, 26, 32, 40, 48, 52, 64, 96 and 160,
FreeType answers a request with the nearest, and the smallest is 20 px. In a 17 px
cell there is no strike small enough:

```
px   cell    ascender  emoji   verdict
13   8x17    13        20x20   OVERFLOWS
16   10x19   15        20x20   OVERFLOWS
26   16x32   25        20x20   fits
28   17x33   26        26x26   fits
30   18x36   28        20x20   fits      <- smaller than at 28 px
```

Note the last two rows: the size was not merely wrong, it was not *monotonic*.

**Fix.** Stop treating the strike as the answer. RaTTY picks the smallest strike
at or above the size it wants and **resamples it down** to exactly that size, so
emoji track the font instead of hopping between whatever sizes the font ships. The
target is the primary font's capital height times `font.emoji_scale`, which
defaults to just under parity with an `M`.

The resampling is a box filter in **premultiplied** space, un-premultiplied once
at the end. Averaging straight-alpha colour across a transparent edge pulls in the
colour of pixels that are not really there, and an emoji's outline picks up a halo
of its own interior; weighting each contribution by its own coverage is what keeps
the edges clean.

It is done at rasterization rather than by drawing a smaller quad because the
atlas is sampled 1:1 with `GL_NEAREST` (see [rendering](rendering.md)) -- scaling
at draw time would alias the bitmap instead of resizing it. Doing it once per
glyph, cached in the atlas, costs nothing per frame.

**One more trap on the way.** `FT_Bitmap_Size::height` is not the glyph size. For
Apple Color Emoji the strikes report heights of 26, 34, 42 and 52 while rendering
glyphs of 20, 26, 32 and 40 -- `height` is the strike's *line* height and
`y_ppem` is its em. Selecting on `height` picked a strike one step too small every
time, which capped emoji at 20 px however large the font got. The fix reads
`y_ppem`.

Pinned by `tests/test_render.cpp` (`testEmojiFitTheirCell`), which sweeps eight
font sizes and asserts at each that the glyph fits its two cells and its row,
stays within the ascender, matches the capital-height target to within a pixel,
and never shrinks as the font grows. A single-size test would have passed two of
the three broken versions.

## A dimmed pane made the whole window translucent

**Symptom.** Not seen on screen, and that is the point: the readback of a pane
came out with the right colours and an alpha of **197** instead of 255.

**Cause.** `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` blends the
destination *alpha* along with the colour. Anything translucent drawn over the
surface therefore pulls the framebuffer's alpha down — and RaTTY draws exactly
such a thing over every pane that is not the current one, the dim veil. For a
veil at strength `a` the arithmetic is `a² + (1 − a)`, which for 0.32 is 0.78, or
197.

This was harmless for as long as panes were `QOpenGLWidget`s: Qt composited the
result into an opaque backing store and threw the alpha away. Drawing straight
into a native layer, it is the **window server** that reads that alpha — so a
dimmed pane would have made the window itself see-through.

**Fix.** `glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO,
GL_ONE)`. Colour blends as before; destination alpha is left alone.

**Worth generalising.** Moving from a compositor that normalises your output to
one that does not will expose every place you were sloppy about alpha. The bug
was years old and completely invisible until the surface changed underneath it.

## Half the window came back blank

**Symptom.** With the shared canvas, panes rendered correctly on screen, but
`grabFramebuffer()` returned images in which the right-hand pane was a flat
colour. Only in the tests, and only sometimes.

**Cause.** Two separate mistakes, found in that order.

First, `paintGL()` computed the surface size itself as
`width() × devicePixelRatio()`. Qt does not promise that: `grabFramebuffer()`
renders into an offscreen buffer of its own choosing. When the two disagreed the
panes were laid out for a 1000 px surface and read back from a 2000 px one, so
everything past the halfway mark was empty. The viewport is now established
explicitly at the top of every frame.

Second, and more fundamental: `QOpenGLWindow::grabFramebuffer()` is only
reliable when the window keeps a framebuffer object to render into, and keeping
one would reinstate the full-window buffer the shared canvas exists to avoid.
With `NoPartialUpdate` it reads a back buffer whose contents are **undefined
after a swap** — which is why it came back blank.

**Fix.** `TerminalCanvas::grabPane()` renders a frame and reads it straight back
with `glReadPixels` before anything is swapped. Exact, and it costs no permanent
memory.

**Worth generalising.** The tests inspected pixels *only* through the grab path,
so a grab that set up its own viewport would have hidden a broken on-screen path
completely. Making `paintGL()` establish the viewport itself removed the
divergence rather than papering over it.

## A pane with no shell in it

**Symptom.** On a headless Wayland session, every pane came up empty and
`shellPid()` was −1. Fine on X11, fine on macOS.

**Cause.** Session creation had been moved to the first `renderInto()` call, so
that the grid handed to the shell would be computed from the pane's real size
rather than the minimum size it has at `showEvent()` time. That works right up
until something never paints — and a compositor that does not present the window
produces no frames at all. No frame, no session, no shell.

**Fix.** `showEvent()` posts the start to the event loop with
`QTimer::singleShot(0, ...)`. By then the splitter has laid the pane out, so the
grid is the right one, and it does not depend on a frame ever happening.

**Worth generalising.** "Do it on first paint" is a tempting way to wait for
layout, and it silently couples liveness to visibility. A zero-timer waits for
the same thing without that coupling. The earlier symptom of getting this wrong
was subtler and easy to miss: the shell was told a 5×24 grid for a pane that was
about to be 34×61.

---

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

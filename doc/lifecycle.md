# Lifecycle: from bytes to pixels

How data moves through RaTTY, and the object lifetimes that go with it.


### Output path (shell → screen)

```
  shell writes to the pty slave
        │
        ▼
  QSocketNotifier fires on the master fd
        │
        ▼
  TerminalSession::drainPty()
        │   reads up to 32 × 64 KiB per event
        ▼
  TerminalEmulator::write(bytes)
        │
        ├─► Utf8Decoder  bytes → char32_t, retaining any partial sequence
        │
        ▼
  VTParser::advance(code points)
        │   pure syntax: Ground / Escape / CSI / OSC / …
        │
        ├─► VTHandler::print(ch)            printable character
        ├─► VTHandler::control(c0)          BS, HT, LF, CR, BEL, …
        ├─► VTHandler::csiDispatch(seq)     CSI … final
        ├─► VTHandler::escDispatch(i, f)    ESC … final
        └─► VTHandler::oscDispatch(n, data) OSC n ; data ST
              │
              ▼
        TerminalEmulator  (semantics: SGR → pen, modes, replies)
              │
              ▼
        Screen  (cells, cursor, scrolling)  ── revision() bumped
              │
              ▼
        TerminalSession emits screenChanged()
              │
              ▼
        TerminalWidget::update()  →  paintGL()
              │
              ▼
        TerminalRenderer::paint(screen, palette, layout, options)
              │
              ├─► GLRenderer::fillBackground(...)   layer 1
              ├─► GLRenderer::drawGlyph(...)        layer 2
              └─► GLRenderer::fillOverlay(...)      layer 3
                        │
                        ▼
              GLRenderer::endFrame()
                  flush layer 1 → flush layer 2 → flush layer 3
```

The three layers are the whole reason cell backgrounds no longer hide their own
characters. Draw order is a property of the API, not of the order in which the
grid loop happens to emit calls.

### Input path (keyboard → shell)

```
  QKeyEvent
     │
     ▼
  TerminalWidget::keyPressEvent
     │
     ├─ Config::isBound(sequence)? ── yes ─► event->ignore()
     │                                        │  propagates up the widget chain
     │                                        ▼
     │                                   MainWindow::keyPressEvent → handleAction()
     │
     ├─ search prompt open? ── yes ─► handleSearchKey(): query, steps, escape
     │
     └─ no ─► InputHandler::keyEventToBytes(event, applicationCursorKeys)
                    │
                    ├─ scrollViewToBottom(), clearSelection()
                    ▼
              TerminalSession::sendInput(bytes) → PTY::write → shell
```

The `isBound` check is what makes application shortcuts work at all. The widget
previously accepted *every* key event, so nothing ever reached `MainWindow` and
no keybinding in the config file could fire. It comes **before** the search
prompt deliberately: the prompt owns the keyboard while it is open, but a
shortcut still has to work mid-search.

The two steps on the way out matter for the same reason as each other. Typing
returns the view to the live screen, or the echo of what was just typed lands out
of sight; and it drops the selection, because the text under the highlight is
about to move.

---


## The OpenGL context lifecycle

There is **one GL context per window**, owned by `TerminalCanvas`, and it is
created once and never torn down until the window closes. Most of what used to
be delicate here stopped existing when panes stopped owning surfaces; see
[one surface per window](rendering.md#one-surface-per-window).

### `TerminalWidget`

A plain `QWidget` that owns a `TerminalSession` and a `TerminalRenderer`, and
does little else: translate events, compute the layout, describe how to draw
itself. It has no GL context and no `paintEvent` — the canvas is stacked over it
and calls `renderInto()` with a viewport. Process management, byte decoding and
VT interpretation all live in `TerminalSession`; the grid→pixels mapping lives in
`TerminalRenderer`.

Details worth knowing before editing it:

- **The session must never be rebuilt.** It owns the pty and the shell, and
  reparenting a pane — which is exactly what splitting or closing one does — must
  not touch them. This used to be a live hazard: panes were `QOpenGLWidget`s,
  reparenting destroyed the GL context, Qt called `initializeGL()` again, and the
  session was rebuilt along with the renderer. That killed the running shell and
  replaced it with an empty one on every split, which is what made the pane look
  blank. `ensureSession()` is now idempotent and there is no context to lose, but
  the invariant is worth keeping stated.
- **The shell starts on layout, not on paint.** `showEvent()` posts the start to
  the event loop rather than doing it inline or waiting for a frame. Inline is
  too early — the splitter has not divided the space yet, so the pane is at its
  minimum size and the grid handed to the shell is the wrong one (measured: a
  5×24 grid for a pane that was about to be 34×61). Waiting for a frame is worse:
  a window a compositor never presents produces no frames, and the terminal ends
  up with no shell in it at all.
- **The canvas releases its GL objects with its context current**, in
  `~TerminalCanvas`. Deleting them later would issue GL calls against a context
  that no longer exists.
- **The canvas is not built when the platform has no GL.** The offscreen plugin
  the non-GL suites run under cannot create a context, and a `QOpenGLWindow`
  there does not merely draw nothing — it fails inside Qt's own paint machinery.
  A pane whose window has no canvas simply does not draw, which is the graceful
  degradation those suites want.
- The cursor blink timer only runs when the pane has focus *and* blinking is
  enabled. It used to repaint the entire grid twice a second unconditionally.
  Incoming output resets the blink phase so the cursor stays solid while text
  arrives.
- An unfocused pane draws a hollow cursor, which is how tiling terminals signal
  "input does not go here".

## Why a split is fast

Opening a split used to take around 450 ms of blocking work, which is long enough
to feel like the application had stopped. None of it was the tree surgery.

Reparenting a `QOpenGLWidget` destroyed its context, so `initializeGL()` ran
again — on the new pane *and* on the pane being split — and each run rebuilt
everything the old context had owned. Rebuilding the renderer meant compiling two
shader programs and, worse, resolving the entire font chain from scratch. Font
resolution shells out to `fc-match`, once per family per style, and a chain is
the primary family in four styles, the platform monospace, and six candidate
emoji families. A four-way split came to **157 subprocess spawns**.

Five things fixed it, in the order they matter:

| Change | Where | Effect |
| --- | --- | --- |
| `Qt::AA_ShareOpenGLContexts` | `main()` | The context survives reparenting, so neither the split pane nor any other rebuilds anything at all |
| fontconfig answers memoized for the process | `font_manager.cpp` | 157 spawns → 17, and the 17 are the genuinely distinct questions |
| `FontManager` instances shared by (families, fallbacks, pixel size) | `FontManager::shared()` | The font chain is built **once** for the whole window; a new pane adopts it |
| Shader programs shared across the context group | `GLRenderer::acquirePrograms()` | One compile and link per share group instead of per pane |
| One GL surface for the whole window | `TerminalCanvas` | A new pane brings up no context, no atlas and no buffers, because it has none of its own |

Measured on the same machine and the same configuration: **~450 ms → ~30 ms**.

The last row is what removed the remainder. What used to be left was Qt and
driver cost in bringing up a fresh `QOpenGLContext` per pane — about 16 ms of it
inside the first `QOpenGLVertexArrayObject::create()` on that context. That was
described here as "a different architecture, not an optimization", which was
true; the architecture changed. A split is now pure widget surgery, and the same
change is what made a pane cost roughly 1 MB instead of 10.

Two consequences worth remembering when editing:

- A shared `FontManager` must never be resized while anyone else holds it.
  `FontManager::shared()` keys on the pixel size for exactly this reason, and
  only re-scales a chain it can see is unreferenced.
- The shader programs are held **weakly** by the cache and strongly by the
  renderer. With one renderer per window that is no longer load-bearing for
  ordering, but it is still what guarantees the programs are deleted while a
  context of the group is current.


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
     └─ no ─► InputHandler::keyEventToBytes(event, applicationCursorKeys)
                    │
                    ▼
              TerminalSession::sendInput(bytes) → PTY::write → shell
```

The `isBound` check is what makes application shortcuts work at all. The widget
previously accepted *every* key event, so nothing ever reached `MainWindow` and
no keybinding in the config file could fire.

---


## The OpenGL context lifecycle

### `TerminalWidget`

A `QOpenGLWidget` that owns a `GLRenderer`, a `TerminalSession` and a
`TerminalRenderer`, and does little else: translate events, compute the layout,
paint. Process management, byte decoding and VT interpretation all live in
`TerminalSession`; the grid→pixels mapping lives in `TerminalRenderer`.

Details worth knowing before editing it:

- **`initializeGL()` can run more than once, and must not rebuild the session.**
  Reparenting a `QOpenGLWidget` — which is exactly what splitting or closing a
  pane does — destroys its GL context and creates a new one, so Qt calls
  `initializeGL()` again. Measured:

  ```
  [pane] initializeGL #1  ctx=0xca6cc9450
  --- reparented into a QSplitter ---
  [pane] initializeGL #2  ctx=0xca6cca7d0
  ```

  The renderer *must* be rebuilt, because every GL object it owns belonged to the
  dead context. The `TerminalSession` must *not* be: it owns the pty and the
  shell, which have nothing to do with any context. Rebuilding it killed the
  running shell and replaced it with an empty one on every split, which is what
  made the pane look blank.

  `main()` now sets `Qt::AA_ShareOpenGLContexts`, and Qt skips that teardown
  entirely when contexts are shared — so in practice the second call above no
  longer happens. The guard stays, because it is what the invariant rests on and
  because nothing about it depends on the attribute being set. See
  [why a split is fast](#why-a-split-is-fast).
- **GL resources are released from `QOpenGLContext::aboutToBeDestroyed`**, the
  only moment the outgoing context is still current. Deleting them later would
  issue GL calls against a context that no longer exists.
- `resizeGL()` must **not** call `makeCurrent()`/`doneCurrent()`. Qt invokes it
  with the context already current, and releasing it leaves Qt's own resize
  handling without one. `reloadFont()`, which is called from outside a paint,
  does need the pair.
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

Four things fixed it, in the order they matter:

| Change | Where | Effect |
| --- | --- | --- |
| `Qt::AA_ShareOpenGLContexts` | `main()` | The context survives reparenting, so neither the split pane nor any other rebuilds anything at all |
| fontconfig answers memoized for the process | `font_manager.cpp` | 157 spawns → 17, and the 17 are the genuinely distinct questions |
| `FontManager` instances shared by (families, fallbacks, pixel size) | `FontManager::shared()` | The font chain is built **once** for the whole window; a new pane adopts it |
| Shader programs shared across the context group | `GLRenderer::acquirePrograms()` | One compile and link per share group instead of per pane |

Measured on the same machine and the same configuration: **~450 ms → ~30 ms**.

What is left is Qt and driver cost in bringing up a fresh `QOpenGLContext` —
about 16 ms of it inside the first `QOpenGLVertexArrayObject::create()` on that
context. Removing it would mean one context for the whole window rather than one
per pane, which is a different architecture, not an optimization.

Two consequences worth remembering when editing:

- A shared `FontManager` must never be resized while anyone else holds it.
  `FontManager::shared()` keys on the pixel size for exactly this reason, and
  only re-scales a chain it can see is unreferenced.
- The shader programs are held **weakly** by the cache and strongly by each
  renderer. That is what makes the last renderer to let go the one that deletes
  them, from `releaseGLResources()`, where a context of the group is current.


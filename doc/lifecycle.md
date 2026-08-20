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

- **`initializeGL()` runs more than once, and must not rebuild the session.**
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


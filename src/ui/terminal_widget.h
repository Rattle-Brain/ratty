/*
 * TerminalWidget - one terminal session, as a pane
 *
 * Deliberately thin: it owns a TerminalSession, translates Qt events into
 * session input, and describes how to draw itself. Process management, byte
 * decoding and VT interpretation live in TerminalSession; the grid-to-pixels
 * mapping lives in TerminalRenderer.
 *
 * It does not own a GPU surface and does not paint itself. Every pane in a
 * window draws through one shared TerminalCanvas, which is what makes a pane
 * cost no GPU memory -- see terminal_canvas.h for why that matters. What stays
 * here is everything Qt is good at and a canvas would have to reinvent: layout
 * through QSplitter, focus, the keyboard, and input methods.
 *
 * All rendering happens in *physical* pixels: the canvas gives this pane a
 * viewport already scaled by the device pixel ratio, so geometry computed here
 * is multiplied by that same ratio and the font is rasterized to match.
 */

#ifndef UI_TERMINAL_WIDGET_H
#define UI_TERMINAL_WIDGET_H

#include "../core/terminal_session.h"
#include "../render/gl_renderer.h"
#include "../render/terminal_renderer.h"
#include "input_handler.h"
#include <QImage>
#include <QWidget>
#include <memory>

class QTimer;
class TerminalCanvas;

class TerminalWidget : public QWidget {
    Q_OBJECT

public:
    /*
     * `startDirectory` is where this pane's shell begins, resolved by the caller
     * from the configuration (see StartDirectory). It has to be known at
     * construction because the session is created the first time the widget
     * gets a GL context, which is before anything else can set it.
     */
    explicit TerminalWidget(QWidget* parent = nullptr,
                            const QString& startDirectory = QString());
    ~TerminalWidget() override;

    /* Marks this pane as the focused one in a split layout. */
    void setPaneFocused(bool focused);
    bool isPaneFocused() const { return paneFocused_; }

    /*
     * Fade this pane back, because another one has the keyboard.
     *
     * Separate from setPaneFocused() because it answers a different question:
     * whether there is anything to tell apart. Only SplitContainer knows how
     * many panes share the tab, and a tab holding one pane must not be dimmed
     * merely because nothing has claimed the marker yet.
     */
    void setPaneDimmed(bool dimmed);
    bool isPaneDimmed() const { return paneDimmed_; }

    void copySelection();
    void paste();

    /*
     * Scrollback view. Positive counts move towards the past, matching
     * Screen::scrollViewBy(). A page is one screenful less a row, so a line of
     * context carries over.
     */
    void scrollLines(int lines);
    void scrollPages(int pages);
    void scrollToTop();
    void scrollToBottom();
    void clearScrollback();

    /* How far back the view is, and how much history there is to move through.
     * Both are zero with no session. */
    int viewOffset() const;
    int historySize() const;

    /* Font size follows the global config; call after changing it. */
    void reloadFont();

    /*
     * Re-apply every setting this pane owns after the configuration has been
     * re-read: colours, font, scrollback and cursor behaviour. The session and
     * the shell inside it are untouched -- a reload must not cost the user their
     * running command.
     */
    void applyConfiguration();

    /*
     * Draw this pane into the window's shared surface, at the given viewport in
     * physical pixels (`bottom` measured from the bottom of the surface, as GL
     * counts). Called by TerminalCanvas, which owns the renderer.
     */
    void renderInto(GLRenderer& renderer, int left, int bottom, int width, int height);

    /*
     * This pane's pixels, read back from the shared surface. Panes have no
     * framebuffer of their own any more; this keeps the one thing that was
     * worth asking a QOpenGLWidget for.
     */
    QImage grabFramebuffer() const;

    /*
     * Recompute the grid for the current font and push the new size to the
     * session. Public because the canvas calls it for every pane when the font
     * is re-rasterized, which is a window-wide event.
     */
    void updateGeometryForFont();

    QString title() const { return title_; }

    /* The directory this pane's shell is in now, or empty if unknown. A new
     * split configured to inherit starts here. */
    QString workingDirectory() const;

    /* The shell's process id, or -1 if none is running. */
    pid_t shellPid() const { return session_ ? session_->shellPid() : -1; }

    /*
     * What the platform input method needs to know: that this widget takes
     * composed input at all, and where to put a candidate window. Public
     * because QWidget declares it so.
     */
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    void sessionEnded();
    void titleChanged(const QString& title);
    /*
     * The user picked this pane by clicking in it.
     *
     * Deliberately not driven off focusInEvent(): Qt hands focus out by itself
     * every time a widget is reparented -- which every split and close does --
     * and it does not deliver a focus event at all until the window is active.
     * Neither is the user choosing a pane. A click is.
     */
    void paneActivated();

protected:
    /*
     * Watches for the widget moving to a display with a different device pixel
     * ratio, which changes how many physical pixels a point is worth and so
     * invalidates every rasterized glyph. Qt delivers this without necessarily
     * resizing the widget, so a resize cannot be relied on to catch it.
     */
    bool event(QEvent* event) override;
    /*
     * Refuses Tab-based focus traversal, which is what keeps Tab and Shift+Tab
     * available to the shell. See the implementation.
     */
    bool focusNextPrevChild(bool next) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    /*
     * Composed input. A dead key produces no text of its own -- the platform
     * input method holds the composition and delivers the result here, and it
     * only does that for a widget that has asked (WA_InputMethodEnabled, set in
     * the constructor). See the implementation.
     */
    void inputMethodEvent(QInputMethodEvent* event) override;

private slots:
    void onScreenChanged();
    void onBlinkTick();

private:
    /* The window's shared GPU surface, or nullptr before it exists. */
    TerminalCanvas* canvas() const;
    /* Ask the shared surface for a repaint; this widget paints nothing itself. */
    void requestRepaint();
    /* Create the session on first use, once the canvas can size the grid. */
    void ensureSession();
    /*
     * Start the shell once the pane has been laid out, without waiting to be
     * painted. Posted rather than run inline: at showEvent() time the splitter
     * has not divided the space yet, so the pane is still at its minimum size
     * and the grid computed from it would be the wrong one.
     */
    void scheduleSessionStart();

    /* Physical-pixel size of this pane's slice of the shared surface. */
    int framebufferWidth() const;
    int framebufferHeight() const;
    double scaleFactor() const;
    /* Configured window padding, converted to physical pixels. */
    int paddingPixels() const;
    /* Config's cursor style, unless the application asked for another one. */
    CursorStyle effectiveCursorStyle() const;

    /*
     * Grid cell under a widget-relative position, clamped to the grid so a drag
     * that leaves the window still reports its nearest cell -- which is what
     * every application selecting text with the mouse expects. False only when
     * there is no layout yet.
     */
    bool cellAt(const QPointF& position, int& row, int& col) const;

    /*
     * The cell the cursor is on, in the logical pixels Qt reports geometry in.
     * This is where the platform puts a candidate window, so getting it wrong
     * parks one in the corner of the screen.
     */
    QRectF cursorRectangle() const;

    /*
     * Hand one mouse event to the application, if it asked for the mouse and
     * this event is reportable. Returns true when the application took it, so
     * the caller knows not to act on it locally.
     */
    bool reportMouse(MouseAction action, MouseButton button, const QPointF& position,
                     Qt::KeyboardModifiers modifiers);
    /* True while the application owns the mouse and the user is not overriding
     * it with Shift. */
    bool applicationWantsMouse(Qt::KeyboardModifiers modifiers) const;

    /* Whole wheel notches available from `event`, accumulating the fractional
     * deltas that trackpads and high-resolution wheels send. Positive is a
     * scroll towards the past. */
    int consumeWheelNotches(const QWheelEvent* event);
    /* Repeat a cursor key, respecting DECCKM: how the wheel drives a pager on
     * the alternate screen. */
    void sendCursorKey(char final, int count);

    /* Recompute the layout and push the new size to the session. Called on
     * resize, on font change and when the widget moves to a screen with a
     * different device pixel ratio. */
    void restartBlink();

    std::unique_ptr<TerminalSession> session_;
    TerminalRenderer gridRenderer_;
    TerminalRenderer::Layout layout_;
    InputHandler inputHandler_;

    QTimer* blinkTimer_ = nullptr;
    /* A session start is already posted; do not post a second. */
    bool sessionStartPosted_ = false;
    bool cursorPhaseOn_ = true;
    bool paneFocused_ = false;
    bool paneDimmed_ = false;
    QString title_;
    /* Where this pane's shell was told to start. */
    QString startDirectory_;

    /* Leftover wheel rotation, in eighths of a degree. */
    int wheelRemainder_ = 0;
    /* Last cell reported for motion, so a drag across one cell is not reported
     * once per pixel. */
    int lastMotionRow_ = -1;
    int lastMotionCol_ = -1;

    static constexpr int DefaultRows = 24;
    static constexpr int DefaultCols = 80;
    static constexpr int CursorBlinkMs = 530;
    /* One notch of a conventional mouse wheel, in Qt's angle-delta units. */
    static constexpr int WheelNotch = 120;
};

#endif /* UI_TERMINAL_WIDGET_H */

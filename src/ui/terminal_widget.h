/*
 * TerminalWidget - OpenGL view of one terminal session
 *
 * Deliberately thin: it owns a GL context, a GLRenderer and a TerminalSession,
 * translates Qt events into session input, and paints. Process management, byte
 * decoding and VT interpretation live in TerminalSession; the grid-to-pixels
 * mapping lives in TerminalRenderer.
 *
 * All rendering happens in *physical* pixels. Qt hands resizeGL() the widget's
 * logical size but sets the GL viewport to the device-pixel size before
 * paintGL(), so anything drawn in logical coordinates is silently magnified by
 * the device pixel ratio. Every geometry value below is therefore multiplied by
 * devicePixelRatio(), and the font is rasterized at that same scale.
 */

#ifndef UI_TERMINAL_WIDGET_H
#define UI_TERMINAL_WIDGET_H

#include "../core/terminal_session.h"
#include "../render/gl_renderer.h"
#include "../render/terminal_renderer.h"
#include "input_handler.h"
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <memory>

class QTimer;

class TerminalWidget : public QOpenGLWidget, protected QOpenGLFunctions {
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
     * resizing the widget, so resizeGL() cannot be relied on to catch it.
     */
    bool event(QEvent* event) override;
    /*
     * Refuses Tab-based focus traversal, which is what keeps Tab and Shift+Tab
     * available to the shell. See the implementation.
     */
    bool focusNextPrevChild(bool next) override;
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

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
    /* Release GL-owned objects while their context is still current. */
    void releaseGLResources();

private:
    /* Physical-pixel size of the framebuffer Qt gave us. */
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
    void updateGeometryForFont();
    bool applyFontScale();
    /* True when the rasterized font no longer matches the display or the
     * configured size, and has to be rebuilt before the next frame. */
    bool fontScaleStale() const;
    /* This screen's logical DPI, never zero. */
    double logicalDpi() const;
    void restartBlink();

    std::unique_ptr<GLRenderer> renderer_;
    std::unique_ptr<TerminalSession> session_;
    TerminalRenderer gridRenderer_;
    TerminalRenderer::Layout layout_;
    InputHandler inputHandler_;

    QTimer* blinkTimer_ = nullptr;
    bool cursorPhaseOn_ = true;
    bool paneFocused_ = false;
    QString title_;
    /* Where this pane's shell was told to start. */
    QString startDirectory_;

    /*
     * What the font on screen was rasterized for, so a screen change is
     * detectable. All three are inputs to applyFontScale(), and all three can
     * change without this widget being resized.
     */
    double lastScaleFactor_ = 0.0;
    double lastLogicalDpi_ = 0.0;
    int lastFontSize_ = 0;

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

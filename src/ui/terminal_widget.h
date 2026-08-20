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
    explicit TerminalWidget(QWidget* parent = nullptr);
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

    /* The shell's process id, or -1 if none is running. */
    pid_t shellPid() const { return session_ ? session_->shellPid() : -1; }

signals:
    void sessionEnded();
    void titleChanged(const QString& title);

protected:
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

    /* Scale the font was last rasterized at, so a screen change is detectable. */
    double lastScaleFactor_ = 0.0;
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

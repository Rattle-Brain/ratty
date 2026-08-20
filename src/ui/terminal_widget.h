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

    /* Font size follows the global config; call after changing it. */
    void reloadFont();

    QString title() const { return title_; }

signals:
    void sessionEnded();
    void titleChanged(const QString& title);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private slots:
    void onScreenChanged();
    void onBlinkTick();

private:
    /* Physical-pixel size of the framebuffer Qt gave us. */
    int framebufferWidth() const;
    int framebufferHeight() const;
    double scaleFactor() const;
    /* Configured window padding, converted to physical pixels. */
    int paddingPixels() const;
    /* Config's cursor style, unless the application asked for another one. */
    CursorStyle effectiveCursorStyle() const;

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

    static constexpr int DefaultRows = 24;
    static constexpr int DefaultCols = 80;
    static constexpr int CursorBlinkMs = 530;
};

#endif /* UI_TERMINAL_WIDGET_H */

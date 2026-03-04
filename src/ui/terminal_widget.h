/*
 * TerminalWidget - OpenGL-accelerated terminal display
 *
 * QOpenGLWidget-based terminal that:
 * - Renders text using GLRenderer
 * - Manages a PTY process
 * - Handles keyboard/mouse input
 * - Displays raw PTY output (until terminal emulation is implemented)
 */

#ifndef UI_TERMINAL_WIDGET_H
#define UI_TERMINAL_WIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QSocketNotifier>
#include <QTimer>
#include <memory>
#include "../core/pty.h"
#include "../core/terminal_emulator.h"
#include "../render/gl_renderer.h"
#include "input_handler.h"

class TerminalWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget* parent = nullptr);
    ~TerminalWidget() override;

    // Focus management (for split navigation)
    void setFocusedBorder(bool focused);
    bool isFocusedTerminal() const { return focusedBorder_; }

    // Clipboard operations
    void copySelection();
    void paste();

signals:
    // Emitted when the PTY session ends (user exits shell)
    void sessionEnded();

protected:
    // QOpenGLWidget overrides
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Input handling
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private slots:
    void onPTYDataReady();
    void onBlinkTimer();

private:
    void createPTY();
    void calculateTerminalSize();
    void renderContent();

    // PTY
    std::unique_ptr<PTY> pty_;
    QSocketNotifier* ptyNotifier_;

    // Renderer
    std::unique_ptr<GLRenderer> renderer_;

    // Terminal emulator
    std::unique_ptr<TerminalEmulator> emulator_;

    // Input handling
    InputHandler inputHandler_;

    // Terminal state
    int rows_;
    int cols_;
    bool focusedBorder_;

    // Cursor
    bool cursorVisible_;
    QTimer* blinkTimer_;

    static constexpr int DEFAULT_ROWS = 16;
    static constexpr int DEFAULT_COLS = 40;
    static constexpr int CURSOR_BLINK_MS = 500;
};

#endif /* UI_TERMINAL_WIDGET_H */

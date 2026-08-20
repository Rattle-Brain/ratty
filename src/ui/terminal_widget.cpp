/*
 * TerminalWidget - OpenGL terminal view implementation
 */

#include "terminal_widget.h"
#include "../config/config.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QKeyCombination>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

TerminalWidget::TerminalWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    /*
     * The surface format is set once for the whole application in main(); doing
     * it per-widget as well only risked the two disagreeing. What does belong
     * here is the absence of multisampling: MSAA cannot help alpha-blended
     * glyph quads (they have no geometric edges to smooth) and only adds a
     * resolve blit that softens the result.
     */
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(200, 100);

    blinkTimer_ = new QTimer(this);
    blinkTimer_->setInterval(CursorBlinkMs);
    connect(blinkTimer_, &QTimer::timeout, this, &TerminalWidget::onBlinkTick);
}

TerminalWidget::~TerminalWidget() {
    /* A no-op if the context was already destroyed and took the renderer with
     * it, which is the usual case. */
    releaseGLResources();
}

double TerminalWidget::scaleFactor() const {
    const double ratio = devicePixelRatioF();
    return ratio > 0.0 ? ratio : 1.0;
}

int TerminalWidget::framebufferWidth() const {
    return std::max(1, static_cast<int>(std::lround(width() * scaleFactor())));
}

int TerminalWidget::framebufferHeight() const {
    return std::max(1, static_cast<int>(std::lround(height() * scaleFactor())));
}

int TerminalWidget::paddingPixels() const {
    /* Config stores logical pixels so the gap looks the same on every display;
     * everything in the renderer is physical. */
    return static_cast<int>(std::lround(Config::instance().windowPadding() * scaleFactor()));
}

void TerminalWidget::initializeGL() {
    initializeOpenGLFunctions();

    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (context) {
        const QSurfaceFormat format = context->format();
        if (format.majorVersion() < 3
            || (format.majorVersion() == 3 && format.minorVersion() < 3)) {
            qCritical() << "TerminalWidget: OpenGL 3.3 core required, got"
                        << format.majorVersion() << "." << format.minorVersion();
            return;
        }

        /*
         * Reparenting a QOpenGLWidget -- which is exactly what splitting a pane
         * does -- destroys its context and creates a new one, so this runs more
         * than once. Everything the previous renderer owned belongs to the dead
         * context, and must be released while that context is still current.
         */
        connect(context, &QOpenGLContext::aboutToBeDestroyed,
                this, &TerminalWidget::releaseGLResources, Qt::UniqueConnection);
    }

    /* GL resources cannot outlive their context, so the renderer is rebuilt. */
    renderer_ = std::make_unique<GLRenderer>();
    if (!renderer_->initialize()) {
        qCritical() << "TerminalWidget: renderer initialization failed";
        renderer_.reset();
        return;
    }

    if (!applyFontScale()) {
        qCritical() << "TerminalWidget: no usable font, nothing can be drawn";
        return;
    }

    layout_ = TerminalRenderer::computeLayout(renderer_->fontMetrics(),
                                             framebufferWidth(), framebufferHeight(),
                                             paddingPixels());

    /*
     * The session must NOT be rebuilt. It owns the pty and the shell, neither of
     * which has anything to do with the GL context: recreating it here killed the
     * running shell and replaced it with an empty one every time a pane was
     * split, which looked exactly like the terminal going blank.
     */
    if (session_) {
        if (layout_.isValid()) {
            session_->resize(layout_.rows, layout_.cols);
        }
        restartBlink();
        update();
        return;
    }

    const int rows = layout_.isValid() ? layout_.rows : DefaultRows;
    const int cols = layout_.isValid() ? layout_.cols : DefaultCols;

    session_ = std::make_unique<TerminalSession>(rows, cols,
                                                Config::instance().palette(), this);
    session_->setScrollbackLines(Config::instance().scrollbackLines());
    session_->setAlternateScroll(Config::instance().alternateScroll());
    connect(session_.get(), &TerminalSession::screenChanged,
            this, &TerminalWidget::onScreenChanged);
    connect(session_.get(), &TerminalSession::ended,
            this, &TerminalWidget::sessionEnded);
    connect(session_.get(), &TerminalSession::titleChanged, this,
            [this](const QString& title) {
                title_ = title;
                emit titleChanged(title);
            });
    connect(session_.get(), &TerminalSession::bellRang, this, []() {
        QApplication::beep();
    });

    if (!session_->isValid()) {
        qCritical() << "TerminalWidget: could not start a shell";
    }

    restartBlink();
}

void TerminalWidget::releaseGLResources() {
    /*
     * Called from QOpenGLContext::aboutToBeDestroyed, where the outgoing context
     * is still current -- the only moment at which these objects can be deleted
     * correctly. Deleting them later would issue GL calls against a context that
     * no longer exists.
     */
    if (!renderer_) return;

    makeCurrent();
    renderer_.reset();
    doneCurrent();
}

bool TerminalWidget::applyFontScale() {
    if (!renderer_) return false;

    const Config& config = Config::instance();
    const double scale = scaleFactor();

    /*
     * Points to physical pixels. Qt reports 72 logical DPI on macOS and 96 on
     * most of X11/Wayland, and the device pixel ratio carries the HiDPI factor
     * on top; multiplying the two is exactly how Qt sizes its own text.
     */
    const double logicalDpi = logicalDpiY() > 0 ? logicalDpiY() : 96.0;
    const double pixelSize = config.fontSize() * (logicalDpi / 72.0) * scale;

    if (!renderer_->setFont(config.fontFamilies(), config.fontFallbacks(), pixelSize)) {
        return false;
    }

    lastScaleFactor_ = scale;
    lastFontSize_ = config.fontSize();
    return true;
}

void TerminalWidget::reloadFont() {
    if (!renderer_) return;

    makeCurrent();
    if (applyFontScale()) {
        updateGeometryForFont();
    }
    doneCurrent();
    update();
}

void TerminalWidget::updateGeometryForFont() {
    if (!renderer_ || !renderer_->hasFont()) return;

    layout_ = TerminalRenderer::computeLayout(renderer_->fontMetrics(),
                                             framebufferWidth(), framebufferHeight(),
                                             paddingPixels());
    if (!layout_.isValid()) return;

    if (session_) {
        session_->resize(layout_.rows, layout_.cols);
    }
}

void TerminalWidget::resizeGL(int, int) {
    /*
     * No glViewport() call here: Qt sets the viewport to the device-pixel size
     * of its backing framebuffer immediately before every paintGL(), so setting
     * it from the logical size (as this used to) was both wrong and pointless.
     */
    if (!renderer_) return;

    /*
     * Moving to a screen with a different ratio changes how many pixels a point
     * is worth, so the font has to be re-rasterized before the layout is
     * recomputed. No makeCurrent() here: Qt invokes resizeGL() with the context
     * already current, and releasing it would leave Qt's own resize handling
     * without a context.
     */
    if (std::abs(scaleFactor() - lastScaleFactor_) > 0.001
        || Config::instance().fontSize() != lastFontSize_) {
        applyFontScale();
    }

    updateGeometryForFont();
}

void TerminalWidget::paintGL() {
    /*
     * The palette belongs to the session, not to Config: an application can
     * retheme its own terminal through OSC 4/10/11/12, and that must not leak
     * into other panes. Before a session exists, fall back to the configured
     * colours so the first frame is not black.
     */
    const Palette& palette = session_ ? session_->palette() : Config::instance().palette();
    const QColor background = palette.defaultBackground();

    if (!renderer_ || !renderer_->isInitialized()) {
        glClearColor(static_cast<GLfloat>(background.redF()),
                     static_cast<GLfloat>(background.greenF()),
                     static_cast<GLfloat>(background.blueF()), 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    renderer_->beginFrame(framebufferWidth(), framebufferHeight(), background);

    if (session_ && layout_.isValid()) {
        TerminalRenderer::Options options;
        options.cursorVisible = true;
        options.cursorPhaseOn = cursorPhaseOn_;
        options.cursorStyle = effectiveCursorStyle();

        gridRenderer_.paint(*renderer_, session_->screen(), palette, layout_, options);
    }

    renderer_->endFrame();

    if (renderer_->needsRepaint()) {
        /* The atlas grew while this frame was being built, so part of it was
         * dropped. Ask for one more pass; the atlas is now large enough. */
        update();
    }
}

CursorStyle TerminalWidget::effectiveCursorStyle() const {
    /* An unfocused pane shows a hollow cursor, which is how every tiling
     * terminal signals "input does not go here". */
    if (!hasFocus()) return CursorStyle::HollowBlock;

    /* An explicit DECSCUSR request from the application wins: editors use it to
     * signal their mode, and overriding that with the user's static preference
     * would throw the information away. */
    if (session_ && session_->hasRequestedCursorStyle()) {
        return session_->requestedCursorStyle();
    }
    return Config::instance().cursorStyle();
}

void TerminalWidget::onScreenChanged() {
    /*
     * Output resets the blink phase so the cursor is solid while text is
     * arriving; that is both conventional and avoids a cursor that appears to
     * flicker during long output.
     */
    cursorPhaseOn_ = true;
    restartBlink();
    update();
}

void TerminalWidget::onBlinkTick() {
    cursorPhaseOn_ = !cursorPhaseOn_;
    update();
}

void TerminalWidget::restartBlink() {
    if (!blinkTimer_) return;

    /* DECSCUSR also says whether the cursor should blink. */
    const bool blink = (session_ && session_->hasRequestedCursorStyle())
                           ? session_->cursorBlinkRequested()
                           : Config::instance().cursorBlink();

    if (blink && hasFocus()) {
        blinkTimer_->start();
    } else {
        /* A steady cursor costs no timer wakeups; the old code repainted the
         * whole grid twice a second regardless of focus or configuration. */
        blinkTimer_->stop();
        cursorPhaseOn_ = true;
    }
}

void TerminalWidget::setPaneFocused(bool focused) {
    if (paneFocused_ == focused) return;
    paneFocused_ = focused;
    update();
}

void TerminalWidget::keyPressEvent(QKeyEvent* event) {
    /*
     * Application keybindings get first refusal. This widget used to accept
     * *every* key event, so the shortcuts in MainWindow::keyPressEvent were
     * unreachable and no keybinding in the config file ever fired.
     */
    if (Config::instance().isBound(event)) {
        event->ignore();
        return;
    }

    if (!session_ || !session_->isValid()) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }

    const QByteArray bytes = inputHandler_.keyEventToBytes(event,
                                                           session_->applicationCursorKeys());
    if (bytes.isEmpty()) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }

    /*
     * Typing returns to the live screen. Without this the echo of what was just
     * typed would land out of sight, which feels like the terminal has stopped
     * responding.
     */
    if (session_->scrollViewToBottom()) update();

    session_->sendInput(bytes);
    event->accept();
}

/* ------------------------------------------------------------------- mouse */

bool TerminalWidget::cellAt(const QPointF& position, int& row, int& col) const {
    if (!layout_.isValid() || !session_) return false;

    /* Qt reports the position in logical pixels; the layout is physical. */
    const double scale = scaleFactor();
    const double x = position.x() * scale - layout_.originX;
    const double y = position.y() * scale - layout_.originY;

    const int rows = std::min(layout_.rows, session_->rows());
    const int cols = std::min(layout_.cols, session_->cols());

    col = std::clamp(static_cast<int>(std::floor(x / layout_.cellWidth)), 0, std::max(0, cols - 1));
    row = std::clamp(static_cast<int>(std::floor(y / layout_.cellHeight)), 0, std::max(0, rows - 1));
    return true;
}

bool TerminalWidget::applicationWantsMouse(Qt::KeyboardModifiers modifiers) const {
    if (!session_ || !session_->isValid()) return false;
    if (session_->mouseTracking() == MouseTracking::None) return false;

    /*
     * Shift is the local override, the same one xterm uses: it is what lets the
     * user scroll or paste while an application that has grabbed the mouse is
     * running.
     */
    if (modifiers & Qt::ShiftModifier) return false;

    /* Scrolled back, the row under the pointer is a history row and reporting it
     * would tell the application about a position on a screen it cannot see. */
    return !session_->scrolledBack();
}

bool TerminalWidget::reportMouse(MouseAction action, MouseButton button,
                                 const QPointF& position, Qt::KeyboardModifiers modifiers) {
    if (!applicationWantsMouse(modifiers)) return false;

    MouseReport report;
    report.action = action;
    report.button = button;
    if (!cellAt(position, report.row, report.col)) return false;

    report.modifiers.shift = (modifiers & Qt::ShiftModifier) != 0;
    report.modifiers.alt = (modifiers & Qt::AltModifier) != 0;
    report.modifiers.control = (modifiers & Qt::ControlModifier) != 0;

    session_->sendMouseReport(report);

    /*
     * Report or not, the application asked for the mouse, so the event is
     * consumed: a click in a `vim` window must not also paste.
     */
    return true;
}

namespace {

MouseButton mouseButtonFor(Qt::MouseButton button) {
    switch (button) {
    case Qt::LeftButton:   return MouseButton::Left;
    case Qt::MiddleButton: return MouseButton::Middle;
    case Qt::RightButton:  return MouseButton::Right;
    default:               return MouseButton::None;
    }
}

/* The button a motion event should be attributed to: the lowest-numbered one
 * still held, which is the convention every terminal follows. */
MouseButton heldButtonFor(Qt::MouseButtons buttons) {
    if (buttons & Qt::LeftButton)   return MouseButton::Left;
    if (buttons & Qt::MiddleButton) return MouseButton::Middle;
    if (buttons & Qt::RightButton)  return MouseButton::Right;
    return MouseButton::None;
}

} // namespace

void TerminalWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    event->accept();

    lastMotionRow_ = -1;
    lastMotionCol_ = -1;

    if (reportMouse(MouseAction::Press, mouseButtonFor(event->button()),
                    event->position(), event->modifiers())) {
        return;
    }

    /* Middle click pastes the primary selection on X11; on platforms without
     * one Qt returns the clipboard, which is close enough to be useful. */
    if (event->button() == Qt::MiddleButton) {
        paste();
    }
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* event) {
    event->accept();
    reportMouse(MouseAction::Release, mouseButtonFor(event->button()),
                event->position(), event->modifiers());
}

void TerminalWidget::mouseMoveEvent(QMouseEvent* event) {
    event->accept();

    if (!applicationWantsMouse(event->modifiers())) return;

    const MouseTracking tracking = session_->mouseTracking();
    if (tracking != MouseTracking::ButtonEvent && tracking != MouseTracking::AnyEvent) {
        return;
    }

    /*
     * Mouse tracking is on for the whole widget, so this fires per pixel. A
     * report is only interesting when the pointer changes cell -- otherwise a
     * slow drag across one character floods the application (and the pty) with
     * identical reports.
     */
    int row = 0;
    int col = 0;
    if (!cellAt(event->position(), row, col)) return;
    if (row == lastMotionRow_ && col == lastMotionCol_) return;
    lastMotionRow_ = row;
    lastMotionCol_ = col;

    reportMouse(MouseAction::Move, heldButtonFor(event->buttons()),
                event->position(), event->modifiers());
}

int TerminalWidget::consumeWheelNotches(const QWheelEvent* event) {
    /*
     * Trackpads and high-resolution wheels send fractions of a notch, so the
     * remainder is carried between events; taking angleDelta() one event at a
     * time would either ignore small deltas or scroll a full notch per pixel.
     */
    wheelRemainder_ += event->angleDelta().y();
    const int notches = wheelRemainder_ / WheelNotch;
    wheelRemainder_ -= notches * WheelNotch;
    return notches;
}

void TerminalWidget::sendCursorKey(char final, int count) {
    if (!session_ || count <= 0) return;

    /* DECCKM decides the form, exactly as it does for the arrow keys. */
    const QByteArray prefix = session_->applicationCursorKeys() ? QByteArray("\x1bO")
                                                               : QByteArray("\x1b[");
    QByteArray bytes;
    bytes.reserve((prefix.size() + 1) * count);
    for (int i = 0; i < count; ++i) {
        bytes += prefix;
        bytes += final;
    }
    session_->sendInput(bytes);
}

void TerminalWidget::wheelEvent(QWheelEvent* event) {
    /* Accepted unconditionally: an unhandled wheel event would propagate to the
     * tab bar and switch tabs, which is never what was meant. */
    event->accept();
    if (!session_) return;

    const int notches = consumeWheelNotches(event);
    if (notches == 0) return;

    const int lines = std::abs(notches) * Config::instance().scrollMultiplier();

    /* An application that asked for the mouse gets the wheel too, as button 4/5
     * presses -- that is how a TUI scrolls its own panes. */
    if (applicationWantsMouse(event->modifiers())) {
        const MouseButton button = notches > 0 ? MouseButton::WheelUp
                                               : MouseButton::WheelDown;
        for (int i = 0; i < std::abs(notches); ++i) {
            reportMouse(MouseAction::Press, button, event->position(), event->modifiers());
        }
        return;
    }

    /*
     * The alternate screen has no scrollback to move -- a full-screen
     * application repaints instead of scrolling -- so the wheel is translated
     * into cursor keys. Without this the wheel does nothing at all in `less` or
     * `man`, which reads as a broken terminal.
     */
    if (session_->alternateScreenActive()) {
        if (session_->alternateScroll() && !(event->modifiers() & Qt::ShiftModifier)) {
            sendCursorKey(notches > 0 ? 'A' : 'B', lines);
        }
        return;
    }

    scrollLines(notches > 0 ? lines : -lines);
}

/* -------------------------------------------------------------- scrollback */

void TerminalWidget::scrollLines(int lines) {
    if (!session_ || lines == 0) return;
    if (session_->scrollViewBy(lines)) update();
}

void TerminalWidget::scrollPages(int pages) {
    /* One row of overlap between pages, so the line the eye stopped on is still
     * there after the jump. */
    const int rows = layout_.isValid() ? layout_.rows : DefaultRows;
    scrollLines(pages * std::max(1, rows - 1));
}

void TerminalWidget::scrollToTop() {
    if (session_ && session_->scrollViewToTop()) update();
}

void TerminalWidget::scrollToBottom() {
    if (session_ && session_->scrollViewToBottom()) update();
}

void TerminalWidget::clearScrollback() {
    if (!session_) return;
    session_->clearScrollback();
    update();
}

int TerminalWidget::viewOffset() const {
    return session_ ? session_->viewOffset() : 0;
}

int TerminalWidget::historySize() const {
    return session_ ? session_->historySize() : 0;
}

void TerminalWidget::focusInEvent(QFocusEvent* event) {
    QOpenGLWidget::focusInEvent(event);
    /* DECSET 1004: applications that track focus dim their cursor or pause an
     * animation when the window loses it. */
    if (session_) session_->sendFocusEvent(true);
    restartBlink();
    update();
}

void TerminalWidget::focusOutEvent(QFocusEvent* event) {
    QOpenGLWidget::focusOutEvent(event);
    if (session_) session_->sendFocusEvent(false);
    restartBlink();
    update();
}

void TerminalWidget::copySelection() {
    /* Text selection is not implemented yet; see todo-ratty.md. */
    qInfo() << "TerminalWidget: copy requested, but text selection is not implemented";
}

void TerminalWidget::paste() {
    if (!session_ || !session_->isValid()) return;

    const QClipboard* clipboard = QApplication::clipboard();
    session_->sendPaste(clipboard->text());
}

/*
 * TerminalWidget - terminal pane implementation
 */

#include "terminal_widget.h"
#include "terminal_canvas.h"
#include "../config/config.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QEvent>
#include <QtGlobal>
#include <QInputMethodEvent>
#include <QKeyCombination>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

TerminalWidget::TerminalWidget(QWidget* parent, const QString& startDirectory)
    : QWidget(parent)
    , startDirectory_(startDirectory)
{
    /*
     * The surface format is set once for the whole application in main(); doing
     * it per-widget as well only risked the two disagreeing. What does belong
     * here is the absence of multisampling: MSAA cannot help alpha-blended
     * glyph quads (they have no geometric edges to smooth) and only adds a
     * resolve blit that softens the result.
     */
    setFocusPolicy(Qt::StrongFocus);
    /*
     * Composed input has to be asked for.
     *
     * On a Spanish keyboard `~` is a *dead key* -- Option+n-tilde on macOS,
     * AltGr+n-tilde on Linux -- and so is every accent (the acute, then `a`,
     * for `a`-acute). None of that arrives as a key event carrying text: the
     * platform input method holds the half-finished composition and delivers
     * the result as a QInputMethodEvent, but only to a widget with this
     * attribute set. Without it the composition is silently dropped, which is
     * why the tilde could not be typed on either platform, and why no input
     * method (CJK and friends) worked either.
     */
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setMouseTracking(true);
    /*
     * This pane never paints. The shared canvas is stacked over it and draws it,
     * so Qt should not spend a backing-store fill on a widget that is always
     * covered -- and must not clear it to a colour that would flash through
     * during a resize.
     */
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setMinimumSize(200, 100);

    blinkTimer_ = new QTimer(this);
    blinkTimer_->setInterval(CursorBlinkMs);
    connect(blinkTimer_, &QTimer::timeout, this, &TerminalWidget::onBlinkTick);
}

TerminalWidget::~TerminalWidget() {
    if (TerminalCanvas* c = canvas()) c->removePane(this);
}

TerminalCanvas* TerminalWidget::canvas() const {
    return TerminalCanvas::forWidget(this);
}

void TerminalWidget::requestRepaint() {
    if (TerminalCanvas* c = canvas()) c->update();
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

void TerminalWidget::ensureSession() {
    /*
     * The session owns the pty and the shell. It is created once, lazily, as
     * soon as the canvas can tell us how big a grid fits -- and never rebuilt.
     * When panes owned their own GL context this ran from initializeGL(), which
     * Qt calls again on every reparent; rebuilding the session there killed the
     * running shell every time a pane was split. There is no context to lose
     * now, but the invariant is worth keeping stated: nothing below re-creates
     * a session that already exists.
     */
    if (session_) return;

    TerminalCanvas* c = canvas();
    GLRenderer* renderer = c ? c->renderer() : nullptr;
    if (renderer && renderer->hasFont()) {
        layout_ = TerminalRenderer::computeLayout(renderer->fontMetrics(),
                                                 framebufferWidth(), framebufferHeight(),
                                                 paddingPixels());
    }

    const int rows = layout_.isValid() ? layout_.rows : DefaultRows;
    const int cols = layout_.isValid() ? layout_.cols : DefaultCols;

    session_ = std::make_unique<TerminalSession>(rows, cols,
                                                Config::instance().palette(),
                                                startDirectory_, this);
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

void TerminalWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    /*
     * Register with the window's surface, creating one if this pane tree is
     * being used outside a MainWindow. Done on show rather than in the
     * constructor because a pane is built before it is put in its window, and
     * until then window() is not the window it will end up in.
     */
    if (TerminalCanvas* c = TerminalCanvas::ensureFor(this)) {
        c->addPane(this);
        c->syncGeometry();
        /*
         * Bring the font up now, but leave the session to the first frame.
         *
         * A pane is shown before its splitter has laid it out, so at this point
         * it is still at its minimum size -- creating the session here told the
         * shell a 5x24 grid and left it that way. By the first frame the
         * geometry is settled and the grid is the real one.
         */
        c->ensureReady();
        c->update();
    }
    scheduleSessionStart();
}

void TerminalWidget::scheduleSessionStart() {
    if (session_ || sessionStartPosted_) return;
    sessionStartPosted_ = true;

    /*
     * Once the event loop has laid this pane out, size the grid and start the
     * shell -- whether or not a frame has been drawn yet.
     *
     * Hanging this off the first paint instead meant a pane that was never
     * painted never started its shell at all, which is not hypothetical: a
     * compositor that does not expose the window (a headless Wayland session,
     * a window opened minimised) produces no frames, and the terminal would sit
     * there with no shell in it.
     */
    QTimer::singleShot(0, this, [this]() {
        sessionStartPosted_ = false;
        if (session_) return;
        if (TerminalCanvas* c = canvas()) c->ensureReady();
        updateGeometryForFont();
        ensureSession();
        requestRepaint();
    });
}

QImage TerminalWidget::grabFramebuffer() const {
    TerminalCanvas* c = canvas();
    return c ? c->grabPane(this) : QImage();
}

void TerminalWidget::reloadFont() {
    /*
     * The font belongs to the window, not the pane: one canvas, one atlas, one
     * rasterization. Asking the canvas to redo it re-lays-out every pane, which
     * is what a font change requires anyway.
     */
    if (TerminalCanvas* c = canvas()) {
        c->invalidateFont();
        c->update();
    }
}

void TerminalWidget::applyConfiguration() {
    const Config& config = Config::instance();

    if (session_) {
        session_->setScrollbackLines(config.scrollbackLines());
        session_->setAlternateScroll(config.alternateScroll());
        session_->setBasePalette(config.palette());
    }

    /*
     * reloadFont() covers the font *and* the geometry that depends on it, which
     * includes the window padding: a changed padding moves the grid origin and
     * can change the row and column count, so the session has to be resized to
     * match. Cheap when nothing changed -- an identical font request hands back
     * the same shared chain and the glyph atlas is left alone.
     */
    reloadFont();

    /* The cursor's style and whether it blinks are both settings. */
    restartBlink();
    requestRepaint();
}

void TerminalWidget::updateGeometryForFont() {
    TerminalCanvas* c = canvas();
    GLRenderer* renderer = c ? c->renderer() : nullptr;
    if (!renderer || !renderer->hasFont()) return;

    layout_ = TerminalRenderer::computeLayout(renderer->fontMetrics(),
                                             framebufferWidth(), framebufferHeight(),
                                             paddingPixels());
    if (!layout_.isValid()) return;

    if (session_) {
        session_->resize(layout_.rows, layout_.cols);
    }
}

bool TerminalWidget::event(QEvent* event) {
    /* Let Qt react first, so devicePixelRatioF() already reports the new value
     * by the time the cases below look at it. */
    const bool result = QWidget::event(event);

    switch (event->type()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    /*
     * Qt 6.6 is the first version that says this directly. Guarded rather than
     * simply required, because needing 6.6 to build would rule out every
     * current Debian and Ubuntu LTS for the sake of one enumerator -- and on
     * older Qt the screen-change event below plus the canvas's own staleness
     * check cover exactly the same ground.
     */
    case QEvent::DevicePixelRatioChange:
#endif
    case QEvent::ScreenChangeInternal:
        /*
         * Moving the window between displays does not necessarily resize the
         * widget -- the logical geometry is unchanged -- so a resize may never
         * come, and without this nothing would notice the new ratio at all.
         *
         * The canvas re-rasterizes on its next frame rather than here: it owns
         * the font and the context, and doing it during an event that may be
         * arriving at a hidden pane would be the wrong moment.
         */
        if (TerminalCanvas* c = canvas()) {
            c->invalidateFont();
            c->update();
        }
        break;
    default:
        break;
    }

    return result;
}

bool TerminalWidget::focusNextPrevChild(bool /*next*/) {
    /*
     * Tab belongs to the shell, so this pane never gives it up.
     *
     * QWidget::event() consumes Key_Tab and Key_Backtab for focus traversal
     * *before* keyPressEvent() is called, and only falls through to the key
     * handler if traversal found nothing to move to. With a single pane there is
     * nothing, so Tab worked; the moment the window was split there was, and Tab
     * moved the caret to the other pane instead of completing a filename.
     *
     * Refusing traversal here is what lets the event reach keyPressEvent(),
     * where InputHandler encodes it as HT -- and Shift+Tab as CBT, which is what
     * shells bind to reverse completion.
     *
     * Moving between panes is deliberately on its own bindings
     * (focus_left/right/up/down): a terminal cannot afford to spend Tab on
     * window management.
     */
    return false;
}

void TerminalWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    /*
     * The pane moved or changed size, so the canvas has to redraw it somewhere
     * new -- and the grid it covers has to be recomputed and pushed to the
     * shell. syncGeometry() is here too because a resize of any pane is also
     * how the page area itself changes size.
     */
    updateGeometryForFont();
    if (TerminalCanvas* c = canvas()) {
        c->syncGeometry();
        c->update();
    }
}

void TerminalWidget::renderInto(GLRenderer& renderer, int left, int bottom,
                                int paneWidth, int paneHeight) {
    /*
     * The session cannot be created before the canvas has a font, because the
     * shell has to be told a grid size at start-up. The first frame is
     * therefore also where the session comes from.
     */
    ensureSession();

    /*
     * The palette belongs to the session, not to Config: an application can
     * retheme its own terminal through OSC 4/10/11/12, and that must not leak
     * into other panes. Before a session exists, fall back to the configured
     * colours so the first frame is not black.
     */
    const Palette& palette = session_ ? session_->palette() : Config::instance().palette();
    const QColor background = palette.defaultBackground();

    /*
     * The viewport puts this pane's origin at (0, 0), so everything below is in
     * the pane's own coordinate space and knows nothing about where on the
     * shared surface it landed.
     */
    renderer.beginFrame(left, bottom, paneWidth, paneHeight, background);

    if (session_ && layout_.isValid()) {
        TerminalRenderer::Options options;
        options.cursorVisible = true;
        options.cursorPhaseOn = cursorPhaseOn_;
        options.cursorStyle = effectiveCursorStyle();

        gridRenderer_.paint(renderer, session_->screen(), palette, layout_, options);
    }

    /*
     * A pane that is not the current one fades back, so which one the keyboard
     * is talking to is obvious without hunting for the cursor.
     *
     * Done as one translucent quad in the background colour over the finished
     * frame rather than by dimming the colours themselves: it costs six vertices
     * instead of a second pass through the palette for every cell, it dims the
     * cursor and the decorations along with the text, and -- because it fades
     * *towards the background* rather than towards black -- it reads correctly on
     * a light theme as well as a dark one.
     */
    if (paneDimmed_ && Config::instance().dimUnfocusedSplits()) {
        QColor veil = background;
        veil.setAlphaF(Config::instance().splitDimStrength());
        renderer.fillOverlay(0, 0, paneWidth, paneHeight, veil);
    }

    renderer.endFrame();
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
    requestRepaint();
}

void TerminalWidget::onBlinkTick() {
    cursorPhaseOn_ = !cursorPhaseOn_;
    requestRepaint();
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
    requestRepaint();
}

void TerminalWidget::setPaneDimmed(bool dimmed) {
    if (paneDimmed_ == dimmed) return;
    paneDimmed_ = dimmed;
    requestRepaint();
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
        QWidget::keyPressEvent(event);
        return;
    }

    const QByteArray bytes = inputHandler_.keyEventToBytes(event,
                                                           session_->applicationCursorKeys());
    if (bytes.isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }

    /*
     * Typing returns to the live screen. Without this the echo of what was just
     * typed would land out of sight, which feels like the terminal has stopped
     * responding.
     */
    if (session_->scrollViewToBottom()) requestRepaint();

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
    /* Named rather than the default Qt::OtherFocusReason, and announced: a
     * click is the user picking this pane, which is what the window's focus
     * history is built from. */
    setFocus(Qt::MouseFocusReason);
    emit paneActivated();
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
    if (session_->scrollViewBy(lines)) requestRepaint();
}

void TerminalWidget::scrollPages(int pages) {
    /* One row of overlap between pages, so the line the eye stopped on is still
     * there after the jump. */
    const int rows = layout_.isValid() ? layout_.rows : DefaultRows;
    scrollLines(pages * std::max(1, rows - 1));
}

void TerminalWidget::scrollToTop() {
    if (session_ && session_->scrollViewToTop()) requestRepaint();
}

void TerminalWidget::scrollToBottom() {
    if (session_ && session_->scrollViewToBottom()) requestRepaint();
}

void TerminalWidget::clearScrollback() {
    if (!session_) return;
    session_->clearScrollback();
    requestRepaint();
}

int TerminalWidget::viewOffset() const {
    return session_ ? session_->viewOffset() : 0;
}

int TerminalWidget::historySize() const {
    return session_ ? session_->historySize() : 0;
}

void TerminalWidget::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    /* DECSET 1004: applications that track focus dim their cursor or pause an
     * animation when the window loses it. */
    if (session_) session_->sendFocusEvent(true);
    restartBlink();
    requestRepaint();
}

void TerminalWidget::inputMethodEvent(QInputMethodEvent* event) {
    if (!event) return;

    /*
     * The commit string is what the composition finally resolved to: the `~` of
     * a dead-key tilde, the `a`-acute of an accent, a run of CJK chosen from a
     * candidate window. It is ordinary input and goes to the shell as UTF-8.
     *
     * The preedit string is the composition still in progress, which the
     * platform would like drawn under the cursor. RaTTY does not draw it yet
     * (todo-ratty.md); the event is accepted regardless, because refusing it
     * abandons the composition instead of letting it finish.
     */
    const QByteArray committed = event->commitString().toUtf8();
    if (!committed.isEmpty() && session_ && session_->isValid()) {
        /* Same courtesy as typing: bring the live screen back into view first,
         * or the echo of what was just composed lands out of sight. */
        if (session_->scrollViewToBottom()) requestRepaint();
        session_->sendInput(committed);
    }

    event->accept();
}

QRectF TerminalWidget::cursorRectangle() const {
    if (!layout_.isValid() || !session_) return QRectF(0, 0, 1, 1);

    /* The layout is in physical pixels and Qt wants logical ones; cellAt() does
     * this conversion in the other direction. */
    const double scale = scaleFactor();
    if (scale <= 0.0) return QRectF(0, 0, 1, 1);

    const Screen& screen = session_->screen();
    const int row = std::clamp(screen.cursorRow(), 0, std::max(0, layout_.rows - 1));
    const int col = std::clamp(screen.cursorCol(), 0, std::max(0, layout_.cols - 1));

    return QRectF((layout_.originX + col * layout_.cellWidth) / scale,
                  (layout_.originY + row * layout_.cellHeight) / scale,
                  layout_.cellWidth / scale, layout_.cellHeight / scale);
}

QVariant TerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const {
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle:
        return cursorRectangle();
    case Qt::ImFont:
        return font();
    case Qt::ImHints:
        /* A terminal wants the characters as typed. Autocorrect,
         * capitalisation and predictive text would all rewrite them. */
        return static_cast<int>(Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText
                                | Qt::ImhNoTextHandles);
    case Qt::ImSurroundingText:
        /* There is no editable buffer on this side: the shell owns the line. */
        return QString();
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition:
        return 0;
    default:
        return QWidget::inputMethodQuery(query);
    }
}

void TerminalWidget::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    if (session_) session_->sendFocusEvent(false);
    restartBlink();
    requestRepaint();
}

QString TerminalWidget::workingDirectory() const {
    return session_ ? session_->workingDirectory() : QString();
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

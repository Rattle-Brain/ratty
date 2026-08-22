/*
 * TerminalWidget - terminal pane implementation
 */

#include "terminal_widget.h"
#include "terminal_canvas.h"
#include "../config/config.h"
#include <QApplication>
#include <QClipboard>
#include <QCursor>
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

    /*
     * A drag held past the top or bottom edge keeps scrolling, which needs a
     * timer: the pointer is not moving, so there are no more mouse events to
     * hang it off.
     */
    autoScrollTimer_ = new QTimer(this);
    autoScrollTimer_->setInterval(AutoScrollMs);
    connect(autoScrollTimer_, &QTimer::timeout, this, &TerminalWidget::onAutoScrollTick);

    clickTimer_.start();
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

    applyClipboardPolicy();
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
        /* Whether an application may reach the clipboard is a setting, so a
         * reload has to be able to withdraw the permission as well as grant
         * it. */
        applyClipboardPolicy();
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

    if (!session_) return;

    const int previousRows = session_->rows();
    const int previousCols = session_->cols();
    session_->resize(layout_.rows, layout_.cols);

    if (session_->rows() != previousRows || session_->cols() != previousCols) {
        /*
         * A width change rewraps the buffer, so the line numbers a selection or
         * a search match are held in no longer name the text they named -- see
         * Screen::reflow(). The selection goes; a search that is open is simply
         * run again against the new layout.
         */
        clearSelection();
        if (searchActive_) {
            refreshSearch();
        } else {
            searchMatches_.clear();
            searchIndex_ = -1;
        }
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

        if (!selection_.isEmpty()) options.selection = &selection_;
        if (searchActive_) {
            options.statusLine = &statusLine_;
            if (!searchMatches_.empty()) {
                options.matches = &searchMatches_;
                options.currentMatch = searchIndex_;
            }
        }
        options.scrollIndicator = Config::instance().scrollIndicator()
                               && session_->scrolledBack();

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
    /* An application putting the alternate screen up or taking it down changes
     * which buffer -- and so which line numbering -- a selection would refer
     * to; see alternateScreenActive_. */
    if (session_ && session_->alternateScreenActive() != alternateScreenActive_) {
        alternateScreenActive_ = session_->alternateScreenActive();
        selection_.clear();
        searchMatches_.clear();
        searchIndex_ = -1;
        if (searchActive_) endSearch();
    }

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

    /*
     * With the search prompt open the keyboard belongs to it -- but only after
     * the bindings above, so a shortcut still works while searching.
     */
    if (searchActive_ && handleSearchKey(event)) {
        event->accept();
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

    /* Typing invalidates a selection the way it does in every terminal: the
     * text is about to move, and a highlight left behind on it reads as a bug. */
    clearSelection();

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

    if (event->button() == Qt::MiddleButton) {
        /*
         * Middle click pastes the primary selection where the platform has one
         * -- which is X11's convention and what a selection made here has just
         * been put on -- and the clipboard everywhere else.
         */
        const QClipboard* clipboard = QApplication::clipboard();
        if (clipboard && session_ && session_->isValid()) {
            session_->sendPaste(clipboard->text(clipboard->supportsSelection()
                                                    ? QClipboard::Selection
                                                    : QClipboard::Clipboard));
        }
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    beginSelection(event->position(), countClick(event->position()), event->modifiers());
}

void TerminalWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    /* Qt's second click, counted by countClick() like any other press. */
    mousePressEvent(event);
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* event) {
    event->accept();

    if (dragging_ && event->button() == Qt::LeftButton) {
        finishSelection();
        return;
    }

    reportMouse(MouseAction::Release, mouseButtonFor(event->button()),
                event->position(), event->modifiers());
}

void TerminalWidget::mouseMoveEvent(QMouseEvent* event) {
    event->accept();

    if (dragging_) {
        updateAutoScroll(event->position());
        extendSelection(event->position());
        return;
    }

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
    /* The lines a selection or a match named have just been thrown away. */
    selection_.clear();
    searchMatches_.clear();
    searchIndex_ = -1;
    if (searchActive_) refreshSearch();
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

/* ---------------------------------------------------------------- selection */

bool TerminalWidget::selectionPointAt(const QPointF& position, SelectionPoint& point) const {
    if (!session_) return false;

    int row = 0;
    int col = 0;
    if (!cellAt(position, row, col)) return false;

    /* cellAt() answers in view rows; a selection is held in the buffer's own
     * line numbers, so that it stays on its text as the view moves. */
    point.line = session_->screen().viewTopLine() + row;
    point.col = col;
    return true;
}

int TerminalWidget::countClick(const QPointF& position) {
    int row = 0;
    int col = 0;
    if (!cellAt(position, row, col)) return 1;

    /*
     * Qt reports a double click but has no notion of a third, and a terminal
     * needs one -- so the counting is done here, against the platform's own
     * double-click interval and the cell the previous click landed on. Past
     * three it starts again at one, so a fourth click is a plain click.
     */
    const bool sameCell = row == lastClickRow_ && col == lastClickCol_;
    const bool inTime = clickTimer_.isValid()
                     && clickTimer_.elapsed() <= QApplication::doubleClickInterval();
    clickCount_ = (sameCell && inTime) ? clickCount_ % 3 + 1 : 1;

    lastClickRow_ = row;
    lastClickCol_ = col;
    clickTimer_.restart();
    return clickCount_;
}

void TerminalWidget::beginSelection(const QPointF& position, int clickCount,
                                    Qt::KeyboardModifiers modifiers) {
    SelectionPoint point;
    if (!selectionPointAt(position, point)) return;

    /*
     * Alt is the rectangular modifier, not Ctrl or Shift. Shift is spoken for:
     * it is what bypasses an application's mouse grab, so it is held for most
     * selections made inside a TUI and cannot mean anything else. Ctrl+click is
     * the platform's context menu on macOS.
     */
    SelectionMode mode = SelectionMode::Character;
    if (modifiers & Qt::AltModifier)   mode = SelectionMode::Block;
    else if (clickCount == 2)          mode = SelectionMode::Word;
    else if (clickCount >= 3)          mode = SelectionMode::Line;

    dragging_ = true;
    selection_.begin(session_->screen(), point, mode);
    requestRepaint();
}

void TerminalWidget::extendSelection(const QPointF& position) {
    SelectionPoint point;
    if (!selectionPointAt(position, point)) return;
    selection_.extend(session_->screen(), point);
    requestRepaint();
}

void TerminalWidget::finishSelection() {
    dragging_ = false;
    autoScrollDirection_ = 0;
    if (autoScrollTimer_) autoScrollTimer_->stop();
    selection_.finishDrag();

    /*
     * A click that never moved is a click, not a selection: it clears whatever
     * was selected rather than leaving a one-character highlight behind. A word
     * or line selection is deliberate even without movement, and a rectangular
     * one is asked for explicitly, so both stand.
     */
    if (selection_.mode() == SelectionMode::Character
        && selection_.range().start == selection_.range().end) {
        clearSelection();
        return;
    }

    publishSelection(Config::instance().copyOnSelect());
    requestRepaint();
}

void TerminalWidget::clearSelection() {
    if (selection_.isEmpty()) return;
    selection_.clear();
    requestRepaint();
}

void TerminalWidget::publishSelection(bool toClipboard) {
    if (!session_ || selection_.isEmpty()) return;

    const std::u32string text = selection_.text(session_->screen());
    if (text.empty()) return;

    QClipboard* clipboard = QApplication::clipboard();
    if (!clipboard) return;

    const QString value = QString::fromUcs4(text.data(),
                                            static_cast<qsizetype>(text.size()));

    /*
     * The primary selection is set by the act of selecting, which is what makes
     * middle-click paste work; the clipboard is only touched deliberately, by a
     * copy or by copy-on-select, because it is where the user keeps something
     * they mean to keep.
     */
    if (clipboard->supportsSelection()) {
        clipboard->setText(value, QClipboard::Selection);
    }
    if (toClipboard) {
        clipboard->setText(value, QClipboard::Clipboard);
    }
}

void TerminalWidget::updateAutoScroll(const QPointF& position) {
    /* Positive is towards the past, matching scrollLines(). */
    int direction = 0;
    if (position.y() < 0)               direction = 1;
    else if (position.y() > height())   direction = -1;

    if (direction == autoScrollDirection_) return;
    autoScrollDirection_ = direction;
    if (!autoScrollTimer_) return;

    if (direction == 0) autoScrollTimer_->stop();
    else                autoScrollTimer_->start();
}

void TerminalWidget::onAutoScrollTick() {
    if (!dragging_ || autoScrollDirection_ == 0) {
        if (autoScrollTimer_) autoScrollTimer_->stop();
        return;
    }

    scrollLines(autoScrollDirection_ * AutoScrollLines);
    /* The pointer has not moved, but the line under it has, so the far end of
     * the selection has to be taken again. */
    extendSelection(mapFromGlobal(QCursor::pos()));
}

void TerminalWidget::applyClipboardPolicy() {
    if (!session_) return;

    const Config& config = Config::instance();
    TerminalSession::ClipboardSetter setter;
    TerminalSession::ClipboardGetter getter;

    if (config.clipboardWriteAllowed()) {
        setter = [](const QString& text, bool primary) {
            QClipboard* clipboard = QApplication::clipboard();
            if (!clipboard) return;
            const bool usePrimary = primary && clipboard->supportsSelection();
            clipboard->setText(text, usePrimary ? QClipboard::Selection
                                                : QClipboard::Clipboard);
        };
    }
    if (config.clipboardReadAllowed()) {
        getter = [](bool primary) -> QString {
            const QClipboard* clipboard = QApplication::clipboard();
            if (!clipboard) return QString();
            const bool usePrimary = primary && clipboard->supportsSelection();
            return clipboard->text(usePrimary ? QClipboard::Selection
                                              : QClipboard::Clipboard);
        };
    }

    session_->setClipboardHandlers(std::move(setter), std::move(getter));
}

void TerminalWidget::copySelection() {
    if (selection_.isEmpty()) return;
    publishSelection(/*toClipboard=*/true);
}

/* ------------------------------------------------------------------- search */

void TerminalWidget::beginSearch() {
    if (!session_) return;

    searchActive_ = true;
    /* A fresh prompt starts empty; the previous query is kept only for
     * find_next, which is how a closed search can be resumed. */
    searchQuery_.clear();
    refreshSearch();
}

void TerminalWidget::endSearch() {
    if (!searchActive_) return;

    searchActive_ = false;
    statusLine_.clear();
    /*
     * The current match stays selected and the query is remembered: closing the
     * prompt to copy what was found, or to step on with find_next, are both
     * things the user does next.
     */
    requestRepaint();
}

void TerminalWidget::refreshSearch() {
    searchMatches_.clear();
    searchIndex_ = -1;
    searchTruncated_ = false;

    if (session_ && !searchQuery_.empty()) {
        const SearchResults results = searchScrollback(session_->screen(), searchQuery_);
        searchMatches_ = results.matches;
        searchTruncated_ = results.truncated;
        if (!searchMatches_.empty()) {
            /* Start at the newest match: what is being looked for in a
             * scrollback is usually what happened most recently. */
            showMatch(static_cast<int>(searchMatches_.size()) - 1);
        }
    }

    updateStatusLine();
    requestRepaint();
}

void TerminalWidget::showMatch(int index) {
    if (!session_ || index < 0 || index >= static_cast<int>(searchMatches_.size())) return;

    searchIndex_ = index;
    const SelectionRange& match = searchMatches_[static_cast<size_t>(index)];
    /* Selecting the match is what makes it copyable, and highlights it as the
     * one in hand. */
    selection_.set(match);

    /* A third of the way down, so there is context both above and below it. */
    const int rows = layout_.isValid() ? layout_.rows : DefaultRows;
    session_->scrollViewToLine(match.start.line, rows / 3);

    updateStatusLine();
    requestRepaint();
}

void TerminalWidget::stepMatch(int delta) {
    if (searchMatches_.empty() || delta == 0) return;

    const int count = static_cast<int>(searchMatches_.size());
    int index = searchIndex_ < 0 ? (delta < 0 ? count - 1 : 0) : searchIndex_ + delta;
    /* Wrapping, so stepping off one end starts again at the other. */
    index = ((index % count) + count) % count;
    showMatch(index);
}

void TerminalWidget::findNext() {
    if (searchQuery_.empty()) {
        beginSearch();
        return;
    }
    /* A search that was closed, or invalidated by a resize, is run again rather
     * than stepping through a stale list. */
    if (searchMatches_.empty()) {
        refreshSearch();
        return;
    }
    stepMatch(1);
}

void TerminalWidget::findPrevious() {
    if (searchQuery_.empty()) {
        beginSearch();
        return;
    }
    if (searchMatches_.empty()) {
        refreshSearch();
        return;
    }
    stepMatch(-1);
}

void TerminalWidget::updateStatusLine() {
    if (!searchActive_) {
        statusLine_.clear();
        return;
    }

    std::u32string line = U"/";
    line += searchQuery_;

    /* The count, or why there is none. */
    std::string suffix;
    if (searchQuery_.empty()) {
        suffix = "type to search";
    } else if (searchMatches_.empty()) {
        suffix = "no match";
    } else {
        suffix = std::to_string(searchIndex_ + 1) + "/"
               + std::to_string(searchMatches_.size());
        /* A cap that was hit is stated rather than passed off as a total. */
        if (searchTruncated_) suffix += "+";
    }

    std::u32string right;
    for (const char c : suffix) right += static_cast<char32_t>(c);

    /*
     * The count sits at the right margin and the query at the left. When a long
     * query reaches the count, the query wins: what has been typed matters more
     * than how much it matched.
     */
    const int cols = layout_.isValid() ? layout_.cols : DefaultCols;
    const size_t width = static_cast<size_t>(std::max(0, cols));
    if (line.size() + right.size() + 1 <= width) {
        line.resize(width - right.size(), U' ');
        line += right;
    }

    statusLine_ = line;
}

bool TerminalWidget::handleSearchKey(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        endSearch();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        /* Return steps back through the buffer, Shift+Return forward again --
         * the direction a scrollback search goes. */
        stepMatch((event->modifiers() & Qt::ShiftModifier) ? 1 : -1);
        return true;
    case Qt::Key_Up:
        stepMatch(-1);
        return true;
    case Qt::Key_Down:
        stepMatch(1);
        return true;
    case Qt::Key_Backspace:
        if (!searchQuery_.empty()) {
            searchQuery_.pop_back();
            refreshSearch();
        }
        return true;
    case Qt::Key_U:
        /* Ctrl+U clears the line, as it does at a shell prompt. */
        if (event->modifiers() & Qt::ControlModifier) {
            searchQuery_.clear();
            refreshSearch();
            return true;
        }
        break;
    default:
        break;
    }

    /*
     * Anything printable extends the query; anything else is swallowed. The
     * search owns the keyboard while it is open -- Escape is the way out, and
     * the keybindings were given first refusal before this was called.
     */
    const std::u32string typed = event->text().toStdU32String();
    bool changed = false;
    for (const char32_t ch : typed) {
        if (ch >= 0x20 && ch != 0x7F) {
            searchQuery_ += ch;
            changed = true;
        }
    }
    if (changed) refreshSearch();
    return true;
}

void TerminalWidget::paste() {
    if (!session_ || !session_->isValid()) return;

    const QClipboard* clipboard = QApplication::clipboard();
    session_->sendPaste(clipboard->text());
}

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
    /* Release GL-owned objects while the context is still current. */
    makeCurrent();
    renderer_.reset();
    doneCurrent();
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

    if (QOpenGLContext* context = QOpenGLContext::currentContext()) {
        const QSurfaceFormat format = context->format();
        if (format.majorVersion() < 3
            || (format.majorVersion() == 3 && format.minorVersion() < 3)) {
            qCritical() << "TerminalWidget: OpenGL 3.3 core required, got"
                        << format.majorVersion() << "." << format.minorVersion();
            return;
        }
    }

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

    const int rows = layout_.isValid() ? layout_.rows : DefaultRows;
    const int cols = layout_.isValid() ? layout_.cols : DefaultCols;

    session_ = std::make_unique<TerminalSession>(rows, cols,
                                                Config::instance().palette(), this);
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

    session_->sendInput(bytes);
    event->accept();
}

void TerminalWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();

    /* Middle click pastes the primary selection on X11; on platforms without
     * one Qt returns the clipboard, which is close enough to be useful. */
    if (event->button() == Qt::MiddleButton) {
        paste();
    }
    event->accept();
}

void TerminalWidget::wheelEvent(QWheelEvent* event) {
    /* Scrollback is not implemented yet, so swallow the event rather than
     * letting it propagate into the tab bar. */
    event->accept();
}

void TerminalWidget::focusInEvent(QFocusEvent* event) {
    QOpenGLWidget::focusInEvent(event);
    restartBlink();
    update();
}

void TerminalWidget::focusOutEvent(QFocusEvent* event) {
    QOpenGLWidget::focusOutEvent(event);
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

/*
 * TerminalCanvas - shared GPU surface implementation
 */

#include "terminal_canvas.h"
#include "terminal_widget.h"
#include "../config/config.h"
#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QOpenGLContext>
#include <QMouseEvent>
#include <QTabWidget>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

/*
 * Every canvas, by the top-level widget it serves. A pane needs to find its
 * canvas and has no other route to it: the canvas is a QWindow, so it is not
 * among its window's child *widgets* and findChild() would never see it.
 */
QHash<const QWidget*, TerminalCanvas*>& canvasRegistry() {
    static QHash<const QWidget*, TerminalCanvas*> registry;
    return registry;
}

/*
 * Whether this platform can give us an OpenGL context at all.
 *
 * The offscreen plugin the non-GL test suites run under cannot, and a
 * QOpenGLWindow on such a platform is not merely blank -- it fails inside Qt's
 * own paint machinery. A pane whose window has no canvas simply does not draw,
 * which is exactly the graceful degradation those suites want, so the answer
 * here decides whether a canvas is created at all.
 *
 * Probed once. The context is destroyed immediately and the real canvas builds
 * its own.
 */
bool platformSupportsOpenGL() {
    static const bool supported = [] {
        QOpenGLContext probe;
        probe.setFormat(QSurfaceFormat::defaultFormat());
        return probe.create();
    }();
    return supported;
}

} // namespace

TerminalCanvas::TerminalCanvas() = default;

TerminalCanvas::~TerminalCanvas() {
    /* The renderer owns GL objects, which can only be deleted with the context
     * current. */
    if (renderer_ && glReady_) {
        makeCurrent();
        renderer_.reset();
        doneCurrent();
    }
    /* If the context never came up there are no GL objects to release, and
     * nothing to make current in order to do it. */
    renderer_.reset();
    for (auto it = canvasRegistry().begin(); it != canvasRegistry().end();) {
        it = (it.value() == this) ? canvasRegistry().erase(it) : ++it;
    }
}

QWidget* TerminalCanvas::createContainer(QWidget* parent) {
    container_ = QWidget::createWindowContainer(this, parent);
    /*
     * The canvas must never take focus. Keyboard and input-method events have
     * to keep reaching the focused pane widget underneath, which is what makes
     * this a drop-in replacement for panes that used to own their own surface.
     */
    container_->setFocusPolicy(Qt::NoFocus);
    if (QWidget* top = parent ? parent->window() : nullptr) {
        canvasRegistry().insert(top, this);
    }
    return container_;
}

bool TerminalCanvas::isSupported() { return platformSupportsOpenGL(); }

TerminalCanvas* TerminalCanvas::forWidget(const QWidget* widget) {
    if (!widget) return nullptr;
    return canvasRegistry().value(widget->window(), nullptr);
}

TerminalCanvas* TerminalCanvas::ensureFor(QWidget* widget) {
    if (!widget) return nullptr;
    if (TerminalCanvas* existing = forWidget(widget)) return existing;
    if (!platformSupportsOpenGL()) return nullptr;

    QWidget* top = widget->window();
    if (!top) return nullptr;

    auto* canvas = new TerminalCanvas();
    canvas->setPageProvider(top);
    canvas->createContainer(top);
    canvas->syncGeometry();
    canvas->container()->show();
    return canvas;
}

QImage TerminalCanvas::grabPane(const QWidget* pane) {
    if (!pane || !glReady_ || !handle()) return QImage();

    /*
     * Draw a frame and read it straight back, rather than going through
     * QOpenGLWindow::grabFramebuffer().
     *
     * That function is only reliable when the window keeps a framebuffer object
     * of its own to render into, and keeping one would put back the very
     * full-window buffer this class exists to avoid. Rendering into the back
     * buffer and reading it before it is swapped costs nothing and is exact --
     * after a swap the buffer's contents are undefined, which is why reading it
     * at any other moment came back blank.
     */
    makeCurrent();

    const double scale = scaleFactor();
    const int surfaceWidth = std::max(1, static_cast<int>(std::lround(width() * scale)));
    const int surfaceHeight = std::max(1, static_cast<int>(std::lround(height() * scale)));

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    paintGL();
    glFinish();

    const QRect logical = paneRect(pane);
    const int x = static_cast<int>(std::lround(logical.x() * scale));
    const int top = static_cast<int>(std::lround(logical.y() * scale));
    const int w = static_cast<int>(std::lround(logical.width() * scale));
    const int h = static_cast<int>(std::lround(logical.height() * scale));
    const QRect device = QRect(x, top, w, h).intersected(QRect(0, 0, surfaceWidth, surfaceHeight));
    if (device.isEmpty()) return QImage();

    QImage image(device.width(), device.height(), QImage::Format_RGBA8888);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    /*
     * GL counts rows from the bottom, so read from the flipped y and put the
     * rows back the right way up on the way in. Done by hand rather than with
     * QImage's own flip, which is spelled mirrored() before Qt 6.9 and
     * flipped() after -- and this way there is no second copy of the image.
     */
    const int bottom = surfaceHeight - (device.y() + device.height());
    std::vector<uint8_t> row(static_cast<size_t>(device.width()) * 4);
    for (int y = 0; y < device.height(); ++y) {
        glReadPixels(device.x(), bottom + y, device.width(), 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, row.data());
        std::memcpy(image.scanLine(device.height() - 1 - y), row.data(), row.size());
    }

    return image;
}

void TerminalCanvas::addPane(TerminalWidget* pane) {
    if (!pane || panes_.contains(pane)) return;
    panes_.append(pane);
}

void TerminalCanvas::removePane(TerminalWidget* pane) {
    panes_.removeAll(pane);
}

void TerminalCanvas::syncGeometry() {
    if (!container_ || !reference_) return;

    /*
     * Cover the page area only, so the tab bar -- an ordinary widget inside the
     * same QTabWidget -- is left visible above it. Every page has the same
     * geometry, so whichever one is current answers for all of them. Anything
     * that is not a QTabWidget is covered whole.
     */
    QWidget* page = reference_;
    if (auto* tabs = qobject_cast<QTabWidget*>(reference_.data())) {
        page = tabs->currentWidget();
    }
    if (!page) return;

    QWidget* host = container_->parentWidget();
    if (!host) return;

    const QRect area(host->mapFromGlobal(page->mapToGlobal(QPoint(0, 0))), page->size());
    if (container_->geometry() != area) {
        container_->setGeometry(area);
    }
    /* Native child windows sit above their siblings, but a later-created
     * sibling can still land on top; make sure the canvas is not buried. */
    container_->raise();
}

double TerminalCanvas::scaleFactor() const {
    const double ratio = devicePixelRatio();
    return ratio > 0.0 ? ratio : 1.0;
}

double TerminalCanvas::logicalDpi() const {
    const QScreen* s = screen();
    const double dpi = s ? s->logicalDotsPerInchY() : 0.0;
    return dpi > 0.0 ? dpi : 96.0;
}

void TerminalCanvas::initializeGL() {
    initializeOpenGLFunctions();

    const QSurfaceFormat format = context() ? context()->format() : QSurfaceFormat();
    if (format.majorVersion() < 3
        || (format.majorVersion() == 3 && format.minorVersion() < 3)) {
        qCritical() << "TerminalCanvas: OpenGL 3.3 core required, got"
                    << format.majorVersion() << "." << format.minorVersion();
        return;
    }

    renderer_ = std::make_unique<GLRenderer>();
    if (!renderer_->initialize()) {
        qCritical() << "TerminalCanvas: renderer initialization failed";
        renderer_.reset();
        return;
    }
    glReady_ = true;
    fontValid_ = false;
}

bool TerminalCanvas::ensureReady() {
    if (!platformSupportsOpenGL()) return false;

    /* makeCurrent() creates the context and runs initializeGL() the first time,
     * but only once the window itself exists. */
    if (!handle()) create();
    if (!handle()) return false;

    makeCurrent();
    if (!glReady_) return false;

    refreshFont();
    return renderer_ && renderer_->hasFont();
}

bool TerminalCanvas::refreshFont() {
    if (!renderer_ || !renderer_->isInitialized()) return false;

    const Config& config = Config::instance();
    const double scale = scaleFactor();
    const double dpi = logicalDpi();

    const bool stale = !fontValid_
                    || std::abs(scale - lastScaleFactor_) > 0.001
                    || std::abs(dpi - lastLogicalDpi_) > 0.001
                    || config.fontSize() != lastFontSize_;
    if (!stale) return false;

    /*
     * Points to physical pixels. The device pixel ratio carries the HiDPI
     * factor on top of the logical DPI; multiplying the two is exactly how Qt
     * sizes its own text.
     */
    const double pixelSize = config.fontSize() * (dpi / 72.0) * scale;
    if (!renderer_->setFont(config.fontFamilies(), config.fontFallbacks(), pixelSize,
                            config.emojiScale())) {
        qCritical() << "TerminalCanvas: no usable font, nothing can be drawn";
        return false;
    }

    lastScaleFactor_ = scale;
    lastLogicalDpi_ = dpi;
    lastFontSize_ = config.fontSize();
    fontValid_ = true;
    return true;
}

QRect TerminalCanvas::paneRect(const QWidget* pane) const {
    /* Through global coordinates, so this does not depend on where the
     * container sits in the widget tree. */
    const QPoint topLeft = mapFromGlobal(pane->mapToGlobal(QPoint(0, 0)));
    return QRect(topLeft, pane->size());
}

void TerminalCanvas::paintGL() {
    /* No context, no drawing -- and no GL calls either, since without a context
     * the function pointers behind them were never resolved. */
    if (!glReady_) return;

    const Config& config = Config::instance();

    const double scale = scaleFactor();
    const int surfaceWidth = std::max(1, static_cast<int>(std::lround(width() * scale)));
    const int surfaceHeight = std::max(1, static_cast<int>(std::lround(height() * scale)));

    /*
     * Clear to the split separator colour and let the panes paint over it. The
     * only pixels left showing are the gaps between panes, which is exactly
     * where the divider goes -- so the dividers come for free, and they cannot
     * drift out of step with the pane rectangles the way a separately drawn
     * line could.
     *
     * The viewport is set from the surface size rather than left as it is:
     * every pane below narrows it to its own rectangle, so at the end of a
     * frame it describes whichever pane was drawn last, and inheriting that
     * would clip every frame after the first into a corner of the window.
     */
    const QColor separator = config.chromeColors().resolve(config.palette()).splitSeparator;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, surfaceWidth, surfaceHeight);
    glClearColor(static_cast<GLfloat>(separator.redF()),
                 static_cast<GLfloat>(separator.greenF()),
                 static_cast<GLfloat>(separator.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!renderer_ || !renderer_->isInitialized()) return;

    /* One font for the whole window; re-lay-out every pane when it changes. */
    if (refreshFont()) {
        for (const auto& pane : panes_) {
            if (pane) pane->updateGeometryForFont();
        }
    }
    if (!renderer_->hasFont()) return;

    bool repaintNeeded = false;

    for (const auto& pane : panes_) {
        if (!pane || !pane->isVisible()) continue;

        const QRect rect = paneRect(pane);
        if (rect.isEmpty()) continue;

        const int left = static_cast<int>(std::lround(rect.x() * scale));
        const int top = static_cast<int>(std::lround(rect.y() * scale));
        const int paneWidth = std::max(1, static_cast<int>(std::lround(rect.width() * scale)));
        const int paneHeight = std::max(1, static_cast<int>(std::lround(rect.height() * scale)));
        if (left >= surfaceWidth || top >= surfaceHeight) continue;

        /* GL counts the viewport from the bottom of the surface. */
        const int bottom = surfaceHeight - (top + paneHeight);

        pane->renderInto(*renderer_, left, bottom, paneWidth, paneHeight);
        repaintNeeded = repaintNeeded || renderer_->needsRepaint();
    }

    if (repaintNeeded) {
        /* The atlas grew part-way through, so something was dropped; the atlas
         * is now big enough, so one more pass finishes the job. */
        update();
    }
}

bool TerminalCanvas::forwardMouse(QEvent* event) {
    auto* mouse = dynamic_cast<QMouseEvent*>(event);
    auto* wheel = dynamic_cast<QWheelEvent*>(event);
    if (!mouse && !wheel) return false;
    if (!reference_) return false;

    const QPointF globalPos = mouse ? mouse->globalPosition() : wheel->globalPosition();

    /*
     * A press picks the target and keeps it until the release. Without that, a
     * splitter divider being dragged would stop receiving events the moment the
     * pointer moved off the two pixels it occupies.
     */
    QWidget* target = nullptr;
    if (mouse && mouse->type() == QEvent::MouseButtonPress) {
        mouseGrabber_ = nullptr;
    }
    if (mouseGrabber_) {
        target = mouseGrabber_;
    } else {
        const QPoint local = reference_->mapFromGlobal(globalPos.toPoint());
        target = reference_->childAt(local);
        /* childAt() returns the deepest child, which is what we want: the pane
         * widget, or the splitter handle between two of them. */
    }
    if (!target) return false;

    if (mouse) {
        if (mouse->type() == QEvent::MouseButtonPress) mouseGrabber_ = target;
        if (mouse->type() == QEvent::MouseButtonRelease) mouseGrabber_ = nullptr;

        QMouseEvent copy(mouse->type(),
                         target->mapFromGlobal(globalPos.toPoint()),
                         globalPos,
                         mouse->button(), mouse->buttons(), mouse->modifiers());
        QCoreApplication::sendEvent(target, &copy);
        return copy.isAccepted();
    }

    QWheelEvent copy(QPointF(target->mapFromGlobal(globalPos.toPoint())),
                     globalPos, wheel->pixelDelta(), wheel->angleDelta(),
                     wheel->buttons(), wheel->modifiers(), wheel->phase(),
                     wheel->inverted());
    QCoreApplication::sendEvent(target, &copy);
    return copy.isAccepted();
}

bool TerminalCanvas::event(QEvent* event) {
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
        if (forwardMouse(event)) return true;
        break;
    default:
        break;
    }
    return QOpenGLWindow::event(event);
}

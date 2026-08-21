/*
 * TerminalCanvas - one GPU surface for every pane in a window
 *
 * Panes used to be QOpenGLWidgets, one GL context and one glyph atlas each.
 * That is expensive twice over. A QOpenGLWidget does not draw to the window: it
 * draws to its own framebuffer object, which Qt then composites into the
 * window's backing store, and on macOS that composite goes through the
 * GL-on-Metal shim as a full-window texture upload every single flush. And
 * every extra pane repeated the whole arrangement -- another context, another
 * 4 MiB atlas, another set of buffers.
 *
 * So there is one surface per *window* instead, and it is a native one:
 * QOpenGLWindow renders straight into a layer the window server composites,
 * with no Qt backing-store round trip at all. Panes keep their widgets -- which
 * is what keeps QSplitter doing the layout and Qt doing focus, keyboard and
 * input methods -- but those widgets no longer paint. This canvas is stacked
 * over them and draws all of them, each into its own viewport, from one atlas
 * and one set of vertex buffers.
 *
 * Two consequences are worth stating plainly:
 *
 *   - A pane costs no GPU memory at all. Eight panes measure the same as one,
 *     where the old arrangement grew by roughly 9 MiB a pane.
 *
 *   - Panes in tabs that are not showing are not drawn, because they are not
 *     visible, so a tab costs nothing until it is looked at.
 *
 * Sitting on top of the widgets means this window receives the mouse events
 * they would otherwise have got, so it hands them back -- see forwardMouse().
 * Keyboard and input-method events are untouched: this window never takes
 * focus, so they continue to go straight to the focused pane widget.
 */

#ifndef UI_TERMINAL_CANVAS_H
#define UI_TERMINAL_CANVAS_H

#include "../render/gl_renderer.h"
#include <QList>
#include <QOpenGLFunctions>
#include <QImage>
#include <QOpenGLWindow>
#include <QPointer>
#include <memory>

class TerminalWidget;
class QWidget;

class TerminalCanvas : public QOpenGLWindow, protected QOpenGLFunctions {
    Q_OBJECT

public:
    TerminalCanvas();
    ~TerminalCanvas() override;

    /*
     * Wrap this window in a widget that can go into a layout. Called once, by
     * the window that owns the canvas; the container is parented to `parent`.
     */
    QWidget* createContainer(QWidget* parent);
    QWidget* container() const { return container_; }

    /*
     * The area the canvas covers. Given a QTabWidget it tracks the current
     * page, so the tab bar stays an ordinary widget above it; given anything
     * else it covers that widget whole.
     */
    void setPageProvider(QWidget* reference) { reference_ = reference; }
    /* Reposition the container over the current page. Cheap; call freely. */
    void syncGeometry();

    /* Panes to draw. A pane registers on construction and leaves on destruction. */
    void addPane(TerminalWidget* pane);
    void removePane(TerminalWidget* pane);

    /*
     * The renderer every pane draws through. Null until the GL context exists,
     * which is why panes ask for it rather than caching it.
     */
    GLRenderer* renderer() { return renderer_ && renderer_->isInitialized() ? renderer_.get() : nullptr; }

    /*
     * Bring the context and the font up before anything asks to be drawn.
     *
     * A pane needs the font metrics the moment it is shown, because the grid
     * size it computes from them is what the shell is told at start-up -- and a
     * shell should be started as soon as the pane exists, not deferred to the
     * first frame. Returns false when there is no usable context.
     */
    bool ensureReady();

    /* Re-rasterize the font if the display or the configured size changed.
     * Returns true when it actually changed, so callers can re-lay-out. */
    bool refreshFont();
    /* Force a re-rasterization on the next frame (the font setting changed). */
    void invalidateFont() { fontValid_ = false; }

    /* False when the platform cannot provide an OpenGL context, in which case
     * no canvas should be built and panes simply do not draw. */
    static bool isSupported();

    /* The canvas belonging to `widget`'s window, or nullptr. */
    static TerminalCanvas* forWidget(const QWidget* widget);
    /*
     * The canvas for `widget`'s window, creating one over the whole window if
     * there is none. MainWindow builds its own so it can place it over the tab
     * page precisely; this covers a pane tree used on its own, which is how the
     * tests drive it.
     */
    static TerminalCanvas* ensureFor(QWidget* widget);

    /*
     * The pixels belonging to `pane`, cropped out of the shared surface. Panes
     * no longer have a framebuffer of their own, and this is what stands in for
     * the one they used to have.
     */
    QImage grabPane(const QWidget* pane);

protected:
    void initializeGL() override;
    void paintGL() override;
    bool event(QEvent* event) override;

private:
    /* Where `pane` sits on this surface, in the canvas's logical pixels. */
    QRect paneRect(const QWidget* pane) const;
    /* Hand a mouse event back to whichever widget is underneath it. */
    bool forwardMouse(QEvent* event);
    double scaleFactor() const;
    double logicalDpi() const;

    std::unique_ptr<GLRenderer> renderer_;
    QList<QPointer<TerminalWidget>> panes_;
    QPointer<QWidget> reference_;
    QWidget* container_ = nullptr;

    /*
     * What the rasterized font matches. One canvas serves one window, and a
     * window is on one screen, so unlike the old per-pane arrangement there is
     * a single answer to this.
     */
    /*
     * False until initializeGL() has run successfully. Without a GL context --
     * the offscreen platform plugin cannot create one -- QOpenGLFunctions is
     * not initialized and every gl* call below would be a null dispatch.
     */
    bool glReady_ = false;
    bool fontValid_ = false;
    double lastScaleFactor_ = 0.0;
    double lastLogicalDpi_ = 0.0;
    int lastFontSize_ = 0;

    /*
     * Set while a drag is in progress, so the moves and the release go to
     * whatever the press landed on -- a splitter handle being dragged has to
     * keep receiving events once the pointer has left it.
     */
    QPointer<QWidget> mouseGrabber_;
};

#endif /* UI_TERMINAL_CANVAS_H */

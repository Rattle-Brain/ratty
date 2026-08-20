/*
 * GLRenderer - batched 2D drawing for the terminal
 *
 * Two things about this class matter more than anything else:
 *
 * 1. It works exclusively in *physical* pixels. beginFrame() takes the
 *    framebuffer size in device pixels and builds a matching orthographic
 *    projection. The previous version projected the widget's logical size onto
 *    a framebuffer that Qt had already sized in device pixels, so on any
 *    Retina/HiDPI display the whole scene -- glyphs included -- was scaled up
 *    2x by the GPU.
 *
 * 2. Draw order is structural, not incidental. Callers submit into three
 *    explicit layers and endFrame() flushes them bottom-up. Before, rectangles
 *    were flushed *after* text, so any cell with a non-default background
 *    painted over its own character.
 */

#ifndef RENDER_GL_RENDERER_H
#define RENDER_GL_RENDERER_H

#include "font_manager.h"
#include "glyph_atlas.h"
#include <QColor>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

class GLRenderer {
public:
    GLRenderer();
    ~GLRenderer();

    GLRenderer(const GLRenderer&) = delete;
    GLRenderer& operator=(const GLRenderer&) = delete;

    /* Call with the target GL context current. */
    bool initialize();
    bool isInitialized() const { return initialized_; }

    /*
     * Load the first installed family from `families` at `pixelSize` physical
     * pixels per em; an empty list selects the platform default. Returns false
     * if no font could be loaded, in which case the previous font (if any) is
     * gone.
     */
    bool setFont(const QStringList& families, const QStringList& fallbacks,
                 double pixelSize);
    const FontMetrics& fontMetrics() const { return fonts_.metrics(); }
    bool hasFont() const { return fonts_.isValid(); }

    /* framebufferWidth/Height are in physical pixels. */
    void beginFrame(int framebufferWidth, int framebufferHeight, const QColor& clearColor);
    void endFrame();

    /*
     * True when the glyph atlas was rebuilt part-way through the frame that just
     * ended. Anything batched before that point had to be discarded, so the
     * frame is incomplete and the caller should schedule another paint.
     */
    bool needsRepaint() const { return atlasRebuiltMidFrame_; }

    /* Layer 1: cell backgrounds. */
    void fillBackground(int x, int y, int width, int height, const QColor& color);

    /*
     * Layer 2: glyphs. `penX` is the left edge of the advance, `baselineY` the
     * baseline, both in physical pixels. `presentation` selects between the text
     * and emoji form of a dual-form code point.
     */
    void drawGlyph(char32_t codepoint, FontStyle style, GlyphPresentation presentation,
                   int penX, int baselineY, const QColor& color);

    /* Layer 3: cursor, selection, focus indicators - drawn over the text. */
    void fillOverlay(int x, int y, int width, int height, const QColor& color);
    void strokeOverlay(int x, int y, int width, int height, int thickness,
                       const QColor& color);

private:
    struct TextVertex {
        float x, y;
        float u, v;
        float r, g, b, a;
        /* 0 = tint the coverage mask with (r,g,b); 1 = a colour glyph, used
         * verbatim. Per-vertex rather than a uniform so colour and monochrome
         * glyphs stay in the same batch and the same draw call. */
        float isColor;
    };
    struct RectVertex {
        float x, y;
        float r, g, b, a;
    };

    bool compileShader(QOpenGLShaderProgram& program, const QString& vertexPath,
                       const QString& fragmentPath, const char* label);
    void appendRect(std::vector<RectVertex>& target, int x, int y, int width, int height,
                    const QColor& color);
    void flushRects(std::vector<RectVertex>& vertices);
    void flushText();
    void ensureCapacity(QOpenGLBuffer& buffer, int& capacityBytes, int neededBytes);

    QOpenGLFunctions* gl_ = nullptr;
    bool initialized_ = false;

    FontManager fonts_;
    std::unique_ptr<GlyphAtlas> atlas_;
    /* The preference list the current faces were loaded for, so a pure size
     * change can skip re-resolving fonts. */
    QStringList loadedRequest_;
    QStringList loadedFallbacks_;

    std::unique_ptr<QOpenGLShaderProgram> textShader_;
    std::unique_ptr<QOpenGLShaderProgram> rectShader_;

    QOpenGLBuffer textVBO_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer rectVBO_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject textVAO_;
    QOpenGLVertexArrayObject rectVAO_;
    int textCapacityBytes_ = 0;
    int rectCapacityBytes_ = 0;

    uint64_t atlasGeneration_ = 0;
    bool atlasRebuiltMidFrame_ = false;

    QMatrix4x4 projection_;

    /* Layers, flushed in this order by endFrame(). */
    std::vector<RectVertex> backgroundVertices_;
    std::vector<TextVertex> textVertices_;
    std::vector<RectVertex> overlayVertices_;

    /* Flush the text layer early if it grows past this, to bound VBO size. */
    static constexpr size_t TextFlushThreshold = 96 * 1024;
};

#endif /* RENDER_GL_RENDERER_H */

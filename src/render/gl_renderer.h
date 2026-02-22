/*
 * GLRenderer - OpenGL rendering engine for terminal
 *
 * Handles GPU-accelerated text rendering using:
 * - FontManager for glyph rasterization
 * - GlyphAtlas for texture caching
 * - GLSL shaders for rendering
 *
 * Simplified version without HarfBuzz text shaping (glyph-by-glyph rendering)
 */

#ifndef RENDER_GL_RENDERER_H
#define RENDER_GL_RENDERER_H

// Include native OpenGL on macOS for direct API access
#ifdef Q_OS_MACOS
#include <OpenGL/gl3.h>
#endif

#include "font_manager.h"
#include "glyph_atlas.h"
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QColor>
#include <QString>

/* Text vertex structure */
struct TextVertex {
    float x, y;          // Position
    float u, v;          // Texture coordinates
    float r, g, b, a;    // Color
};

/* Rectangle vertex structure */
struct RectVertex {
    float x, y;          // Position
    float r, g, b, a;    // Color
};

class GLRenderer {
public:
    GLRenderer();
    ~GLRenderer();

    // Delete copy, allow move
    GLRenderer(const GLRenderer&) = delete;
    GLRenderer& operator=(const GLRenderer&) = delete;

    // Initialize OpenGL resources (call after GL context is current)
    bool initialize();

    // Font management
    bool loadFont(const QString& path, FontStyle style, int sizePt, int dpi = 96);
    bool loadDefaultFont(int sizePt, int dpi = 96);
    void setFontSize(int sizePt, int dpi = -1);
    FontMetrics getFontMetrics() const;

    // Frame rendering
    void beginFrame(int width, int height);
    void endFrame();

    // Rendering primitives
    void drawText(const QString& text, float x, float y, const QColor& color, FontStyle style = FontStyleRegular);
    void drawRect(float x, float y, float width, float height, const QColor& color);
    void drawRectOutline(float x, float y, float width, float height, const QColor& color, float lineWidth = 1.0f);

    // Clear screen
    void clear(const QColor& color);

    // Check if initialized
    bool isInitialized() const { return initialized_; }

private:
    struct Batch {
        QVector<TextVertex> textVertices;
        QVector<RectVertex> rectVertices;
    };

    void flushTextBatch();
    void flushRectBatch();

    QOpenGLFunctions* gl_;
    bool initialized_;

    // Subsystems
    FontManager fontManager_;
    GlyphAtlas* glyphAtlas_;

    // Shaders
    QOpenGLShaderProgram* textShader_;
    QOpenGLShaderProgram* rectShader_;
    GLint textTextureUniformLoc_;  // Uniform location for u_texture sampler

    // Buffers
    QOpenGLBuffer textVBO_;
    QOpenGLBuffer rectVBO_;
    QOpenGLVertexArrayObject textVAO_;
    QOpenGLVertexArrayObject rectVAO_;

    // Projection matrix
    QMatrix4x4 projection_;

    // Viewport
    int viewportWidth_;
    int viewportHeight_;

    // Batching
    Batch batch_;

    static constexpr int MAX_TEXT_VERTICES = 65536;
    static constexpr int MAX_RECT_VERTICES = 16384;
};

#endif /* RENDER_GL_RENDERER_H */

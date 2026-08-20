/*
 * GLRenderer - batched 2D drawing implementation
 */

#include "gl_renderer.h"
#include <QDebug>
#include <QFile>
#include <QOpenGLContext>
#include <algorithm>
#include <cstddef>

namespace {

constexpr int kAtlasInitialSize = 1024;

/* Vertex attribute locations, matching the layout() qualifiers in the shaders. */
enum : GLuint {
    AttrPosition = 0,
    AttrTexCoord = 1,
    AttrTextColor = 2,
    AttrTextIsColor = 3,
    AttrRectColor = 1,
};

QByteArray readShaderSource(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

} // namespace

GLRenderer::GLRenderer() = default;
GLRenderer::~GLRenderer() = default;

bool GLRenderer::initialize() {
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        qCritical() << "GLRenderer: no current OpenGL context";
        return false;
    }

    gl_ = context->functions();
    if (!gl_) {
        qCritical() << "GLRenderer: could not obtain OpenGL functions";
        return false;
    }

    atlas_ = std::make_unique<GlyphAtlas>(gl_, kAtlasInitialSize);
    if (!atlas_->isValid()) {
        qCritical() << "GLRenderer: glyph atlas creation failed";
        return false;
    }

    textShader_ = std::make_unique<QOpenGLShaderProgram>();
    if (!compileShader(*textShader_, QStringLiteral(":/shaders/shaders/text.vert"),
                       QStringLiteral(":/shaders/shaders/text.frag"), "text")) {
        return false;
    }
    rectShader_ = std::make_unique<QOpenGLShaderProgram>();
    if (!compileShader(*rectShader_, QStringLiteral(":/shaders/shaders/rect.vert"),
                       QStringLiteral(":/shaders/shaders/rect.frag"), "rect")) {
        return false;
    }

    /* Bind the atlas sampler to texture unit 0 once; nothing else uses a
     * texture, so the binding never changes. */
    textShader_->bind();
    textShader_->setUniformValue("u_texture", 0);
    textShader_->release();

    textVAO_.create();
    textVAO_.bind();
    textVBO_.create();
    textVBO_.bind();
    textVBO_.setUsagePattern(QOpenGLBuffer::StreamDraw);
    gl_->glEnableVertexAttribArray(AttrPosition);
    gl_->glVertexAttribPointer(AttrPosition, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                               reinterpret_cast<void*>(offsetof(TextVertex, x)));
    gl_->glEnableVertexAttribArray(AttrTexCoord);
    gl_->glVertexAttribPointer(AttrTexCoord, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                               reinterpret_cast<void*>(offsetof(TextVertex, u)));
    gl_->glEnableVertexAttribArray(AttrTextColor);
    gl_->glVertexAttribPointer(AttrTextColor, 4, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                               reinterpret_cast<void*>(offsetof(TextVertex, r)));
    gl_->glEnableVertexAttribArray(AttrTextIsColor);
    gl_->glVertexAttribPointer(AttrTextIsColor, 1, GL_FLOAT, GL_FALSE, sizeof(TextVertex),
                               reinterpret_cast<void*>(offsetof(TextVertex, isColor)));
    textVAO_.release();
    textVBO_.release();

    rectVAO_.create();
    rectVAO_.bind();
    rectVBO_.create();
    rectVBO_.bind();
    rectVBO_.setUsagePattern(QOpenGLBuffer::StreamDraw);
    gl_->glEnableVertexAttribArray(AttrPosition);
    gl_->glVertexAttribPointer(AttrPosition, 2, GL_FLOAT, GL_FALSE, sizeof(RectVertex),
                               reinterpret_cast<void*>(offsetof(RectVertex, x)));
    gl_->glEnableVertexAttribArray(AttrRectColor);
    gl_->glVertexAttribPointer(AttrRectColor, 4, GL_FLOAT, GL_FALSE, sizeof(RectVertex),
                               reinterpret_cast<void*>(offsetof(RectVertex, r)));
    rectVAO_.release();
    rectVBO_.release();

    initialized_ = true;
    return true;
}

bool GLRenderer::compileShader(QOpenGLShaderProgram& program, const QString& vertexPath,
                               const QString& fragmentPath, const char* label) {
    const QByteArray vertexSource = readShaderSource(vertexPath);
    const QByteArray fragmentSource = readShaderSource(fragmentPath);

    if (vertexSource.isEmpty() || fragmentSource.isEmpty()) {
        qCritical() << "GLRenderer:" << label << "shader sources missing from resources";
        return false;
    }

    if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource)) {
        qCritical() << "GLRenderer:" << label << "vertex shader:" << program.log();
        return false;
    }
    if (!program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource)) {
        qCritical() << "GLRenderer:" << label << "fragment shader:" << program.log();
        return false;
    }
    if (!program.link()) {
        qCritical() << "GLRenderer:" << label << "shader link:" << program.log();
        return false;
    }
    return true;
}

bool GLRenderer::setFont(const QStringList& families, const QStringList& fallbacks,
                         double pixelSize) {
    if (pixelSize <= 0.0) return false;

    auto toStdVector = [](const QStringList& list) {
        std::vector<std::string> result;
        result.reserve(static_cast<size_t>(list.size()));
        for (const QString& item : list) result.push_back(item.toStdString());
        return result;
    };

    /* A pure size change keeps the faces and only re-scales them. */
    const bool sameRequest = (families == loadedRequest_)
                          && (fallbacks == loadedFallbacks_)
                          && fonts_.isValid();

    bool ok = false;
    if (sameRequest) {
        ok = fonts_.setPixelSize(pixelSize);
    } else {
        fonts_.setFallbackFamilies(toStdVector(fallbacks));
        ok = fonts_.loadFamily(toStdVector(families), pixelSize);
        if (ok) {
            loadedRequest_ = families;
            loadedFallbacks_ = fallbacks;
        }
    }
    if (!ok) return false;

    /* Cached glyphs were rasterized at the old size. */
    if (atlas_) atlas_->clear();
    atlasGeneration_ = atlas_ ? atlas_->generation() : 0;
    return true;
}

void GLRenderer::beginFrame(int framebufferWidth, int framebufferHeight,
                            const QColor& clearColor) {
    if (!initialized_) return;

    backgroundVertices_.clear();
    textVertices_.clear();
    overlayVertices_.clear();

    atlasRebuiltMidFrame_ = false;
    atlasGeneration_ = atlas_ ? atlas_->generation() : 0;

    /*
     * Map pixel coordinates directly onto the framebuffer, y growing downwards.
     * With integer vertex positions a quad edge lands exactly on a pixel
     * boundary and fragment centres land on texel centres, which is what lets
     * GL_NEAREST reproduce the rasterized glyph verbatim.
     */
    projection_.setToIdentity();
    projection_.ortho(0.0f, static_cast<float>(framebufferWidth),
                      static_cast<float>(framebufferHeight), 0.0f,
                      -1.0f, 1.0f);

    gl_->glDisable(GL_DEPTH_TEST);
    gl_->glEnable(GL_BLEND);
    gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl_->glClearColor(static_cast<GLfloat>(clearColor.redF()),
                      static_cast<GLfloat>(clearColor.greenF()),
                      static_cast<GLfloat>(clearColor.blueF()),
                      static_cast<GLfloat>(clearColor.alphaF()));
    gl_->glClear(GL_COLOR_BUFFER_BIT);
}

void GLRenderer::endFrame() {
    if (!initialized_) return;

    /* Bottom to top: backgrounds, then glyphs, then cursor and friends. */
    flushRects(backgroundVertices_);
    flushText();
    flushRects(overlayVertices_);
}

void GLRenderer::appendRect(std::vector<RectVertex>& target, int x, int y,
                            int width, int height, const QColor& color) {
    if (width <= 0 || height <= 0 || color.alpha() == 0) return;

    const float x0 = static_cast<float>(x);
    const float y0 = static_cast<float>(y);
    const float x1 = static_cast<float>(x + width);
    const float y1 = static_cast<float>(y + height);
    const float r = static_cast<float>(color.redF());
    const float g = static_cast<float>(color.greenF());
    const float b = static_cast<float>(color.blueF());
    const float a = static_cast<float>(color.alphaF());

    target.push_back({x0, y0, r, g, b, a});
    target.push_back({x1, y0, r, g, b, a});
    target.push_back({x1, y1, r, g, b, a});
    target.push_back({x0, y0, r, g, b, a});
    target.push_back({x1, y1, r, g, b, a});
    target.push_back({x0, y1, r, g, b, a});
}

void GLRenderer::fillBackground(int x, int y, int width, int height, const QColor& color) {
    if (!initialized_) return;
    appendRect(backgroundVertices_, x, y, width, height, color);
}

void GLRenderer::fillOverlay(int x, int y, int width, int height, const QColor& color) {
    if (!initialized_) return;
    appendRect(overlayVertices_, x, y, width, height, color);
}

void GLRenderer::strokeOverlay(int x, int y, int width, int height, int thickness,
                               const QColor& color) {
    if (!initialized_ || thickness <= 0) return;

    const int t = std::min(thickness, std::min(width, height));
    appendRect(overlayVertices_, x, y, width, t, color);                    // top
    appendRect(overlayVertices_, x, y + height - t, width, t, color);       // bottom
    appendRect(overlayVertices_, x, y + t, t, height - 2 * t, color);       // left
    appendRect(overlayVertices_, x + width - t, y + t, t, height - 2 * t, color); // right
}

void GLRenderer::drawGlyph(char32_t codepoint, FontStyle style,
                           GlyphPresentation presentation, int penX, int baselineY,
                           const QColor& color) {
    if (!initialized_ || !atlas_ || !fonts_.isValid() || color.alpha() == 0) return;

    const CachedGlyph* glyph = atlas_->glyph(codepoint, style, presentation, fonts_);

    if (atlas_->generation() != atlasGeneration_) {
        /*
         * Caching this glyph forced the atlas to be rebuilt, so every UV already
         * queued points into the old layout. Throw the batch away rather than
         * draw garbage; needsRepaint() asks the caller for a fresh frame.
         */
        textVertices_.clear();
        atlasGeneration_ = atlas_->generation();
        atlasRebuiltMidFrame_ = true;
    }

    if (!glyph || glyph->region.width <= 0 || glyph->region.height <= 0) {
        return;  // unmapped or blank (space) - nothing to rasterize
    }

    /* Integer placement: the atlas holds pixel-aligned coverage, so any
     * fractional offset here would resample and soften the glyph. */
    const float x0 = static_cast<float>(penX + glyph->bearingX);
    const float y0 = static_cast<float>(baselineY - glyph->bearingY);
    const float x1 = x0 + static_cast<float>(glyph->region.width);
    const float y1 = y0 + static_cast<float>(glyph->region.height);

    const AtlasRegion& region = glyph->region;
    const float r = static_cast<float>(color.redF());
    const float g = static_cast<float>(color.greenF());
    const float b = static_cast<float>(color.blueF());
    const float a = static_cast<float>(color.alphaF());
    /* A colour glyph carries its own colours, so the shader must not tint it. */
    const float isColor = glyph->isColor ? 1.0f : 0.0f;

    if (textVertices_.size() >= TextFlushThreshold) {
        flushText();
    }

    textVertices_.push_back({x0, y0, region.u0, region.v0, r, g, b, a, isColor});
    textVertices_.push_back({x1, y0, region.u1, region.v0, r, g, b, a, isColor});
    textVertices_.push_back({x1, y1, region.u1, region.v1, r, g, b, a, isColor});
    textVertices_.push_back({x0, y0, region.u0, region.v0, r, g, b, a, isColor});
    textVertices_.push_back({x1, y1, region.u1, region.v1, r, g, b, a, isColor});
    textVertices_.push_back({x0, y1, region.u0, region.v1, r, g, b, a, isColor});
}

void GLRenderer::ensureCapacity(QOpenGLBuffer& buffer, int& capacityBytes, int neededBytes) {
    if (neededBytes <= capacityBytes) return;

    /* Grow geometrically so a busy frame does not reallocate repeatedly. */
    int newCapacity = capacityBytes > 0 ? capacityBytes : 64 * 1024;
    while (newCapacity < neededBytes) newCapacity *= 2;

    buffer.allocate(newCapacity);
    capacityBytes = newCapacity;
}

void GLRenderer::flushRects(std::vector<RectVertex>& vertices) {
    if (vertices.empty()) return;

    const int bytes = static_cast<int>(vertices.size() * sizeof(RectVertex));

    rectShader_->bind();
    rectShader_->setUniformValue("u_projection", projection_);

    rectVAO_.bind();
    rectVBO_.bind();
    ensureCapacity(rectVBO_, rectCapacityBytes_, bytes);
    rectVBO_.write(0, vertices.data(), bytes);

    gl_->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    rectVBO_.release();
    rectVAO_.release();
    rectShader_->release();

    vertices.clear();
}

void GLRenderer::flushText() {
    if (textVertices_.empty() || !atlas_) return;

    const int bytes = static_cast<int>(textVertices_.size() * sizeof(TextVertex));

    textShader_->bind();
    textShader_->setUniformValue("u_projection", projection_);

    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, atlas_->textureId());

    textVAO_.bind();
    textVBO_.bind();
    ensureCapacity(textVBO_, textCapacityBytes_, bytes);
    textVBO_.write(0, textVertices_.data(), bytes);

    gl_->glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(textVertices_.size()));

    textVBO_.release();
    textVAO_.release();
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
    textShader_->release();

    textVertices_.clear();
}

/*
 * GLRenderer - OpenGL rendering engine implementation
 */

#include "gl_renderer.h"
#include <QFile>
#include <QDebug>
#include <QOpenGLContext>

GLRenderer::GLRenderer()
    : gl_(nullptr)
    , initialized_(false)
    , glyphAtlas_(nullptr)
    , textShader_(nullptr)
    , rectShader_(nullptr)
    , textVBO_(QOpenGLBuffer::VertexBuffer)
    , rectVBO_(QOpenGLBuffer::VertexBuffer)
    , viewportWidth_(0)
    , viewportHeight_(0)
{
}

GLRenderer::~GLRenderer() {
    if (glyphAtlas_) {
        delete glyphAtlas_;
    }
    if (textShader_) {
        delete textShader_;
    }
    if (rectShader_) {
        delete rectShader_;
    }
}

bool GLRenderer::initialize() {
    // Get OpenGL functions from current context
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        qWarning() << "GLRenderer: No current OpenGL context";
        return false;
    }

    gl_ = context->functions();
    if (!gl_) {
        qWarning() << "GLRenderer: Failed to get OpenGL functions";
        return false;
    }

    // Create glyph atlas
    glyphAtlas_ = new GlyphAtlas(gl_, 1024);

    // Load text shader
    textShader_ = new QOpenGLShaderProgram();

    QFile vertFile(":/shaders/shaders/text.vert");
    QFile fragFile(":/shaders/shaders/text.frag");

    if (!vertFile.open(QIODevice::ReadOnly)) {
        qWarning() << "GLRenderer: Failed to open text.vert";
        return false;
    }
    if (!fragFile.open(QIODevice::ReadOnly)) {
        qWarning() << "GLRenderer: Failed to open text.frag";
        return false;
    }

    if (!textShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertFile.readAll())) {
        qWarning() << "GLRenderer: Failed to compile text vertex shader:" << textShader_->log();
        return false;
    }
    if (!textShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragFile.readAll())) {
        qWarning() << "GLRenderer: Failed to compile text fragment shader:" << textShader_->log();
        return false;
    }

    // Bind attribute locations before linking (required for GLSL 150)
    textShader_->bindAttributeLocation("a_position", 0);
    textShader_->bindAttributeLocation("a_texcoord", 1);
    textShader_->bindAttributeLocation("a_color", 2);

    if (!textShader_->link()) {
        qWarning() << "GLRenderer: Failed to link text shader:" << textShader_->log();
        return false;
    }

    // Get uniform locations and set sampler to texture unit 0
    // IMPORTANT: Use native OpenGL for sampler uniforms on Apple Silicon
    textShader_->bind();
    #ifdef Q_OS_MACOS
        // Use native OpenGL to get uniform location and set sampler
        GLuint programId = textShader_->programId();
        textTextureUniformLoc_ = glGetUniformLocation(programId, "u_texture");
        if (textTextureUniformLoc_ >= 0) {
            glUniform1i(textTextureUniformLoc_, 0);  // Bind to texture unit 0
            qDebug() << "GLRenderer: Set u_texture sampler to unit 0 (location:" << textTextureUniformLoc_ << ")";
        } else {
            qWarning() << "GLRenderer: Failed to get u_texture uniform location";
        }
    #else
        textTextureUniformLoc_ = textShader_->uniformLocation("u_texture");
        textShader_->setUniformValue(textTextureUniformLoc_, 0);
    #endif
    textShader_->release();

    // Load rect shader
    rectShader_ = new QOpenGLShaderProgram();

    QFile rectVertFile(":/shaders/shaders/rect.vert");
    QFile rectFragFile(":/shaders/shaders/rect.frag");

    if (!rectVertFile.open(QIODevice::ReadOnly)) {
        qWarning() << "GLRenderer: Failed to open rect.vert";
        return false;
    }
    if (!rectFragFile.open(QIODevice::ReadOnly)) {
        qWarning() << "GLRenderer: Failed to open rect.frag";
        return false;
    }

    if (!rectShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, rectVertFile.readAll())) {
        qWarning() << "GLRenderer: Failed to compile rect vertex shader:" << rectShader_->log();
        return false;
    }
    if (!rectShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, rectFragFile.readAll())) {
        qWarning() << "GLRenderer: Failed to compile rect fragment shader:" << rectShader_->log();
        return false;
    }

    // Bind attribute locations before linking (required for GLSL 150)
    rectShader_->bindAttributeLocation("a_position", 0);
    rectShader_->bindAttributeLocation("a_color", 1);

    if (!rectShader_->link()) {
        qWarning() << "GLRenderer: Failed to link rect shader:" << rectShader_->log();
        return false;
    }

    // Create text VAO and VBO
    textVAO_.create();
    textVAO_.bind();

    textVBO_.create();
    textVBO_.bind();
    textVBO_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    textVBO_.allocate(MAX_TEXT_VERTICES * sizeof(TextVertex));

    // Set up text vertex attributes
    // Layout: position(2), texcoord(2), color(4)
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)0);

    gl_->glEnableVertexAttribArray(1);
    gl_->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)(2 * sizeof(float)));

    gl_->glEnableVertexAttribArray(2);
    gl_->glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)(4 * sizeof(float)));

    textVAO_.release();
    textVBO_.release();

    // Create rect VAO and VBO
    rectVAO_.create();
    rectVAO_.bind();

    rectVBO_.create();
    rectVBO_.bind();
    rectVBO_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    rectVBO_.allocate(MAX_RECT_VERTICES * sizeof(RectVertex));

    // Set up rect vertex attributes
    // Layout: position(2), color(4)
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(RectVertex), (void*)0);

    gl_->glEnableVertexAttribArray(1);
    gl_->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(RectVertex), (void*)(2 * sizeof(float)));

    rectVAO_.release();
    rectVBO_.release();

    // Enable blending for transparency
    gl_->glEnable(GL_BLEND);
    gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    initialized_ = true;
    qDebug() << "GLRenderer: Initialized successfully";
    return true;
}

bool GLRenderer::loadFont(const QString& path, FontStyle style, int sizePt, int dpi) {
    bool success = fontManager_.load(path.toStdString(), style, sizePt, dpi);
    if (success && style == FontStyleRegular) {
        // Clear glyph cache when changing fonts
        if (glyphAtlas_) {
            glyphAtlas_->clear();
        }
    }
    return success;
}

bool GLRenderer::loadDefaultFont(int sizePt, int dpi) {
    bool success = fontManager_.loadDefault(sizePt, dpi);
    if (success && glyphAtlas_) {
        glyphAtlas_->clear();
    }
    return success;
}

void GLRenderer::setFontSize(int sizePt, int dpi) {
    fontManager_.setSize(sizePt, dpi);
    if (glyphAtlas_) {
        glyphAtlas_->clear();
    }
}

FontMetrics GLRenderer::getFontMetrics() const {
    return fontManager_.getMetrics();
}

void GLRenderer::beginFrame(int width, int height) {
    if (!initialized_) return;

    viewportWidth_ = width;
    viewportHeight_ = height;

    // Set up orthographic projection (0,0 = top-left, width,height = bottom-right)
    projection_.setToIdentity();
    projection_.ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, -1.0f, 1.0f);

    // Clear batches
    batch_.textVertices.clear();
    batch_.rectVertices.clear();
}

void GLRenderer::endFrame() {
    if (!initialized_) return;

    // Flush remaining batches
    flushTextBatch();
    flushRectBatch();
}

void GLRenderer::clear(const QColor& color) {
    if (!gl_) return;
    gl_->glClearColor(color.redF(), color.greenF(), color.blueF(), color.alphaF());
    gl_->glClear(GL_COLOR_BUFFER_BIT);
}

void GLRenderer::drawText(const QString& text, float x, float y, const QColor& color, FontStyle style) {
    if (!initialized_ || !fontManager_.isValid()) return;

    float cursorX = x;
    float cr = color.redF();
    float cg = color.greenF();
    float cb = color.blueF();
    float ca = color.alphaF();

    for (QChar ch : text) {
        uint32_t codepoint = ch.unicode();

        // Check if glyph is cached
        const CachedGlyph* cached = glyphAtlas_->getGlyph(codepoint, style);

        if (!cached) {
            // Rasterize and cache the glyph
            GlyphBitmap glyph;
            if (fontManager_.rasterizeGlyph(codepoint, style, glyph)) {
                glyphAtlas_->cacheGlyph(codepoint, style,
                                       glyph.bitmap, glyph.width, glyph.height,
                                       glyph.bearingX, glyph.bearingY, glyph.advanceX);
                cached = glyphAtlas_->getGlyph(codepoint, style);
            }
        }

        if (cached && cached->isValid) {
            const AtlasRegion& region = cached->region;

            // Calculate glyph position
            float glyphX = cursorX + cached->bearingX;
            float glyphY = y - cached->bearingY;

            // Only render if glyph has non-zero dimensions
            if (region.width > 0 && region.height > 0) {
                // Create 6 vertices for 2 triangles (quad)
                // Triangle 1: top-left, top-right, bottom-right
                // Triangle 2: top-left, bottom-right, bottom-left

                float x0 = glyphX;
                float y0 = glyphY;
                float x1 = glyphX + region.width;
                float y1 = glyphY + region.height;

                // Check if we need to flush
                if (batch_.textVertices.size() + 6 > MAX_TEXT_VERTICES) {
                    flushTextBatch();
                }

                // Add vertices
                batch_.textVertices.append({x0, y0, region.u0, region.v0, cr, cg, cb, ca});
                batch_.textVertices.append({x1, y0, region.u1, region.v0, cr, cg, cb, ca});
                batch_.textVertices.append({x1, y1, region.u1, region.v1, cr, cg, cb, ca});

                batch_.textVertices.append({x0, y0, region.u0, region.v0, cr, cg, cb, ca});
                batch_.textVertices.append({x1, y1, region.u1, region.v1, cr, cg, cb, ca});
                batch_.textVertices.append({x0, y1, region.u0, region.v1, cr, cg, cb, ca});
            }

            cursorX += cached->advanceX;
        } else {
            // Fallback: advance by cell width
            FontMetrics metrics = fontManager_.getMetrics();
            cursorX += metrics.cellWidth;
        }
    }
}

void GLRenderer::drawRect(float x, float y, float width, float height, const QColor& color) {
    if (!initialized_) return;

    float cr = color.redF();
    float cg = color.greenF();
    float cb = color.blueF();
    float ca = color.alphaF();

    // Check if we need to flush
    if (batch_.rectVertices.size() + 6 > MAX_RECT_VERTICES) {
        flushRectBatch();
    }

    float x0 = x;
    float y0 = y;
    float x1 = x + width;
    float y1 = y + height;

    // Add vertices for filled quad
    batch_.rectVertices.append({x0, y0, cr, cg, cb, ca});
    batch_.rectVertices.append({x1, y0, cr, cg, cb, ca});
    batch_.rectVertices.append({x1, y1, cr, cg, cb, ca});

    batch_.rectVertices.append({x0, y0, cr, cg, cb, ca});
    batch_.rectVertices.append({x1, y1, cr, cg, cb, ca});
    batch_.rectVertices.append({x0, y1, cr, cg, cb, ca});
}

void GLRenderer::drawRectOutline(float x, float y, float width, float height, const QColor& color, float lineWidth) {
    if (!initialized_) return;

    // Draw 4 rectangles for the outline
    drawRect(x, y, width, lineWidth, color);                    // Top
    drawRect(x, y + height - lineWidth, width, lineWidth, color); // Bottom
    drawRect(x, y, lineWidth, height, color);                   // Left
    drawRect(x + width - lineWidth, y, lineWidth, height, color); // Right
}

void GLRenderer::flushTextBatch() {
    if (!initialized_ || batch_.textVertices.isEmpty()) return;

    // Bind shader and set uniforms
    textShader_->bind();
    textShader_->setUniformValue("u_projection", projection_);

    // Activate texture unit 0 and bind the atlas texture
    // Use native OpenGL on macOS for consistency with texture creation
    #ifdef Q_OS_MACOS
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glyphAtlas_->textureId());
        if (textTextureUniformLoc_ >= 0) {
            glUniform1i(textTextureUniformLoc_, 0);
        }
    #else
        gl_->glActiveTexture(GL_TEXTURE0);
        gl_->glBindTexture(GL_TEXTURE_2D, glyphAtlas_->textureId());
        textShader_->setUniformValue("u_texture", 0);
    #endif

    // Upload vertex data
    textVAO_.bind();
    textVBO_.bind();
    textVBO_.write(0, batch_.textVertices.constData(), batch_.textVertices.size() * sizeof(TextVertex));

    // Draw
    gl_->glDrawArrays(GL_TRIANGLES, 0, batch_.textVertices.size());

    textVAO_.release();
    textVBO_.release();
    textShader_->release();

    // Clear batch
    batch_.textVertices.clear();
}

void GLRenderer::flushRectBatch() {
    if (!initialized_ || batch_.rectVertices.isEmpty()) return;

    rectShader_->bind();
    rectShader_->setUniformValue("u_projection", projection_);

    // Upload vertex data
    rectVAO_.bind();
    rectVBO_.bind();
    rectVBO_.write(0, batch_.rectVertices.constData(), batch_.rectVertices.size() * sizeof(RectVertex));

    // Draw
    gl_->glDrawArrays(GL_TRIANGLES, 0, batch_.rectVertices.size());

    rectVAO_.release();
    rectVBO_.release();
    rectShader_->release();

    // Clear batch
    batch_.rectVertices.clear();
}

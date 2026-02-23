/*
 * GlyphAtlas - OpenGL texture atlas implementation
 */

#include "glyph_atlas.h"
#include <cstring>
#include <QDebug>

GlyphAtlas::GlyphAtlas(QOpenGLFunctions* gl, int initialSize)
    : gl_(gl)
    , textureId_(0)
    , size_(0)
    , currentY_(0)
    , allocatedPixels_(0)
{
    if (!gl_) {
        qWarning() << "GlyphAtlas: null OpenGL functions";
        return;
    }

    // Round up to power of 2
    size_ = 1;
    while (size_ < initialSize) {
        size_ *= 2;
    }

    // WORKAROUND for Apple Silicon Qt bug: Use all native OpenGL calls
    // Qt's OpenGL wrappers have bugs on Apple Silicon
    #ifdef Q_OS_MACOS
        // Debug: Check OpenGL version
        const GLubyte* version = glGetString(GL_VERSION);
        const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);
        qDebug() << "GlyphAtlas: OpenGL version:" << (const char*)version;
        qDebug() << "GlyphAtlas: GLSL version:" << (const char*)glslVersion;

        // Clear any pre-existing OpenGL errors
        while (glGetError() != GL_NO_ERROR);

        // Create OpenGL texture with native calls
        textureId_ = 0;  // Initialize to 0
        glGenTextures(1, &textureId_);
        qDebug() << "GlyphAtlas: glGenTextures returned ID:" << textureId_;

        if (textureId_ == 0) {
            qWarning() << "GlyphAtlas: glGenTextures failed to create texture!";
            return;
        }

        GLenum genError = glGetError();
        if (genError != GL_NO_ERROR) {
            qWarning() << "GlyphAtlas: glGenTextures error:" << genError;
        }

        glBindTexture(GL_TEXTURE_2D, textureId_);
        GLenum bindError = glGetError();
        if (bindError != GL_NO_ERROR) {
            qWarning() << "GlyphAtlas: glBindTexture failed with error:" << bindError;
        } else {
            qDebug() << "GlyphAtlas: Successfully bound texture" << textureId_;
        }

        // Allocate single-channel texture (GL_RED for grayscale glyphs - saves 75% memory!)
        QByteArray zeros(size_ * size_, 0);  // Single channel
        qDebug() << "GlyphAtlas: Creating" << size_ << "x" << size_ << "GL_RED texture";
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, size_, size_, 0, GL_RED, GL_UNSIGNED_BYTE, zeros.constData());

        // Set texture parameters AFTER allocating storage
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

        // CRITICAL FIX: Set texture swizzle AFTER texture storage allocation
        // This tells macOS Metal how to map GL_RED to shader's vec4 sampler
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        qDebug() << "GlyphAtlas: Set texture swizzle (R,R,R,1) for GL_RED";

        // Check for errors
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            qWarning() << "GlyphAtlas: glTexImage2D error:" << error;
        } else {
            qDebug() << "GlyphAtlas: GL_RED texture created successfully, ID:" << textureId_;
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    #else
        // Create OpenGL texture with Qt wrappers (non-macOS)
        gl_->glGenTextures(1, &textureId_);
        gl_->glBindTexture(GL_TEXTURE_2D, textureId_);

        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size_, size_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        GLenum error = gl_->glGetError();
        if (error != GL_NO_ERROR) {
            qWarning() << "GlyphAtlas: OpenGL error creating texture:" << error;
        }

        gl_->glBindTexture(GL_TEXTURE_2D, 0);
    #endif
}

GlyphAtlas::~GlyphAtlas() {
    if (textureId_ && gl_) {
        gl_->glDeleteTextures(1, &textureId_);
    }
}

bool GlyphAtlas::allocate(int width, int height, AtlasRegion& out) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    int paddedWidth = width + ATLAS_PADDING;
    int paddedHeight = height + ATLAS_PADDING;

    // Try to find an existing shelf that fits
    for (Shelf& shelf : shelves_) {
        // Check if glyph fits on this shelf
        if (shelf.height >= paddedHeight &&
            shelf.xCursor + paddedWidth <= size_) {

            // Allocate from this shelf
            out.x = shelf.xCursor;
            out.y = shelf.y;
            out.width = width;
            out.height = height;

            // Calculate UV coordinates
            float invSize = 1.0f / static_cast<float>(size_);
            out.u0 = static_cast<float>(out.x) * invSize;
            out.v0 = static_cast<float>(out.y) * invSize;
            out.u1 = static_cast<float>(out.x + width) * invSize;
            out.v1 = static_cast<float>(out.y + height) * invSize;

            shelf.xCursor += paddedWidth;
            allocatedPixels_ += width * height;

            return true;
        }
    }

    // No existing shelf fits, create a new one
    if (currentY_ + paddedHeight > size_) {
        // Atlas is full
        return false;
    }

    if (shelves_.size() >= MAX_SHELVES) {
        return false;
    }

    // Create new shelf
    Shelf newShelf;
    newShelf.y = currentY_;
    newShelf.height = paddedHeight;
    newShelf.xCursor = paddedWidth;

    shelves_.append(newShelf);
    currentY_ += paddedHeight;

    // Allocate from new shelf
    out.x = 0;
    out.y = newShelf.y;
    out.width = width;
    out.height = height;

    float invSize = 1.0f / static_cast<float>(size_);
    out.u0 = static_cast<float>(out.x) * invSize;
    out.v0 = static_cast<float>(out.y) * invSize;
    out.u1 = static_cast<float>(out.x + width) * invSize;
    out.v1 = static_cast<float>(out.y + height) * invSize;

    allocatedPixels_ += width * height;

    return true;
}

void GlyphAtlas::upload(const AtlasRegion& region, const uint8_t* bitmap) {
    if (!bitmap || !gl_) return;

    // Upload grayscale bitmap directly to GL_RED texture (no conversion needed!)
    // WORKAROUND for Apple Silicon Qt bug: Use native OpenGL directly
    #ifdef Q_OS_MACOS
        glBindTexture(GL_TEXTURE_2D, textureId_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // Single-channel data
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                       region.x, region.y,
                       region.width, region.height,
                       GL_RED, GL_UNSIGNED_BYTE, bitmap);
        glBindTexture(GL_TEXTURE_2D, 0);
    #else
        gl_->glBindTexture(GL_TEXTURE_2D, textureId_);
        gl_->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_->glTexSubImage2D(GL_TEXTURE_2D, 0,
                            region.x, region.y,
                            region.width, region.height,
                            GL_RED, GL_UNSIGNED_BYTE, bitmap);
        gl_->glBindTexture(GL_TEXTURE_2D, 0);
    #endif
}

bool GlyphAtlas::cacheGlyph(uint32_t codepoint, int style,
                            const uint8_t* bitmap, int width, int height,
                            int bearingX, int bearingY, int advanceX)
{
    GlyphKey key{codepoint, style};

    // Check if already cached
    if (glyphs_.contains(key)) {
        return true;
    }

    // Cache glyph info
    CachedGlyph glyph;
    glyph.bearingX = bearingX;
    glyph.bearingY = bearingY;
    glyph.advanceX = advanceX;
    glyph.isValid = true;

    // Handle empty glyphs (e.g., space character)
    if (width <= 0 || height <= 0) {
        // Create a dummy region with zero dimensions
        glyph.region.x = 0;
        glyph.region.y = 0;
        glyph.region.width = 0;
        glyph.region.height = 0;
        glyph.region.u0 = 0.0f;
        glyph.region.v0 = 0.0f;
        glyph.region.u1 = 0.0f;
        glyph.region.v1 = 0.0f;
        glyphs_.insert(key, glyph);
        return true;
    }

    // Allocate space in atlas
    AtlasRegion region;
    if (!allocate(width, height, region)) {
        qWarning() << "GlyphAtlas: Failed to allocate glyph" << codepoint << "style" << style;
        return false;
    }

    // Upload bitmap
    if (bitmap) {
        upload(region, bitmap);
    }

    glyph.region = region;
    glyphs_.insert(key, glyph);

    return true;
}

const CachedGlyph* GlyphAtlas::getGlyph(uint32_t codepoint, int style) const {
    GlyphKey key{codepoint, style};
    auto it = glyphs_.find(key);
    if (it != glyphs_.end()) {
        return &(*it);
    }
    return nullptr;
}

bool GlyphAtlas::hasGlyph(uint32_t codepoint, int style) const {
    GlyphKey key{codepoint, style};
    return glyphs_.contains(key);
}

void GlyphAtlas::clear() {
    if (!gl_) return;

    shelves_.clear();
    currentY_ = 0;
    allocatedPixels_ = 0;
    glyphs_.clear();

    // Clear the texture to zero (single-channel)
    QByteArray zeros(size_ * size_, 0);  // GL_RED single channel

    // WORKAROUND for Apple Silicon Qt bug: Use native OpenGL directly
    #ifdef Q_OS_MACOS
        glBindTexture(GL_TEXTURE_2D, textureId_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                       size_, size_,
                       GL_RED, GL_UNSIGNED_BYTE, zeros.constData());
        glBindTexture(GL_TEXTURE_2D, 0);
    #else
        gl_->glBindTexture(GL_TEXTURE_2D, textureId_);
        gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            size_, size_,
                            GL_RED, GL_UNSIGNED_BYTE, zeros.constData());
        gl_->glBindTexture(GL_TEXTURE_2D, 0);
    #endif

    qDebug() << "GlyphAtlas: Cleared";
}

bool GlyphAtlas::isFull() const {
    // Consider full if more than 90% allocated or no vertical space left
    int totalPixels = size_ * size_;
    float usage = static_cast<float>(allocatedPixels_) / static_cast<float>(totalPixels);

    return usage > 0.9f || currentY_ >= size_ - 32;
}

bool GlyphAtlas::grow() {
    if (!gl_) return false;

    int newSize = size_ * 2;

    // Cap at reasonable maximum (4096 or 8192)
    if (newSize > 8192) {
        qWarning() << "GlyphAtlas: Cannot grow beyond 8192x8192";
        return false;
    }

    // Delete old texture and create new one
    gl_->glDeleteTextures(1, &textureId_);

    size_ = newSize;
    shelves_.clear();
    currentY_ = 0;
    allocatedPixels_ = 0;
    glyphs_.clear();

    // WORKAROUND for Apple Silicon Qt bug: Use native OpenGL directly
    #ifdef Q_OS_MACOS
        glGenTextures(1, &textureId_);
        glBindTexture(GL_TEXTURE_2D, textureId_);

        QByteArray zeros(newSize * newSize, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, newSize, newSize, 0, GL_RED, GL_UNSIGNED_BYTE, zeros.constData());

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

        // Set texture swizzle for single-channel texture
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);

        glBindTexture(GL_TEXTURE_2D, 0);
    #else
        gl_->glGenTextures(1, &textureId_);
        gl_->glBindTexture(GL_TEXTURE_2D, textureId_);

        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, newSize, newSize, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        gl_->glBindTexture(GL_TEXTURE_2D, 0);
    #endif

    qDebug() << "GlyphAtlas: Grew to" << newSize << "x" << newSize;

    return true;
}

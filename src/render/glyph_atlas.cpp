/*
 * GlyphAtlas - texture atlas implementation
 *
 * Note on the single code path: earlier revisions carried a duplicated
 * "Apple Silicon workaround" that called the native GL entry points directly on
 * macOS. The actual problem there was a missing swizzle for the single-channel
 * format; now that the atlas is RGBA there is no swizzle to get wrong, and one
 * code path serves every platform.
 */

#include "glyph_atlas.h"
#include <QDebug>
#include <algorithm>

namespace {

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

int roundUpToPowerOfTwo(int value) {
    int result = 1;
    while (result < value) result *= 2;
    return result;
}

} // namespace

GlyphAtlas::GlyphAtlas(QOpenGLFunctions* gl, int initialSize)
    : gl_(gl)
{
    if (!gl_) {
        qCritical() << "GlyphAtlas: no OpenGL functions";
        return;
    }
    createTexture(roundUpToPowerOfTwo(std::max(256, initialSize)));
}

GlyphAtlas::~GlyphAtlas() {
    destroyTexture();
}

bool GlyphAtlas::createTexture(int size) {
    destroyTexture();

    size_ = size;
    resetPacking();

    gl_->glGenTextures(1, &textureId_);
    if (textureId_ == 0) {
        qCritical() << "GlyphAtlas: glGenTextures failed";
        return false;
    }

    gl_->glBindTexture(GL_TEXTURE_2D, textureId_);

    const std::vector<uint8_t> zeros(
        static_cast<size_t>(size_) * static_cast<size_t>(size_) * BytesPerPixel, 0);
    gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size_, size_, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, zeros.data());

    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    gl_->glBindTexture(GL_TEXTURE_2D, 0);

    if (const GLenum error = gl_->glGetError(); error != GL_NO_ERROR) {
        qWarning() << "GlyphAtlas: OpenGL error" << error << "creating" << size_ << "px atlas";
    }

    return true;
}

void GlyphAtlas::destroyTexture() {
    if (textureId_ != 0 && gl_) {
        gl_->glDeleteTextures(1, &textureId_);
    }
    textureId_ = 0;
}

void GlyphAtlas::resetPacking() {
    shelves_.clear();
    shelfCursorY_ = 0;
    glyphs_.clear();
    ++generation_;
}

void GlyphAtlas::clear() {
    if (!gl_ || textureId_ == 0) return;

    resetPacking();

    /* Zeroing the texture is not strictly required (nothing references the old
     * regions any more) but it keeps stale coverage out of any glyph whose
     * bitmap is later uploaded with a smaller footprint. */
    const std::vector<uint8_t> zeros(
        static_cast<size_t>(size_) * static_cast<size_t>(size_) * BytesPerPixel, 0);
    gl_->glBindTexture(GL_TEXTURE_2D, textureId_);
    gl_->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size_, size_,
                         GL_RGBA, GL_UNSIGNED_BYTE, zeros.data());
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
}

bool GlyphAtlas::allocate(int width, int height, AtlasRegion& out) {
    if (width <= 0 || height <= 0) return false;

    const int paddedWidth = width + Padding;
    const int paddedHeight = height + Padding;
    if (paddedWidth > size_ || paddedHeight > size_) return false;

    auto assignRegion = [&](int x, int y) {
        const float invSize = 1.0f / static_cast<float>(size_);
        out.x = x;
        out.y = y;
        out.width = width;
        out.height = height;
        out.u0 = static_cast<float>(x) * invSize;
        out.v0 = static_cast<float>(y) * invSize;
        out.u1 = static_cast<float>(x + width) * invSize;
        out.v1 = static_cast<float>(y + height) * invSize;
    };

    /* Reuse a shelf that is tall enough and still has room. */
    for (Shelf& shelf : shelves_) {
        if (shelf.height >= paddedHeight && shelf.xCursor + paddedWidth <= size_) {
            assignRegion(shelf.xCursor, shelf.y);
            shelf.xCursor += paddedWidth;
            return true;
        }
    }

    if (shelfCursorY_ + paddedHeight > size_) return false;

    shelves_.push_back(Shelf{shelfCursorY_, paddedHeight, paddedWidth});
    assignRegion(0, shelfCursorY_);
    shelfCursorY_ += paddedHeight;
    return true;
}

void GlyphAtlas::upload(const AtlasRegion& region, const GlyphBitmap& bitmap) {
    if (bitmap.pixels.empty() || !gl_ || textureId_ == 0) return;

    const uint8_t* source = bitmap.pixels.data();

    if (!bitmap.isColor) {
        /*
         * Widen the coverage mask to RGBA. White with the coverage in alpha
         * means the shader can tint it with the cell's foreground colour, while
         * a colour glyph in the same texture is used verbatim.
         */
        const size_t pixelCount =
            static_cast<size_t>(region.width) * static_cast<size_t>(region.height);
        uploadScratch_.resize(pixelCount * BytesPerPixel);
        for (size_t i = 0; i < pixelCount; ++i) {
            uploadScratch_[i * 4 + 0] = 255;
            uploadScratch_[i * 4 + 1] = 255;
            uploadScratch_[i * 4 + 2] = 255;
            uploadScratch_[i * 4 + 3] = bitmap.pixels[i];
        }
        source = uploadScratch_.data();
    }

    gl_->glBindTexture(GL_TEXTURE_2D, textureId_);
    /* Rows are tightly packed; the default 4-byte alignment happens to suit
     * RGBA, but being explicit keeps this correct if the format ever changes. */
    gl_->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl_->glTexSubImage2D(GL_TEXTURE_2D, 0,
                         region.x, region.y, region.width, region.height,
                         GL_RGBA, GL_UNSIGNED_BYTE, source);
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
}

bool GlyphAtlas::grow() {
    if (size_ >= MaxSize) {
        /* At the cap, start over instead: a terminal's working set of glyphs is
         * small, so a full flush costs one frame of re-rasterization and is
         * preferable to failing to draw. */
        clear();
        return true;
    }
    return createTexture(size_ * 2);
}

const CachedGlyph* GlyphAtlas::glyph(char32_t codepoint, FontStyle style, FontManager& fonts) {
    if (textureId_ == 0) return nullptr;

    const uint64_t key = makeKey(codepoint, style);
    if (const auto it = glyphs_.find(key); it != glyphs_.end()) {
        return &it->second;
    }

    GlyphBitmap bitmap;
    if (!fonts.rasterize(codepoint, style, bitmap)) {
        return nullptr;
    }

    CachedGlyph cached;
    cached.bearingX = bitmap.bearingX;
    cached.bearingY = bitmap.bearingY;
    cached.advanceX = bitmap.advanceX;
    cached.isColor = bitmap.isColor;

    if (bitmap.isEmpty()) {
        /* Space and other blanks: cache the metrics with an empty region so
         * they are not re-rasterized on every frame. */
        return &glyphs_.emplace(key, cached).first->second;
    }

    if (!allocate(bitmap.width, bitmap.height, cached.region)) {
        if (!grow() || !allocate(bitmap.width, bitmap.height, cached.region)) {
            qWarning() << "GlyphAtlas: cannot fit glyph" << static_cast<uint32_t>(codepoint);
            return nullptr;
        }
    }

    upload(cached.region, bitmap);
    return &glyphs_.emplace(key, cached).first->second;
}

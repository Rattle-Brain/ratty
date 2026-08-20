/*
 * GlyphAtlas - one OpenGL texture holding every rasterized glyph
 *
 * Glyphs are packed with a shelf (row-based) allocator into a single GL_RGBA8
 * texture, so a whole screen of text is one draw call.
 *
 * RGBA rather than a single coverage channel, because colour emoji have to live
 * here too. A coverage mask is widened to (255, 255, 255, coverage) on upload
 * and tinted by the shader; a colour glyph is stored as-is and drawn untinted.
 * One texture and one draw call for both is considerably simpler than two
 * atlases, and 4 MiB for a 1024px atlas is not worth optimising.
 *
 * Filtering is GL_NEAREST on purpose. The atlas is only ever sampled 1:1 -- one
 * texel per physical pixel -- so linear filtering cannot improve anything and
 * merely smears a glyph across neighbouring pixels whenever a quad lands even
 * slightly off-grid. Combined with rasterizing at physical pixel size and
 * snapping quads to integers, this is what makes the text crisp.
 */

#ifndef RENDER_GLYPH_ATLAS_H
#define RENDER_GLYPH_ATLAS_H

#include "font_manager.h"
#include <QOpenGLFunctions>
#include <cstdint>
#include <unordered_map>
#include <vector>

/* Sub-rectangle of the atlas, in pixels and in normalized UV. */
struct AtlasRegion {
    int x = 0, y = 0;
    int width = 0, height = 0;
    float u0 = 0.0f, v0 = 0.0f;
    float u1 = 0.0f, v1 = 0.0f;
};

/* Everything needed to place a cached glyph on screen. */
struct CachedGlyph {
    AtlasRegion region;
    int bearingX = 0;
    int bearingY = 0;
    int advanceX = 0;
    /* True for a colour emoji: the texels are the final colour and must not be
     * tinted with the cell's foreground. */
    bool isColor = false;
};

class GlyphAtlas {
public:
    /* `gl` must belong to the context that will sample the texture. */
    GlyphAtlas(QOpenGLFunctions* gl, int initialSize = 1024);
    ~GlyphAtlas();

    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    bool isValid() const { return textureId_ != 0; }
    GLuint textureId() const { return textureId_; }
    int size() const { return size_; }

    /*
     * Look a glyph up, rasterizing and uploading it on first use. Returns
     * nullptr only when the glyph cannot be rasterized at all. Growing or
     * evicting the atlas is handled internally, so callers never see a
     * "texture full" failure.
     */
    const CachedGlyph* glyph(char32_t codepoint, FontStyle style, FontManager& fonts);

    /* Drop every cached glyph (font or size change). */
    void clear();

    /*
     * Bumped whenever the cache is dropped or the texture is reallocated. Any
     * UV coordinate obtained before the change refers to a layout that no longer
     * exists, so a caller batching quads across many lookups must notice and
     * start over.
     */
    uint64_t generation() const { return generation_; }

private:
    /* Codepoint + style in one integer: cheap to hash, no custom hasher. */
    static uint64_t makeKey(char32_t codepoint, FontStyle style) {
        return (static_cast<uint64_t>(codepoint) << 8) | static_cast<uint8_t>(style);
    }

    struct Shelf {
        int y = 0;
        int height = 0;
        int xCursor = 0;
    };

    bool createTexture(int size);
    void destroyTexture();
    bool allocate(int width, int height, AtlasRegion& out);
    /* Uploads `bitmap` into `region`, widening a coverage mask to RGBA. */
    void upload(const AtlasRegion& region, const GlyphBitmap& bitmap);
    /* Double the texture and drop the cache; glyphs re-rasterize on demand. */
    bool grow();
    void resetPacking();

    QOpenGLFunctions* gl_ = nullptr;
    GLuint textureId_ = 0;
    int size_ = 0;

    std::vector<Shelf> shelves_;
    int shelfCursorY_ = 0;
    uint64_t generation_ = 1;

    std::unordered_map<uint64_t, CachedGlyph> glyphs_;

    /* 1 px gutter so GL_NEAREST rounding can never pick up a neighbour. */
    static constexpr int Padding = 1;
    static constexpr int MaxSize = 4096;
    static constexpr int BytesPerPixel = 4;

    /* Scratch buffer for widening coverage masks, reused across uploads. */
    std::vector<uint8_t> uploadScratch_;
};

#endif /* RENDER_GLYPH_ATLAS_H */

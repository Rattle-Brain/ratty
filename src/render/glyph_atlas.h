/*
 * GlyphAtlas - OpenGL texture atlas for glyph caching
 *
 * Uses shelf-based bin packing algorithm to efficiently pack glyphs
 * into a single OpenGL texture for GPU rendering
 */

#ifndef RENDER_GLYPH_ATLAS_H
#define RENDER_GLYPH_ATLAS_H

// Include native OpenGL on macOS for direct API access
#ifdef Q_OS_MACOS
#include <OpenGL/gl3.h>
#endif

#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QHash>
#include <cstdint>
#include <utility>

/* Atlas region (pixel position and UV coordinates) */
struct AtlasRegion {
    int x, y;           // Pixel position in atlas
    int width, height;  // Pixel dimensions
    float u0, v0;       // Top-left UV
    float u1, v1;       // Bottom-right UV
};

/* Glyph cache key */
struct GlyphKey {
    uint32_t codepoint;
    int style;

    bool operator==(const GlyphKey& other) const {
        return codepoint == other.codepoint && style == other.style;
    }
};

// Hash function for QHash
inline uint qHash(const GlyphKey& key, uint seed = 0) {
    return qHash(key.codepoint, seed) ^ qHash(key.style, seed);
}

/* Cached glyph information */
struct CachedGlyph {
    AtlasRegion region;
    int bearingX;
    int bearingY;
    int advanceX;
    bool isValid;
};

class GlyphAtlas {
public:
    explicit GlyphAtlas(QOpenGLFunctions* gl, int initialSize = 1024);
    ~GlyphAtlas();

    // Delete copy, allow move
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    // Cache a glyph
    bool cacheGlyph(uint32_t codepoint, int style,
                   const uint8_t* bitmap, int width, int height,
                   int bearingX, int bearingY, int advanceX);

    // Get cached glyph (returns nullptr if not cached)
    const CachedGlyph* getGlyph(uint32_t codepoint, int style) const;

    // Check if glyph is cached
    bool hasGlyph(uint32_t codepoint, int style) const;

    // Get OpenGL texture ID
    GLuint textureId() const { return textureId_; }

    // Get atlas dimensions
    int size() const { return size_; }

    // Clear all cached glyphs
    void clear();

    // Check if atlas is nearly full
    bool isFull() const;

    // Grow atlas (doubles size, requires re-caching all glyphs)
    bool grow();

private:
    struct Shelf {
        int y;          // Y position of shelf
        int height;     // Height of shelf
        int xCursor;    // Current X position for next allocation
    };

    bool allocate(int width, int height, AtlasRegion& out);
    void upload(const AtlasRegion& region, const uint8_t* bitmap);

    QOpenGLFunctions* gl_;
    GLuint textureId_;
    int size_;

    // Shelf-based packing
    QVector<Shelf> shelves_;
    int currentY_;

    // Glyph cache
    QHash<GlyphKey, CachedGlyph> glyphs_;

    // Statistics
    int allocatedPixels_;

    static constexpr int ATLAS_PADDING = 1;
    static constexpr int MAX_SHELVES = 256;
};

#endif /* RENDER_GLYPH_ATLAS_H */

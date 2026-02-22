#ifndef RENDER_GLYPH_CACHE_H
#define RENDER_GLYPH_CACHE_H

#include "texture_atlas.h"
#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct FontManager FontManager;

/* Cached glyph entry */
typedef struct {
    AtlasRegion region;   /* Position in texture atlas */
    int bearing_x;        /* Horizontal offset from pen */
    int bearing_y;        /* Vertical offset from baseline */
    int advance_x;        /* Horizontal advance */
    bool valid;           /* Entry is valid */
} CachedGlyph;

/* Glyph cache state (opaque) */
typedef struct GlyphCache GlyphCache;

/* Lifecycle */
GlyphCache *glyph_cache_create(FontManager *fm, int atlas_size);
void glyph_cache_destroy(GlyphCache *cache);

/* Lookup or rasterize glyph by glyph index */
const CachedGlyph *glyph_cache_get(GlyphCache *cache, uint32_t glyph_index, int style);

/* Lookup or rasterize glyph by codepoint */
const CachedGlyph *glyph_cache_get_codepoint(GlyphCache *cache, uint32_t codepoint, int style);

/* Batch prefetch (rasterizes glyphs ahead of time) */
void glyph_cache_prefetch(GlyphCache *cache, const uint32_t *glyph_indices, const int *styles, int count);

/* Get underlying atlas texture ID */
uint32_t glyph_cache_get_texture(GlyphCache *cache);

/* Get texture atlas (for advanced usage) */
TextureAtlas *glyph_cache_get_atlas(GlyphCache *cache);

/* Clear cache (on font change) */
void glyph_cache_clear(GlyphCache *cache);

/* Statistics */
int glyph_cache_count(GlyphCache *cache);
int glyph_cache_capacity(GlyphCache *cache);

#endif /* RENDER_GLYPH_CACHE_H */

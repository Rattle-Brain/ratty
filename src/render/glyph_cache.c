#include "glyph_cache.h"
#include "font.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_CACHE_CAPACITY 4096
#define HASH_EMPTY 0xFFFFFFFF

/* Cache key: combines glyph index and style */
typedef struct {
    uint32_t glyph_index;
    uint8_t style;
} GlyphKey;

/* Cache entry */
typedef struct {
    GlyphKey key;
    CachedGlyph glyph;
    bool occupied;
} CacheEntry;

struct GlyphCache {
    FontManager *font_manager;
    TextureAtlas *atlas;
    bool owns_atlas;

    CacheEntry *entries;
    int capacity;
    int count;
};

/* Hash function for glyph key */
static uint32_t hash_key(uint32_t glyph_index, int style) {
    uint32_t key = (glyph_index << 8) | (style & 0xFF);
    /* FNV-1a inspired hash */
    key ^= key >> 16;
    key *= 0x85ebca6b;
    key ^= key >> 13;
    key *= 0xc2b2ae35;
    key ^= key >> 16;
    return key;
}

/* Find entry or empty slot */
static int find_slot(GlyphCache *cache, uint32_t glyph_index, int style) {
    uint32_t hash = hash_key(glyph_index, style);
    int index = hash % cache->capacity;
    int start = index;

    do {
        CacheEntry *entry = &cache->entries[index];
        if (!entry->occupied) {
            return index;  /* Empty slot */
        }
        if (entry->key.glyph_index == glyph_index && entry->key.style == style) {
            return index;  /* Found existing */
        }
        index = (index + 1) % cache->capacity;
    } while (index != start);

    return -1;  /* Cache is full */
}

GlyphCache *glyph_cache_create(FontManager *fm, int atlas_size) {
    if (!fm) return NULL;

    GlyphCache *cache = calloc(1, sizeof(GlyphCache));
    if (!cache) return NULL;

    cache->font_manager = fm;
    cache->capacity = DEFAULT_CACHE_CAPACITY;
    cache->count = 0;

    cache->entries = calloc(cache->capacity, sizeof(CacheEntry));
    if (!cache->entries) {
        free(cache);
        return NULL;
    }

    cache->atlas = texture_atlas_create(atlas_size > 0 ? atlas_size : 1024);
    if (!cache->atlas) {
        free(cache->entries);
        free(cache);
        return NULL;
    }
    cache->owns_atlas = true;

    return cache;
}

void glyph_cache_destroy(GlyphCache *cache) {
    if (!cache) return;

    if (cache->owns_atlas && cache->atlas) {
        texture_atlas_destroy(cache->atlas);
    }

    free(cache->entries);
    free(cache);
}

const CachedGlyph *glyph_cache_get(GlyphCache *cache, uint32_t glyph_index, int style) {
    if (!cache || glyph_index == 0) return NULL;

    int slot = find_slot(cache, glyph_index, style);
    if (slot < 0) {
        /* Cache is full, try to grow atlas and clear cache */
        if (texture_atlas_grow(cache->atlas)) {
            glyph_cache_clear(cache);
            slot = find_slot(cache, glyph_index, style);
        }
        if (slot < 0) return NULL;
    }

    CacheEntry *entry = &cache->entries[slot];

    /* Return if already cached */
    if (entry->occupied && entry->key.glyph_index == glyph_index && entry->key.style == style) {
        return &entry->glyph;
    }

    /* Rasterize the glyph */
    GlyphBitmap bitmap;
    if (!font_manager_rasterize_glyph_index(cache->font_manager, glyph_index, style, &bitmap)) {
        return NULL;
    }

    /* Allocate space in atlas */
    AtlasRegion region;
    if (bitmap.width > 0 && bitmap.height > 0) {
        if (!texture_atlas_allocate(cache->atlas, bitmap.width, bitmap.height, &region)) {
            /* Atlas full, try to grow */
            if (texture_atlas_grow(cache->atlas)) {
                glyph_cache_clear(cache);
                slot = find_slot(cache, glyph_index, style);
                if (slot < 0 || !texture_atlas_allocate(cache->atlas, bitmap.width, bitmap.height, &region)) {
                    glyph_bitmap_free(&bitmap);
                    return NULL;
                }
                entry = &cache->entries[slot];
            } else {
                glyph_bitmap_free(&bitmap);
                return NULL;
            }
        }

        /* Upload to atlas */
        texture_atlas_upload(cache->atlas, &region, bitmap.bitmap);
    } else {
        /* Empty glyph (like space) */
        memset(&region, 0, sizeof(region));
    }

    /* Store in cache */
    entry->key.glyph_index = glyph_index;
    entry->key.style = style;
    entry->glyph.region = region;
    entry->glyph.bearing_x = bitmap.bearing_x;
    entry->glyph.bearing_y = bitmap.bearing_y;
    entry->glyph.advance_x = bitmap.advance_x;
    entry->glyph.valid = true;
    entry->occupied = true;

    cache->count++;

    glyph_bitmap_free(&bitmap);

    return &entry->glyph;
}

const CachedGlyph *glyph_cache_get_codepoint(GlyphCache *cache, uint32_t codepoint, int style) {
    if (!cache) return NULL;

    uint32_t glyph_index = font_manager_get_glyph_index(cache->font_manager, codepoint, style);
    if (glyph_index == 0 && codepoint != 0) {
        /* Try regular style as fallback */
        glyph_index = font_manager_get_glyph_index(cache->font_manager, codepoint, FONT_STYLE_REGULAR);
    }

    if (glyph_index == 0) return NULL;

    return glyph_cache_get(cache, glyph_index, style);
}

void glyph_cache_prefetch(GlyphCache *cache, const uint32_t *glyph_indices, const int *styles, int count) {
    if (!cache || !glyph_indices || !styles || count <= 0) return;

    for (int i = 0; i < count; i++) {
        glyph_cache_get(cache, glyph_indices[i], styles[i]);
    }
}

uint32_t glyph_cache_get_texture(GlyphCache *cache) {
    if (!cache || !cache->atlas) return 0;
    return texture_atlas_get_texture(cache->atlas);
}

TextureAtlas *glyph_cache_get_atlas(GlyphCache *cache) {
    return cache ? cache->atlas : NULL;
}

void glyph_cache_clear(GlyphCache *cache) {
    if (!cache) return;

    memset(cache->entries, 0, cache->capacity * sizeof(CacheEntry));
    cache->count = 0;

    if (cache->atlas) {
        texture_atlas_clear(cache->atlas);
    }
}

int glyph_cache_count(GlyphCache *cache) {
    return cache ? cache->count : 0;
}

int glyph_cache_capacity(GlyphCache *cache) {
    return cache ? cache->capacity : 0;
}

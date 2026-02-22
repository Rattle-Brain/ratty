#ifndef RENDER_TEXTURE_ATLAS_H
#define RENDER_TEXTURE_ATLAS_H

#include <stdint.h>
#include <stdbool.h>

/* Atlas region (pixel position and UV coordinates) */
typedef struct {
    int x, y;           /* Pixel position in atlas */
    int width, height;  /* Pixel dimensions */
    float u0, v0;       /* Top-left UV */
    float u1, v1;       /* Bottom-right UV */
} AtlasRegion;

/* Texture atlas state (opaque) */
typedef struct TextureAtlas TextureAtlas;

/* Lifecycle */
TextureAtlas *texture_atlas_create(int initial_size);
void texture_atlas_destroy(TextureAtlas *atlas);

/* Allocation - returns false if atlas is full */
bool texture_atlas_allocate(TextureAtlas *atlas, int width, int height, AtlasRegion *out);

/* Upload bitmap data to allocated region */
void texture_atlas_upload(TextureAtlas *atlas, const AtlasRegion *region, const uint8_t *bitmap);

/* Get OpenGL texture ID */
uint32_t texture_atlas_get_texture(TextureAtlas *atlas);

/* Get atlas dimensions */
int texture_atlas_get_size(TextureAtlas *atlas);

/* Clear atlas (invalidates all regions) */
void texture_atlas_clear(TextureAtlas *atlas);

/* Check if atlas needs rebuild (is full or nearly full) */
bool texture_atlas_is_full(TextureAtlas *atlas);

/* Resize atlas (doubles size, clears all data) */
bool texture_atlas_grow(TextureAtlas *atlas);

#endif /* RENDER_TEXTURE_ATLAS_H */

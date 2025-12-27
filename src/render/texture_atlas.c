#include "texture_atlas.h"
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#define ATLAS_PADDING 1  /* Padding between glyphs to prevent bleeding */
#define MAX_SHELVES 256

/* Shelf for bin packing */
typedef struct {
    int y;          /* Y position of shelf */
    int height;     /* Height of shelf */
    int x_cursor;   /* Current X position for next allocation */
} Shelf;

struct TextureAtlas {
    GLuint texture_id;
    int size;               /* Width and height (always square) */

    /* Shelf-based packing */
    Shelf shelves[MAX_SHELVES];
    int shelf_count;
    int current_y;          /* Next Y position for new shelf */

    /* Statistics */
    int allocated_pixels;
};

TextureAtlas *texture_atlas_create(int initial_size) {
    if (initial_size <= 0) {
        initial_size = 1024;  /* Default size */
    }

    /* Round up to power of 2 */
    int size = 1;
    while (size < initial_size) {
        size *= 2;
    }

    TextureAtlas *atlas = calloc(1, sizeof(TextureAtlas));
    if (!atlas) return NULL;

    atlas->size = size;
    atlas->shelf_count = 0;
    atlas->current_y = 0;
    atlas->allocated_pixels = 0;

    /* Create OpenGL texture */
    glGenTextures(1, &atlas->texture_id);
    glBindTexture(GL_TEXTURE_2D, atlas->texture_id);

    /* Allocate empty texture (single channel for grayscale glyphs) */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);

    /* Set texture parameters */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Enable 1-byte alignment for texture uploads */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindTexture(GL_TEXTURE_2D, 0);

    return atlas;
}

void texture_atlas_destroy(TextureAtlas *atlas) {
    if (!atlas) return;

    if (atlas->texture_id) {
        glDeleteTextures(1, &atlas->texture_id);
    }

    free(atlas);
}

bool texture_atlas_allocate(TextureAtlas *atlas, int width, int height, AtlasRegion *out) {
    if (!atlas || !out || width <= 0 || height <= 0) {
        return false;
    }

    int padded_width = width + ATLAS_PADDING;
    int padded_height = height + ATLAS_PADDING;

    /* Try to find an existing shelf that fits */
    for (int i = 0; i < atlas->shelf_count; i++) {
        Shelf *shelf = &atlas->shelves[i];

        /* Check if glyph fits on this shelf */
        if (shelf->height >= padded_height &&
            shelf->x_cursor + padded_width <= atlas->size) {

            /* Allocate from this shelf */
            out->x = shelf->x_cursor;
            out->y = shelf->y;
            out->width = width;
            out->height = height;

            /* Calculate UV coordinates */
            float inv_size = 1.0f / (float)atlas->size;
            out->u0 = (float)out->x * inv_size;
            out->v0 = (float)out->y * inv_size;
            out->u1 = (float)(out->x + width) * inv_size;
            out->v1 = (float)(out->y + height) * inv_size;

            shelf->x_cursor += padded_width;
            atlas->allocated_pixels += width * height;

            return true;
        }
    }

    /* No existing shelf fits, create a new one */
    if (atlas->current_y + padded_height > atlas->size) {
        /* Atlas is full */
        return false;
    }

    if (atlas->shelf_count >= MAX_SHELVES) {
        return false;
    }

    /* Create new shelf */
    Shelf *new_shelf = &atlas->shelves[atlas->shelf_count++];
    new_shelf->y = atlas->current_y;
    new_shelf->height = padded_height;
    new_shelf->x_cursor = padded_width;

    atlas->current_y += padded_height;

    /* Allocate from new shelf */
    out->x = 0;
    out->y = new_shelf->y;
    out->width = width;
    out->height = height;

    float inv_size = 1.0f / (float)atlas->size;
    out->u0 = (float)out->x * inv_size;
    out->v0 = (float)out->y * inv_size;
    out->u1 = (float)(out->x + width) * inv_size;
    out->v1 = (float)(out->y + height) * inv_size;

    atlas->allocated_pixels += width * height;

    return true;
}

void texture_atlas_upload(TextureAtlas *atlas, const AtlasRegion *region, const uint8_t *bitmap) {
    if (!atlas || !region || !bitmap) return;

    glBindTexture(GL_TEXTURE_2D, atlas->texture_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    region->x, region->y,
                    region->width, region->height,
                    GL_RED, GL_UNSIGNED_BYTE, bitmap);

    glBindTexture(GL_TEXTURE_2D, 0);
}

uint32_t texture_atlas_get_texture(TextureAtlas *atlas) {
    return atlas ? atlas->texture_id : 0;
}

int texture_atlas_get_size(TextureAtlas *atlas) {
    return atlas ? atlas->size : 0;
}

void texture_atlas_clear(TextureAtlas *atlas) {
    if (!atlas) return;

    atlas->shelf_count = 0;
    atlas->current_y = 0;
    atlas->allocated_pixels = 0;

    /* Clear the texture to black */
    glBindTexture(GL_TEXTURE_2D, atlas->texture_id);

    uint8_t *zeros = calloc(1, atlas->size * atlas->size);
    if (zeros) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        atlas->size, atlas->size,
                        GL_RED, GL_UNSIGNED_BYTE, zeros);
        free(zeros);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

bool texture_atlas_is_full(TextureAtlas *atlas) {
    if (!atlas) return true;

    /* Consider full if more than 90% allocated or no vertical space left */
    int total_pixels = atlas->size * atlas->size;
    float usage = (float)atlas->allocated_pixels / (float)total_pixels;

    return usage > 0.9f || atlas->current_y >= atlas->size - 32;
}

bool texture_atlas_grow(TextureAtlas *atlas) {
    if (!atlas) return false;

    int new_size = atlas->size * 2;

    /* Cap at reasonable maximum (4096 or 8192) */
    if (new_size > 8192) {
        return false;
    }

    /* Delete old texture and create new one */
    glDeleteTextures(1, &atlas->texture_id);

    atlas->size = new_size;
    atlas->shelf_count = 0;
    atlas->current_y = 0;
    atlas->allocated_pixels = 0;

    glGenTextures(1, &atlas->texture_id);
    glBindTexture(GL_TEXTURE_2D, atlas->texture_id);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, new_size, new_size, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

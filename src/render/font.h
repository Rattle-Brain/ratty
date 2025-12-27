#ifndef RENDER_FONT_H
#define RENDER_FONT_H

#include <stdbool.h>
#include <stdint.h>

/* Font style indices (must match render.h) */
#define FONT_STYLE_REGULAR     0
#define FONT_STYLE_BOLD        1
#define FONT_STYLE_ITALIC      2
#define FONT_STYLE_BOLD_ITALIC 3
#define FONT_STYLE_COUNT       4

/* Font metrics */
typedef struct {
    int cell_width;           /* Advance width for monospace */
    int cell_height;          /* Line height (ascender + descender + gap) */
    int ascender;             /* Pixels above baseline */
    int descender;            /* Pixels below baseline (positive value) */
    int underline_position;   /* Offset from baseline */
    int underline_thickness;
    int strikethrough_position;
} FontMetrics;

/* Glyph bitmap returned from rasterization */
typedef struct {
    uint8_t *bitmap;          /* Grayscale 8-bit bitmap (caller must free) */
    int width;                /* Bitmap width */
    int height;               /* Bitmap height */
    int bearing_x;            /* Horizontal offset from origin */
    int bearing_y;            /* Vertical offset from baseline */
    int advance_x;            /* Horizontal advance */
    uint32_t glyph_index;     /* Font glyph index */
} GlyphBitmap;

/* Font manager state (opaque) */
typedef struct FontManager FontManager;

/* Lifecycle */
FontManager *font_manager_create(void);
void font_manager_destroy(FontManager *fm);

/* Font loading */
bool font_manager_load(FontManager *fm, const char *path, int style, int size_pt, int dpi);
bool font_manager_load_default(FontManager *fm, int size_pt, int dpi);

/* Set font size (affects all loaded faces) */
bool font_manager_set_size(FontManager *fm, int size_pt, int dpi);

/* Get metrics (based on regular style) */
FontMetrics font_manager_get_metrics(FontManager *fm);

/* Rasterize a single glyph by codepoint */
bool font_manager_rasterize_glyph(FontManager *fm, uint32_t codepoint, int style, GlyphBitmap *out);

/* Rasterize a single glyph by glyph index (for use with HarfBuzz) */
bool font_manager_rasterize_glyph_index(FontManager *fm, uint32_t glyph_index, int style, GlyphBitmap *out);

/* Get glyph index for a codepoint */
uint32_t font_manager_get_glyph_index(FontManager *fm, uint32_t codepoint, int style);

/* Get FreeType face (for HarfBuzz integration) - returns FT_Face */
void *font_manager_get_face(FontManager *fm, int style);

/* Check if a style is loaded */
bool font_manager_has_style(FontManager *fm, int style);

/* Free glyph bitmap data */
void glyph_bitmap_free(GlyphBitmap *glyph);

#endif /* RENDER_FONT_H */

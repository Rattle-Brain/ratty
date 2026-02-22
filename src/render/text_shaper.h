#ifndef RENDER_TEXT_SHAPER_H
#define RENDER_TEXT_SHAPER_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct FontManager FontManager;
typedef struct TextShaper TextShaper;

/* Shaped glyph info */
typedef struct {
    uint32_t glyph_index;   /* Font glyph index (not codepoint) */
    int x_offset;           /* Horizontal offset from pen position (26.6 fixed) */
    int y_offset;           /* Vertical offset (26.6 fixed) */
    int x_advance;          /* Advance after this glyph (26.6 fixed) */
    int y_advance;          /* Vertical advance (usually 0) */
    int cluster;            /* Character cluster index */
} ShapedGlyph;

/* Shaping result */
typedef struct {
    ShapedGlyph *glyphs;    /* Array of shaped glyphs */
    int count;              /* Number of glyphs */
    int style;              /* Font style used */
} ShapingResult;

/* Shaping options */
typedef struct {
    bool enable_ligatures;  /* Enable OpenType ligatures (default: true) */
    bool enable_kerning;    /* Enable kerning (default: true) */
    const char *language;   /* Language tag (NULL for default) */
    const char *script;     /* Script tag (NULL for auto-detect) */
} ShapingOptions;

/* Default shaping options */
#define SHAPING_OPTIONS_DEFAULT ((ShapingOptions){true, true, NULL, NULL})

/* Lifecycle */
TextShaper *text_shaper_create(FontManager *fm);
void text_shaper_destroy(TextShaper *shaper);

/* Rebuild shaper for a specific style (call after font change) */
bool text_shaper_rebuild(TextShaper *shaper, int style);

/* Shape a run of text with default options */
ShapingResult text_shaper_shape(TextShaper *shaper, const char *text, int byte_len, int style);

/* Shape a run of text with custom options */
ShapingResult text_shaper_shape_with_options(TextShaper *shaper, const char *text, int byte_len,
                                              int style, const ShapingOptions *options);

/* Free shaping result */
void shaping_result_free(ShapingResult *result);

/* Check if shaper is ready */
bool text_shaper_is_ready(TextShaper *shaper);

#endif /* RENDER_TEXT_SHAPER_H */

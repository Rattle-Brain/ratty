#include "text_shaper.h"
#include "font.h"
#include <stdlib.h>
#include <string.h>

#include <hb.h>
#include <hb-ft.h>

struct TextShaper {
    FontManager *font_manager;
    hb_font_t *hb_fonts[FONT_STYLE_COUNT];
    hb_buffer_t *buffer;  /* Reusable buffer */
};

TextShaper *text_shaper_create(FontManager *fm) {
    if (!fm) return NULL;

    TextShaper *shaper = calloc(1, sizeof(TextShaper));
    if (!shaper) return NULL;

    shaper->font_manager = fm;

    /* Create reusable buffer */
    shaper->buffer = hb_buffer_create();
    if (!hb_buffer_allocation_successful(shaper->buffer)) {
        free(shaper);
        return NULL;
    }

    /* Create HarfBuzz fonts for each loaded style */
    for (int style = 0; style < FONT_STYLE_COUNT; style++) {
        text_shaper_rebuild(shaper, style);
    }

    return shaper;
}

void text_shaper_destroy(TextShaper *shaper) {
    if (!shaper) return;

    for (int i = 0; i < FONT_STYLE_COUNT; i++) {
        if (shaper->hb_fonts[i]) {
            hb_font_destroy(shaper->hb_fonts[i]);
        }
    }

    if (shaper->buffer) {
        hb_buffer_destroy(shaper->buffer);
    }

    free(shaper);
}

bool text_shaper_rebuild(TextShaper *shaper, int style) {
    if (!shaper || style < 0 || style >= FONT_STYLE_COUNT) {
        return false;
    }

    /* Destroy existing HarfBuzz font */
    if (shaper->hb_fonts[style]) {
        hb_font_destroy(shaper->hb_fonts[style]);
        shaper->hb_fonts[style] = NULL;
    }

    /* Get FreeType face from font manager */
    FT_Face face = (FT_Face)font_manager_get_face(shaper->font_manager, style);
    if (!face) return false;

    /* Create HarfBuzz font from FreeType face */
    shaper->hb_fonts[style] = hb_ft_font_create_referenced(face);
    if (!shaper->hb_fonts[style]) {
        return false;
    }

    return true;
}

/* Get HarfBuzz font for style (with fallback) */
static hb_font_t *get_hb_font(TextShaper *shaper, int style) {
    if (style >= 0 && style < FONT_STYLE_COUNT && shaper->hb_fonts[style]) {
        return shaper->hb_fonts[style];
    }

    /* Fallback chain */
    if (style == FONT_STYLE_BOLD_ITALIC) {
        if (shaper->hb_fonts[FONT_STYLE_BOLD]) return shaper->hb_fonts[FONT_STYLE_BOLD];
        if (shaper->hb_fonts[FONT_STYLE_ITALIC]) return shaper->hb_fonts[FONT_STYLE_ITALIC];
    }

    return shaper->hb_fonts[FONT_STYLE_REGULAR];
}

ShapingResult text_shaper_shape(TextShaper *shaper, const char *text, int byte_len, int style) {
    ShapingOptions opts = SHAPING_OPTIONS_DEFAULT;
    return text_shaper_shape_with_options(shaper, text, byte_len, style, &opts);
}

ShapingResult text_shaper_shape_with_options(TextShaper *shaper, const char *text, int byte_len,
                                              int style, const ShapingOptions *options) {
    ShapingResult result = {0};

    if (!shaper || !text) {
        return result;
    }

    if (byte_len < 0) {
        byte_len = strlen(text);
    }

    if (byte_len == 0) {
        return result;
    }

    hb_font_t *font = get_hb_font(shaper, style);
    if (!font) {
        return result;
    }

    hb_buffer_t *buf = shaper->buffer;

    /* Reset and configure buffer */
    hb_buffer_reset(buf);
    hb_buffer_set_content_type(buf, HB_BUFFER_CONTENT_TYPE_UNICODE);

    /* Set direction (LTR for terminal) */
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);

    /* Set script and language if provided */
    if (options && options->script) {
        hb_buffer_set_script(buf, hb_script_from_string(options->script, -1));
    } else {
        hb_buffer_set_script(buf, HB_SCRIPT_COMMON);
    }

    if (options && options->language) {
        hb_buffer_set_language(buf, hb_language_from_string(options->language, -1));
    }

    /* Add text to buffer */
    hb_buffer_add_utf8(buf, text, byte_len, 0, byte_len);

    /* Set up features */
    hb_feature_t features[4];
    int num_features = 0;

    if (options) {
        if (options->enable_ligatures) {
            /* Enable common ligatures */
            features[num_features++] = (hb_feature_t){
                .tag = HB_TAG('l', 'i', 'g', 'a'),
                .value = 1,
                .start = 0,
                .end = (unsigned int)-1
            };
            features[num_features++] = (hb_feature_t){
                .tag = HB_TAG('c', 'l', 'i', 'g'),
                .value = 1,
                .start = 0,
                .end = (unsigned int)-1
            };
        }
        if (options->enable_kerning) {
            features[num_features++] = (hb_feature_t){
                .tag = HB_TAG('k', 'e', 'r', 'n'),
                .value = 1,
                .start = 0,
                .end = (unsigned int)-1
            };
        }
    }

    /* Shape the text */
    hb_shape(font, buf, num_features > 0 ? features : NULL, num_features);

    /* Extract glyph info */
    unsigned int glyph_count;
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
    hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

    if (glyph_count == 0) {
        return result;
    }

    /* Allocate result */
    result.glyphs = malloc(glyph_count * sizeof(ShapedGlyph));
    if (!result.glyphs) {
        return result;
    }

    result.count = glyph_count;
    result.style = style;

    /* Copy glyph data */
    for (unsigned int i = 0; i < glyph_count; i++) {
        result.glyphs[i].glyph_index = glyph_info[i].codepoint;  /* After shaping, this is glyph index */
        result.glyphs[i].cluster = glyph_info[i].cluster;
        result.glyphs[i].x_offset = glyph_pos[i].x_offset;
        result.glyphs[i].y_offset = glyph_pos[i].y_offset;
        result.glyphs[i].x_advance = glyph_pos[i].x_advance;
        result.glyphs[i].y_advance = glyph_pos[i].y_advance;
    }

    return result;
}

void shaping_result_free(ShapingResult *result) {
    if (result && result->glyphs) {
        free(result->glyphs);
        result->glyphs = NULL;
        result->count = 0;
    }
}

bool text_shaper_is_ready(TextShaper *shaper) {
    if (!shaper) return false;

    /* At least regular style should be available */
    return shaper->hb_fonts[FONT_STYLE_REGULAR] != NULL;
}

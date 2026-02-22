#include "font.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SYNTHESIS_H

#define DEFAULT_DPI 96

struct FontManager {
    FT_Library ft_library;
    FT_Face faces[FONT_STYLE_COUNT];
    bool face_loaded[FONT_STYLE_COUNT];

    int size_pt;
    int dpi;
    FontMetrics metrics;
    bool metrics_valid;
};

/* Helper to compute metrics from a face */
static void compute_metrics(FontManager *fm) {
    FT_Face face = fm->faces[FONT_STYLE_REGULAR];
    if (!face) {
        /* Try to find any loaded face */
        for (int i = 0; i < FONT_STYLE_COUNT; i++) {
            if (fm->faces[i]) {
                face = fm->faces[i];
                break;
            }
        }
    }

    if (!face) {
        fm->metrics_valid = false;
        return;
    }

    /* FreeType metrics are in 26.6 fixed point (divide by 64) */
    fm->metrics.ascender = face->size->metrics.ascender >> 6;
    fm->metrics.descender = -(face->size->metrics.descender >> 6);
    fm->metrics.cell_height = face->size->metrics.height >> 6;

    /* For monospace fonts, use the advance of 'M' or 'W' */
    FT_UInt glyph_index = FT_Get_Char_Index(face, 'M');
    if (glyph_index && FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT) == 0) {
        fm->metrics.cell_width = face->glyph->advance.x >> 6;
    } else {
        /* Fallback: estimate from max_advance */
        fm->metrics.cell_width = face->size->metrics.max_advance >> 6;
    }

    /* Underline/strikethrough positions */
    if (face->underline_position != 0) {
        fm->metrics.underline_position = fm->metrics.ascender - (face->underline_position >> 6);
        fm->metrics.underline_thickness = face->underline_thickness >> 6;
        if (fm->metrics.underline_thickness < 1) {
            fm->metrics.underline_thickness = 1;
        }
    } else {
        /* Defaults if not specified */
        fm->metrics.underline_position = fm->metrics.ascender + 2;
        fm->metrics.underline_thickness = 1;
    }

    fm->metrics.strikethrough_position = fm->metrics.ascender / 2;
    fm->metrics_valid = true;
}

FontManager *font_manager_create(void) {
    FontManager *fm = calloc(1, sizeof(FontManager));
    if (!fm) return NULL;

    if (FT_Init_FreeType(&fm->ft_library) != 0) {
        free(fm);
        return NULL;
    }

    fm->size_pt = 12;
    fm->dpi = DEFAULT_DPI;

    return fm;
}

void font_manager_destroy(FontManager *fm) {
    if (!fm) return;

    for (int i = 0; i < FONT_STYLE_COUNT; i++) {
        if (fm->faces[i]) {
            FT_Done_Face(fm->faces[i]);
        }
    }

    if (fm->ft_library) {
        FT_Done_FreeType(fm->ft_library);
    }

    free(fm);
}

bool font_manager_load(FontManager *fm, const char *path, int style, int size_pt, int dpi) {
    if (!fm || !path || style < 0 || style >= FONT_STYLE_COUNT) {
        return false;
    }

    /* Free existing face for this style */
    if (fm->faces[style]) {
        FT_Done_Face(fm->faces[style]);
        fm->faces[style] = NULL;
        fm->face_loaded[style] = false;
    }

    /* Load the font face */
    FT_Face face;
    if (FT_New_Face(fm->ft_library, path, 0, &face) != 0) {
        fprintf(stderr, "font_manager: Failed to load font: %s\n", path);
        return false;
    }

    /* Set character size */
    if (dpi <= 0) dpi = DEFAULT_DPI;
    if (size_pt <= 0) size_pt = 12;

    if (FT_Set_Char_Size(face, 0, size_pt * 64, dpi, dpi) != 0) {
        FT_Done_Face(face);
        fprintf(stderr, "font_manager: Failed to set char size\n");
        return false;
    }

    fm->faces[style] = face;
    fm->face_loaded[style] = true;
    fm->size_pt = size_pt;
    fm->dpi = dpi;

    /* Recompute metrics */
    compute_metrics(fm);

    return true;
}

bool font_manager_load_default(FontManager *fm, int size_pt, int dpi) {
    if (!fm) return false;

    /* Try common monospace fonts in order of preference */
    const char *font_paths[] = {
        /* macOS */
        "/System/Library/Fonts/Monaco.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/Library/Fonts/SF-Mono-Regular.otf",
        "/System/Library/Fonts/SFNSMono.ttf",
        /* Linux */
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        /* Fallback */
        "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf",
        NULL
    };

    for (int i = 0; font_paths[i] != NULL; i++) {
        if (font_manager_load(fm, font_paths[i], FONT_STYLE_REGULAR, size_pt, dpi)) {
            return true;
        }
    }

    fprintf(stderr, "font_manager: No default monospace font found\n");
    return false;
}

bool font_manager_set_size(FontManager *fm, int size_pt, int dpi) {
    if (!fm || size_pt <= 0) return false;
    if (dpi <= 0) dpi = fm->dpi;

    bool success = true;

    for (int i = 0; i < FONT_STYLE_COUNT; i++) {
        if (fm->faces[i]) {
            if (FT_Set_Char_Size(fm->faces[i], 0, size_pt * 64, dpi, dpi) != 0) {
                success = false;
            }
        }
    }

    if (success) {
        fm->size_pt = size_pt;
        fm->dpi = dpi;
        compute_metrics(fm);
    }

    return success;
}

FontMetrics font_manager_get_metrics(FontManager *fm) {
    if (!fm || !fm->metrics_valid) {
        FontMetrics empty = {0};
        return empty;
    }
    return fm->metrics;
}

/* Get the face to use for a style (falls back to regular) */
static FT_Face get_face_for_style(FontManager *fm, int style) {
    if (style >= 0 && style < FONT_STYLE_COUNT && fm->faces[style]) {
        return fm->faces[style];
    }

    /* Fallback chain */
    if (style == FONT_STYLE_BOLD_ITALIC) {
        if (fm->faces[FONT_STYLE_BOLD]) return fm->faces[FONT_STYLE_BOLD];
        if (fm->faces[FONT_STYLE_ITALIC]) return fm->faces[FONT_STYLE_ITALIC];
    }

    return fm->faces[FONT_STYLE_REGULAR];
}

bool font_manager_rasterize_glyph(FontManager *fm, uint32_t codepoint, int style, GlyphBitmap *out) {
    if (!fm || !out) return false;

    FT_Face face = get_face_for_style(fm, style);
    if (!face) return false;

    uint32_t glyph_index = FT_Get_Char_Index(face, codepoint);
    if (glyph_index == 0 && codepoint != 0) {
        /* Glyph not found - try regular face */
        face = fm->faces[FONT_STYLE_REGULAR];
        if (face) {
            glyph_index = FT_Get_Char_Index(face, codepoint);
        }
    }

    return font_manager_rasterize_glyph_index(fm, glyph_index, style, out);
}

bool font_manager_rasterize_glyph_index(FontManager *fm, uint32_t glyph_index, int style, GlyphBitmap *out) {
    if (!fm || !out) return false;

    FT_Face face = get_face_for_style(fm, style);
    if (!face) return false;

    memset(out, 0, sizeof(GlyphBitmap));

    /* Load the glyph */
    FT_Int32 load_flags = FT_LOAD_RENDER;

    /* Apply synthetic bold/italic if needed */
    bool need_synthetic_bold = (style == FONT_STYLE_BOLD || style == FONT_STYLE_BOLD_ITALIC) &&
                               !fm->faces[style];
    bool need_synthetic_italic = (style == FONT_STYLE_ITALIC || style == FONT_STYLE_BOLD_ITALIC) &&
                                 !fm->faces[style];

    if (FT_Load_Glyph(face, glyph_index, load_flags) != 0) {
        return false;
    }

    FT_GlyphSlot slot = face->glyph;

    /* Apply synthetic transformations */
    if (need_synthetic_bold) {
        FT_GlyphSlot_Embolden(slot);
    }
    if (need_synthetic_italic) {
        FT_GlyphSlot_Oblique(slot);
    }

    /* Render if not already rendered */
    if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
            return false;
        }
    }

    FT_Bitmap *bitmap = &slot->bitmap;

    out->width = bitmap->width;
    out->height = bitmap->rows;
    out->bearing_x = slot->bitmap_left;
    out->bearing_y = slot->bitmap_top;
    out->advance_x = slot->advance.x >> 6;
    out->glyph_index = glyph_index;

    /* Copy bitmap data */
    if (bitmap->width > 0 && bitmap->rows > 0) {
        size_t size = bitmap->width * bitmap->rows;
        out->bitmap = malloc(size);
        if (!out->bitmap) return false;

        /* Handle different pitch values */
        if (bitmap->pitch == (int)bitmap->width) {
            memcpy(out->bitmap, bitmap->buffer, size);
        } else {
            for (unsigned int row = 0; row < bitmap->rows; row++) {
                memcpy(out->bitmap + row * bitmap->width,
                       bitmap->buffer + row * bitmap->pitch,
                       bitmap->width);
            }
        }
    }

    return true;
}

uint32_t font_manager_get_glyph_index(FontManager *fm, uint32_t codepoint, int style) {
    if (!fm) return 0;

    FT_Face face = get_face_for_style(fm, style);
    if (!face) return 0;

    return FT_Get_Char_Index(face, codepoint);
}

void *font_manager_get_face(FontManager *fm, int style) {
    if (!fm) return NULL;
    return get_face_for_style(fm, style);
}

bool font_manager_has_style(FontManager *fm, int style) {
    if (!fm || style < 0 || style >= FONT_STYLE_COUNT) return false;
    return fm->face_loaded[style];
}

void glyph_bitmap_free(GlyphBitmap *glyph) {
    if (glyph && glyph->bitmap) {
        free(glyph->bitmap);
        glyph->bitmap = NULL;
    }
}

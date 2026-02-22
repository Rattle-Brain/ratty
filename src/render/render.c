#include "render.h"
#include "font.h"
#include "text_shaper.h"
#include "glyph_cache.h"
#include "gl_backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DEFAULT_ATLAS_SIZE 1024
#define DEFAULT_FONT_SIZE 14
#define DEFAULT_DPI 96

#define MAX_COMMANDS 4096
#define MAX_TEXT_VERTICES 65536
#define MAX_RECT_VERTICES 16384

struct Renderer {
    /* Subsystems */
    FontManager *font_manager;
    TextShaper *text_shaper;
    GlyphCache *glyph_cache;
    GLBackend *gl_backend;

    /* Metrics */
    FontMetrics metrics;

    /* Configuration */
    int font_size_pt;
    int dpi;

    /* Viewport */
    int viewport_width;
    int viewport_height;

    /* Command buffer */
    RenderCommand *commands;
    int command_count;
    int command_capacity;

    /* Vertex buffers */
    TextVertex *text_vertices;
    int text_vertex_count;
    int text_vertex_capacity;

    RectVertex *rect_vertices;
    int rect_vertex_count;
    int rect_vertex_capacity;
};

/* Helper to add a text quad (2 triangles = 6 vertices) */
static void add_text_quad(Renderer *r, float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          float cr, float cg, float cb, float ca) {
    if (r->text_vertex_count + 6 > r->text_vertex_capacity) return;

    TextVertex *v = &r->text_vertices[r->text_vertex_count];

    /* Triangle 1 */
    v[0] = (TextVertex){x, y, u0, v0, cr, cg, cb, ca};
    v[1] = (TextVertex){x + w, y, u1, v0, cr, cg, cb, ca};
    v[2] = (TextVertex){x + w, y + h, u1, v1, cr, cg, cb, ca};

    /* Triangle 2 */
    v[3] = (TextVertex){x, y, u0, v0, cr, cg, cb, ca};
    v[4] = (TextVertex){x + w, y + h, u1, v1, cr, cg, cb, ca};
    v[5] = (TextVertex){x, y + h, u0, v1, cr, cg, cb, ca};

    r->text_vertex_count += 6;
}

/* Helper to add a rect quad */
static void add_rect_quad(Renderer *r, float x, float y, float w, float h,
                          float cr, float cg, float cb, float ca) {
    if (r->rect_vertex_count + 6 > r->rect_vertex_capacity) return;

    RectVertex *v = &r->rect_vertices[r->rect_vertex_count];

    /* Triangle 1 */
    v[0] = (RectVertex){x, y, cr, cg, cb, ca};
    v[1] = (RectVertex){x + w, y, cr, cg, cb, ca};
    v[2] = (RectVertex){x + w, y + h, cr, cg, cb, ca};

    /* Triangle 2 */
    v[3] = (RectVertex){x, y, cr, cg, cb, ca};
    v[4] = (RectVertex){x + w, y + h, cr, cg, cb, ca};
    v[5] = (RectVertex){x, y + h, cr, cg, cb, ca};

    r->rect_vertex_count += 6;
}

/* Unpack color to floats */
static void unpack_color_float(uint32_t color, float *r, float *g, float *b, float *a) {
    *r = ((color >> 24) & 0xFF) / 255.0f;
    *g = ((color >> 16) & 0xFF) / 255.0f;
    *b = ((color >> 8) & 0xFF) / 255.0f;
    *a = (color & 0xFF) / 255.0f;
}

Renderer *renderer_create(const RenderConfig *config) {
    Renderer *r = calloc(1, sizeof(Renderer));
    if (!r) return NULL;

    r->font_size_pt = config && config->font_size_pt > 0 ? config->font_size_pt : DEFAULT_FONT_SIZE;
    r->dpi = config && config->dpi > 0 ? config->dpi : DEFAULT_DPI;

    /* Create font manager */
    r->font_manager = font_manager_create();
    if (!r->font_manager) {
        fprintf(stderr, "renderer: Failed to create font manager\n");
        renderer_destroy(r);
        return NULL;
    }

    /* Load fonts */
    bool font_loaded = false;
    if (config && config->font_path) {
        font_loaded = font_manager_load(r->font_manager, config->font_path,
                                         FONT_STYLE_REGULAR, r->font_size_pt, r->dpi);
    }
    if (!font_loaded) {
        font_loaded = font_manager_load_default(r->font_manager, r->font_size_pt, r->dpi);
    }
    if (!font_loaded) {
        fprintf(stderr, "renderer: Failed to load any font\n");
        renderer_destroy(r);
        return NULL;
    }

    /* Load variant fonts if provided */
    if (config) {
        if (config->font_path_bold) {
            font_manager_load(r->font_manager, config->font_path_bold,
                              FONT_STYLE_BOLD, r->font_size_pt, r->dpi);
        }
        if (config->font_path_italic) {
            font_manager_load(r->font_manager, config->font_path_italic,
                              FONT_STYLE_ITALIC, r->font_size_pt, r->dpi);
        }
        if (config->font_path_bold_italic) {
            font_manager_load(r->font_manager, config->font_path_bold_italic,
                              FONT_STYLE_BOLD_ITALIC, r->font_size_pt, r->dpi);
        }
    }

    r->metrics = font_manager_get_metrics(r->font_manager);

    /* Create text shaper */
    r->text_shaper = text_shaper_create(r->font_manager);
    if (!r->text_shaper) {
        fprintf(stderr, "renderer: Failed to create text shaper\n");
        renderer_destroy(r);
        return NULL;
    }

    /* Create glyph cache */
    int atlas_size = config && config->atlas_size > 0 ? config->atlas_size : DEFAULT_ATLAS_SIZE;
    r->glyph_cache = glyph_cache_create(r->font_manager, atlas_size);
    if (!r->glyph_cache) {
        fprintf(stderr, "renderer: Failed to create glyph cache\n");
        renderer_destroy(r);
        return NULL;
    }

    /* Create GL backend */
    r->gl_backend = gl_backend_create();
    if (!r->gl_backend) {
        fprintf(stderr, "renderer: Failed to create GL backend\n");
        renderer_destroy(r);
        return NULL;
    }

    /* Allocate command buffer */
    r->command_capacity = MAX_COMMANDS;
    r->commands = malloc(r->command_capacity * sizeof(RenderCommand));
    if (!r->commands) {
        renderer_destroy(r);
        return NULL;
    }

    /* Allocate vertex buffers */
    r->text_vertex_capacity = MAX_TEXT_VERTICES;
    r->text_vertices = malloc(r->text_vertex_capacity * sizeof(TextVertex));
    if (!r->text_vertices) {
        renderer_destroy(r);
        return NULL;
    }

    r->rect_vertex_capacity = MAX_RECT_VERTICES;
    r->rect_vertices = malloc(r->rect_vertex_capacity * sizeof(RectVertex));
    if (!r->rect_vertices) {
        renderer_destroy(r);
        return NULL;
    }

    return r;
}

void renderer_destroy(Renderer *r) {
    if (!r) return;

    if (r->gl_backend) gl_backend_destroy(r->gl_backend);
    if (r->glyph_cache) glyph_cache_destroy(r->glyph_cache);
    if (r->text_shaper) text_shaper_destroy(r->text_shaper);
    if (r->font_manager) font_manager_destroy(r->font_manager);

    free(r->commands);
    free(r->text_vertices);
    free(r->rect_vertices);
    free(r);
}

bool renderer_load_font(Renderer *r, const char *path, int style) {
    if (!r || !path) return false;

    bool ok = font_manager_load(r->font_manager, path, style, r->font_size_pt, r->dpi);
    if (ok) {
        r->metrics = font_manager_get_metrics(r->font_manager);
        glyph_cache_clear(r->glyph_cache);
        text_shaper_rebuild(r->text_shaper, style);
    }
    return ok;
}

void renderer_set_font_size(Renderer *r, int size_pt) {
    if (!r || size_pt <= 0) return;

    r->font_size_pt = size_pt;
    font_manager_set_size(r->font_manager, size_pt, r->dpi);
    r->metrics = font_manager_get_metrics(r->font_manager);
    glyph_cache_clear(r->glyph_cache);

    /* Rebuild all shaper fonts */
    for (int i = 0; i < FONT_STYLE_COUNT; i++) {
        text_shaper_rebuild(r->text_shaper, i);
    }
}

void renderer_begin_frame(Renderer *r, int window_width, int window_height) {
    if (!r) return;

    r->viewport_width = window_width;
    r->viewport_height = window_height;
    r->command_count = 0;
    r->text_vertex_count = 0;
    r->rect_vertex_count = 0;

    gl_backend_set_viewport(r->gl_backend, window_width, window_height);
    gl_backend_begin_frame(r->gl_backend, 0.0f, 0.0f, 0.0f, 1.0f);
}

void renderer_submit(Renderer *r, const RenderCommand *cmd) {
    if (!r || !cmd || r->command_count >= r->command_capacity) return;
    r->commands[r->command_count++] = *cmd;
}

/* Process a single command */
static void process_command(Renderer *r, const RenderCommand *cmd) {
    switch (cmd->type) {
        case RENDER_CMD_CLEAR: {
            float cr, cg, cb, ca;
            unpack_color_float(cmd->data.clear.color, &cr, &cg, &cb, &ca);
            add_rect_quad(r,
                         (float)cmd->data.clear.region.x,
                         (float)cmd->data.clear.region.y,
                         (float)cmd->data.clear.region.width,
                         (float)cmd->data.clear.region.height,
                         cr, cg, cb, ca);
            break;
        }

        case RENDER_CMD_RECT: {
            float cr, cg, cb, ca;
            unpack_color_float(cmd->data.rect.color, &cr, &cg, &cb, &ca);
            if (cmd->data.rect.border_width <= 0) {
                /* Filled rectangle */
                add_rect_quad(r,
                             (float)cmd->data.rect.rect.x,
                             (float)cmd->data.rect.rect.y,
                             (float)cmd->data.rect.rect.width,
                             (float)cmd->data.rect.rect.height,
                             cr, cg, cb, ca);
            } else {
                /* Border only - draw 4 rectangles */
                int bw = cmd->data.rect.border_width;
                int x = cmd->data.rect.rect.x;
                int y = cmd->data.rect.rect.y;
                int w = cmd->data.rect.rect.width;
                int h = cmd->data.rect.rect.height;

                /* Top */
                add_rect_quad(r, (float)x, (float)y, (float)w, (float)bw, cr, cg, cb, ca);
                /* Bottom */
                add_rect_quad(r, (float)x, (float)(y + h - bw), (float)w, (float)bw, cr, cg, cb, ca);
                /* Left */
                add_rect_quad(r, (float)x, (float)(y + bw), (float)bw, (float)(h - 2 * bw), cr, cg, cb, ca);
                /* Right */
                add_rect_quad(r, (float)(x + w - bw), (float)(y + bw), (float)bw, (float)(h - 2 * bw), cr, cg, cb, ca);
            }
            break;
        }

        case RENDER_CMD_TEXT_GRID: {
            const RenderGridCmd *grid = &cmd->data.grid;
            if (!grid->cells || grid->cols <= 0 || grid->rows <= 0) break;

            int cell_w = r->metrics.cell_width;
            int cell_h = r->metrics.cell_height;
            int ascender = r->metrics.ascender;

            for (int row = 0; row < grid->rows; row++) {
                for (int col = 0; col < grid->cols; col++) {
                    const RenderCell *cell = &grid->cells[row * grid->cols + col];

                    int x = grid->region.x + col * cell_w;
                    int y = grid->region.y + row * cell_h;

                    /* Draw background */
                    uint8_t bg_a = cell->bg_color & 0xFF;
                    if (bg_a > 0) {
                        float br, bg, bb, ba;
                        unpack_color_float(cell->bg_color, &br, &bg, &bb, &ba);
                        add_rect_quad(r, (float)x, (float)y, (float)cell_w, (float)cell_h,
                                     br, bg, bb, ba);
                    }

                    /* Draw glyph */
                    if (cell->codepoint != ' ' && cell->codepoint != 0) {
                        int style = FONT_STYLE_REGULAR;
                        if (cell->flags & ATTR_BOLD) style |= 1;
                        if (cell->flags & ATTR_ITALIC) style |= 2;

                        const CachedGlyph *g = glyph_cache_get_codepoint(r->glyph_cache,
                                                                          cell->codepoint, style);
                        if (g && g->valid && g->region.width > 0) {
                            float fr, fg, fb, fa;
                            unpack_color_float(cell->fg_color, &fr, &fg, &fb, &fa);

                            float gx = (float)(x + g->bearing_x);
                            float gy = (float)(y + ascender - g->bearing_y);

                            add_text_quad(r, gx, gy,
                                         (float)g->region.width, (float)g->region.height,
                                         g->region.u0, g->region.v0,
                                         g->region.u1, g->region.v1,
                                         fr, fg, fb, fa);
                        }
                    }

                    /* Draw underline */
                    if (cell->flags & ATTR_UNDERLINE) {
                        float fr, fg, fb, fa;
                        unpack_color_float(cell->fg_color, &fr, &fg, &fb, &fa);
                        add_rect_quad(r, (float)x,
                                     (float)(y + r->metrics.underline_position),
                                     (float)cell_w,
                                     (float)r->metrics.underline_thickness,
                                     fr, fg, fb, fa);
                    }

                    /* Draw strikethrough */
                    if (cell->flags & ATTR_STRIKETHROUGH) {
                        float fr, fg, fb, fa;
                        unpack_color_float(cell->fg_color, &fr, &fg, &fb, &fa);
                        add_rect_quad(r, (float)x,
                                     (float)(y + r->metrics.strikethrough_position),
                                     (float)cell_w,
                                     (float)r->metrics.underline_thickness,
                                     fr, fg, fb, fa);
                    }
                }
            }
            break;
        }

        case RENDER_CMD_TEXT_LINE: {
            const RenderTextLineCmd *text = &cmd->data.text_line;
            if (!text->text) break;

            int len = text->text_len >= 0 ? text->text_len : (int)strlen(text->text);
            if (len == 0) break;

            int style = FONT_STYLE_REGULAR;
            if (text->flags & ATTR_BOLD) style |= 1;
            if (text->flags & ATTR_ITALIC) style |= 2;

            /* Use text shaper for proper ligature support */
            ShapingResult shaped = text_shaper_shape(r->text_shaper, text->text, len, style);

            float fr, fg, fb, fa;
            unpack_color_float(text->fg_color, &fr, &fg, &fb, &fa);

            int pen_x = text->x;
            int pen_y = text->y;

            for (int i = 0; i < shaped.count; i++) {
                const ShapedGlyph *sg = &shaped.glyphs[i];

                const CachedGlyph *g = glyph_cache_get(r->glyph_cache, sg->glyph_index, style);
                if (g && g->valid && g->region.width > 0) {
                    /* HarfBuzz positions are in 26.6 fixed point */
                    float gx = (float)pen_x + (float)sg->x_offset / 64.0f + (float)g->bearing_x;
                    float gy = (float)pen_y - (float)sg->y_offset / 64.0f - (float)g->bearing_y;

                    add_text_quad(r, gx, gy,
                                 (float)g->region.width, (float)g->region.height,
                                 g->region.u0, g->region.v0,
                                 g->region.u1, g->region.v1,
                                 fr, fg, fb, fa);
                }

                pen_x += sg->x_advance >> 6;
                pen_y += sg->y_advance >> 6;
            }

            shaping_result_free(&shaped);
            break;
        }

        case RENDER_CMD_CURSOR: {
            const RenderCursorCmd *cursor = &cmd->data.cursor;
            if (!cursor->visible) break;

            float cr, cg, cb, ca;
            unpack_color_float(cursor->color, &cr, &cg, &cb, &ca);

            int cell_w = r->metrics.cell_width;
            int cell_h = r->metrics.cell_height;
            int x = cursor->x * cell_w;
            int y = cursor->y * cell_h;

            switch (cursor->style) {
                case CURSOR_BLOCK:
                    add_rect_quad(r, (float)x, (float)y, (float)cell_w, (float)cell_h,
                                 cr, cg, cb, ca);
                    break;
                case CURSOR_UNDERLINE:
                    add_rect_quad(r, (float)x, (float)(y + cell_h - 2), (float)cell_w, 2.0f,
                                 cr, cg, cb, ca);
                    break;
                case CURSOR_BAR:
                    add_rect_quad(r, (float)x, (float)y, 2.0f, (float)cell_h,
                                 cr, cg, cb, ca);
                    break;
            }
            break;
        }
    }
}

void renderer_end_frame(Renderer *r) {
    if (!r) return;

    /* Process all commands */
    for (int i = 0; i < r->command_count; i++) {
        process_command(r, &r->commands[i]);
    }

    /* Flush rect vertices first (backgrounds) */
    if (r->rect_vertex_count > 0) {
        gl_backend_draw_rect_quads(r->gl_backend, r->rect_vertices, r->rect_vertex_count);
    }

    /* Flush text vertices (foreground) */
    if (r->text_vertex_count > 0) {
        uint32_t texture = glyph_cache_get_texture(r->glyph_cache);
        gl_backend_draw_text_quads(r->gl_backend, r->text_vertices, r->text_vertex_count, texture);
    }

    gl_backend_end_frame(r->gl_backend);
}

FontMetrics renderer_get_metrics(Renderer *r) {
    if (!r) {
        FontMetrics empty = {0};
        return empty;
    }
    return r->metrics;
}

int renderer_get_cell_width(Renderer *r) {
    return r ? r->metrics.cell_width : 0;
}

int renderer_get_cell_height(Renderer *r) {
    return r ? r->metrics.cell_height : 0;
}

void renderer_viewport_resize(Renderer *r, int width, int height) {
    if (!r) return;
    r->viewport_width = width;
    r->viewport_height = height;
    gl_backend_set_viewport(r->gl_backend, width, height);
}

void renderer_clear(Renderer *r, uint32_t color) {
    if (!r) return;

    RenderCommand cmd = {
        .type = RENDER_CMD_CLEAR,
        .data.clear = {
            .region = {0, 0, r->viewport_width, r->viewport_height},
            .color = color
        }
    };
    renderer_submit(r, &cmd);
}

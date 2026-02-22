#ifndef RENDER_H
#define RENDER_H

#include "render_types.h"
#include "font.h"
#include <stdbool.h>

/* Renderer configuration */
typedef struct {
    const char *font_path;            /* Primary font path */
    const char *font_path_bold;       /* Bold variant (NULL for synthetic) */
    const char *font_path_italic;     /* Italic variant (NULL for synthetic) */
    const char *font_path_bold_italic;/* Bold-italic variant (NULL for synthetic) */
    int font_size_pt;                 /* Font size in points */
    int dpi;                          /* Screen DPI (0 for auto-detect) */
    int atlas_size;                   /* Initial texture atlas size (0 for default) */
} RenderConfig;

/* Lifecycle */
Renderer *renderer_create(const RenderConfig *config);
void renderer_destroy(Renderer *renderer);

/* Font configuration */
bool renderer_load_font(Renderer *renderer, const char *path, int style);
void renderer_set_font_size(Renderer *renderer, int size_pt);

/* Frame rendering */
void renderer_begin_frame(Renderer *renderer, int window_width, int window_height);
void renderer_submit(Renderer *renderer, const RenderCommand *cmd);
void renderer_end_frame(Renderer *renderer);

/* Metrics (for layout calculations) */
FontMetrics renderer_get_metrics(Renderer *renderer);
int renderer_get_cell_width(Renderer *renderer);
int renderer_get_cell_height(Renderer *renderer);

/* Viewport */
void renderer_viewport_resize(Renderer *renderer, int width, int height);

/* Utility */
void renderer_clear(Renderer *renderer, uint32_t color);

#endif /* RENDER_H */

#ifndef RENDER_GL_BACKEND_H
#define RENDER_GL_BACKEND_H

#include <stdint.h>
#include <stdbool.h>

/* OpenGL backend state (opaque) */
typedef struct GLBackend GLBackend;

/* Vertex for textured quad (text rendering) */
typedef struct {
    float x, y;         /* Position */
    float u, v;         /* Texture coordinates */
    float r, g, b, a;   /* Color */
} TextVertex;

/* Vertex for colored quad (rectangles) */
typedef struct {
    float x, y;         /* Position */
    float r, g, b, a;   /* Color */
} RectVertex;

/* Lifecycle */
GLBackend *gl_backend_create(void);
void gl_backend_destroy(GLBackend *backend);

/* Viewport */
void gl_backend_set_viewport(GLBackend *backend, int width, int height);

/* Begin/end frame */
void gl_backend_begin_frame(GLBackend *backend, float r, float g, float b, float a);
void gl_backend_end_frame(GLBackend *backend);

/* Draw textured quads (for text) */
void gl_backend_draw_text_quads(GLBackend *backend, const TextVertex *vertices,
                                 int vertex_count, uint32_t texture_id);

/* Draw colored quads (for rectangles) */
void gl_backend_draw_rect_quads(GLBackend *backend, const RectVertex *vertices, int vertex_count);

/* Shader access (for advanced usage) */
uint32_t gl_backend_get_text_shader(GLBackend *backend);
uint32_t gl_backend_get_rect_shader(GLBackend *backend);

/* Check for GL errors (debug) */
bool gl_backend_check_error(const char *context);

#endif /* RENDER_GL_BACKEND_H */

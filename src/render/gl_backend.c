#include "gl_backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

/* Embedded shaders (compiled into binary) */
static const char *TEXT_VERT_SHADER =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "out vec2 v_texcoord;\n"
    "out vec4 v_color;\n"
    "uniform mat4 u_projection;\n"
    "void main() {\n"
    "    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);\n"
    "    v_texcoord = a_texcoord;\n"
    "    v_color = a_color;\n"
    "}\n";

static const char *TEXT_FRAG_SHADER =
    "#version 330 core\n"
    "in vec2 v_texcoord;\n"
    "in vec4 v_color;\n"
    "out vec4 frag_color;\n"
    "uniform sampler2D u_texture;\n"
    "void main() {\n"
    "    float alpha = texture(u_texture, v_texcoord).r;\n"
    "    frag_color = vec4(v_color.rgb, v_color.a * alpha);\n"
    "}\n";

static const char *RECT_VERT_SHADER =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_position;\n"
    "layout(location = 1) in vec4 a_color;\n"
    "out vec4 v_color;\n"
    "uniform mat4 u_projection;\n"
    "void main() {\n"
    "    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "}\n";

static const char *RECT_FRAG_SHADER =
    "#version 330 core\n"
    "in vec4 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = v_color;\n"
    "}\n";

#define MAX_TEXT_VERTICES 65536
#define MAX_RECT_VERTICES 16384

struct GLBackend {
    /* Shaders */
    GLuint text_shader;
    GLuint rect_shader;

    /* Uniform locations */
    GLint text_proj_loc;
    GLint text_tex_loc;
    GLint rect_proj_loc;

    /* VAO/VBO for text */
    GLuint text_vao;
    GLuint text_vbo;

    /* VAO/VBO for rectangles */
    GLuint rect_vao;
    GLuint rect_vbo;

    /* Projection matrix */
    float projection[16];
    int viewport_width;
    int viewport_height;
};

/* Compile a shader */
static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "gl_backend: Shader compilation failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

/* Link a shader program */
static GLuint link_program(GLuint vert, GLuint frag) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "gl_backend: Program linking failed: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

/* Create orthographic projection matrix */
static void ortho_projection(float *matrix, float left, float right,
                              float bottom, float top, float near, float far) {
    memset(matrix, 0, 16 * sizeof(float));
    matrix[0] = 2.0f / (right - left);
    matrix[5] = 2.0f / (top - bottom);
    matrix[10] = -2.0f / (far - near);
    matrix[12] = -(right + left) / (right - left);
    matrix[13] = -(top + bottom) / (top - bottom);
    matrix[14] = -(far + near) / (far - near);
    matrix[15] = 1.0f;
}

GLBackend *gl_backend_create(void) {
    GLBackend *backend = calloc(1, sizeof(GLBackend));
    if (!backend) return NULL;

    /* Compile text shader */
    GLuint text_vert = compile_shader(GL_VERTEX_SHADER, TEXT_VERT_SHADER);
    GLuint text_frag = compile_shader(GL_FRAGMENT_SHADER, TEXT_FRAG_SHADER);
    if (!text_vert || !text_frag) {
        if (text_vert) glDeleteShader(text_vert);
        if (text_frag) glDeleteShader(text_frag);
        free(backend);
        return NULL;
    }

    backend->text_shader = link_program(text_vert, text_frag);
    glDeleteShader(text_vert);
    glDeleteShader(text_frag);

    if (!backend->text_shader) {
        free(backend);
        return NULL;
    }

    backend->text_proj_loc = glGetUniformLocation(backend->text_shader, "u_projection");
    backend->text_tex_loc = glGetUniformLocation(backend->text_shader, "u_texture");

    /* Compile rect shader */
    GLuint rect_vert = compile_shader(GL_VERTEX_SHADER, RECT_VERT_SHADER);
    GLuint rect_frag = compile_shader(GL_FRAGMENT_SHADER, RECT_FRAG_SHADER);
    if (!rect_vert || !rect_frag) {
        if (rect_vert) glDeleteShader(rect_vert);
        if (rect_frag) glDeleteShader(rect_frag);
        glDeleteProgram(backend->text_shader);
        free(backend);
        return NULL;
    }

    backend->rect_shader = link_program(rect_vert, rect_frag);
    glDeleteShader(rect_vert);
    glDeleteShader(rect_frag);

    if (!backend->rect_shader) {
        glDeleteProgram(backend->text_shader);
        free(backend);
        return NULL;
    }

    backend->rect_proj_loc = glGetUniformLocation(backend->rect_shader, "u_projection");

    /* Create text VAO/VBO */
    glGenVertexArrays(1, &backend->text_vao);
    glGenBuffers(1, &backend->text_vbo);

    glBindVertexArray(backend->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, backend->text_vbo);
    glBufferData(GL_ARRAY_BUFFER, MAX_TEXT_VERTICES * sizeof(TextVertex), NULL, GL_DYNAMIC_DRAW);

    /* Position (location 0) */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)offsetof(TextVertex, x));

    /* Texcoord (location 1) */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)offsetof(TextVertex, u));

    /* Color (location 2) */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)offsetof(TextVertex, r));

    /* Create rect VAO/VBO */
    glGenVertexArrays(1, &backend->rect_vao);
    glGenBuffers(1, &backend->rect_vbo);

    glBindVertexArray(backend->rect_vao);
    glBindBuffer(GL_ARRAY_BUFFER, backend->rect_vbo);
    glBufferData(GL_ARRAY_BUFFER, MAX_RECT_VERTICES * sizeof(RectVertex), NULL, GL_DYNAMIC_DRAW);

    /* Position (location 0) */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(RectVertex), (void*)offsetof(RectVertex, x));

    /* Color (location 1) */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(RectVertex), (void*)offsetof(RectVertex, r));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return backend;
}

void gl_backend_destroy(GLBackend *backend) {
    if (!backend) return;

    if (backend->text_shader) glDeleteProgram(backend->text_shader);
    if (backend->rect_shader) glDeleteProgram(backend->rect_shader);
    if (backend->text_vao) glDeleteVertexArrays(1, &backend->text_vao);
    if (backend->text_vbo) glDeleteBuffers(1, &backend->text_vbo);
    if (backend->rect_vao) glDeleteVertexArrays(1, &backend->rect_vao);
    if (backend->rect_vbo) glDeleteBuffers(1, &backend->rect_vbo);

    free(backend);
}

void gl_backend_set_viewport(GLBackend *backend, int width, int height) {
    if (!backend) return;

    backend->viewport_width = width;
    backend->viewport_height = height;

    glViewport(0, 0, width, height);

    /* Update projection matrix (origin at top-left) */
    ortho_projection(backend->projection, 0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);
}

void gl_backend_begin_frame(GLBackend *backend, float r, float g, float b, float a) {
    if (!backend) return;

    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Enable blending for text */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Disable depth test (2D rendering) */
    glDisable(GL_DEPTH_TEST);
}

void gl_backend_end_frame(GLBackend *backend) {
    (void)backend;
    /* Buffer swap is handled by GLFW */
}

void gl_backend_draw_text_quads(GLBackend *backend, const TextVertex *vertices,
                                 int vertex_count, uint32_t texture_id) {
    if (!backend || !vertices || vertex_count <= 0) return;

    if (vertex_count > MAX_TEXT_VERTICES) {
        vertex_count = MAX_TEXT_VERTICES;
    }

    glUseProgram(backend->text_shader);
    glUniformMatrix4fv(backend->text_proj_loc, 1, GL_FALSE, backend->projection);
    glUniform1i(backend->text_tex_loc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glBindVertexArray(backend->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, backend->text_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count * sizeof(TextVertex), vertices);

    glDrawArrays(GL_TRIANGLES, 0, vertex_count);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void gl_backend_draw_rect_quads(GLBackend *backend, const RectVertex *vertices, int vertex_count) {
    if (!backend || !vertices || vertex_count <= 0) return;

    if (vertex_count > MAX_RECT_VERTICES) {
        vertex_count = MAX_RECT_VERTICES;
    }

    glUseProgram(backend->rect_shader);
    glUniformMatrix4fv(backend->rect_proj_loc, 1, GL_FALSE, backend->projection);

    glBindVertexArray(backend->rect_vao);
    glBindBuffer(GL_ARRAY_BUFFER, backend->rect_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count * sizeof(RectVertex), vertices);

    glDrawArrays(GL_TRIANGLES, 0, vertex_count);

    glBindVertexArray(0);
    glUseProgram(0);
}

uint32_t gl_backend_get_text_shader(GLBackend *backend) {
    return backend ? backend->text_shader : 0;
}

uint32_t gl_backend_get_rect_shader(GLBackend *backend) {
    return backend ? backend->rect_shader : 0;
}

bool gl_backend_check_error(const char *context) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        const char *msg;
        switch (err) {
            case GL_INVALID_ENUM: msg = "INVALID_ENUM"; break;
            case GL_INVALID_VALUE: msg = "INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: msg = "INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY: msg = "OUT_OF_MEMORY"; break;
            default: msg = "UNKNOWN"; break;
        }
        fprintf(stderr, "gl_backend: OpenGL error at %s: %s\n", context, msg);
        return false;
    }
    return true;
}

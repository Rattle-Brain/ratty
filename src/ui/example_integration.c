/*
 * Example Integration
 *
 * This file demonstrates how to integrate the UI system with GLFW.
 * Use this as a template for your main.c
 */

#define GL_SILENCE_DEPRECATION

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

#include "window.h"
#include "keybindings.h"
#include "../config/config.h"
#include "../render/render.h"

/* Global state */
Window *ui_window = NULL;
Renderer *renderer = NULL;

/* GLFW callbacks */
static void key_callback(GLFWwindow *glfw_window, int key, int scancode, int action, int mods) {
    if (!ui_window) return;

    /* Convert GLFW event to the KeyEvent */
    KeyEvent event = {
        .key = key,
        .scancode = scancode,
        .action = (action == GLFW_PRESS) ? KEY_PRESS :
                  (action == GLFW_RELEASE) ? KEY_RELEASE : KEY_REPEAT,
        .mods = mods
    };

    /* Handle keybinding */
    bool handled = keybinding_handle(ui_window, &event);

    if (!handled) {
        /* No keybinding matched - forward to active terminal */
        Tab *active_tab = window_get_active_tab(ui_window);
        if (active_tab) {
            Split *focused = tab_get_focused_split(active_tab);
            if (focused && focused->pty) {
                /* TODO: Send key to PTY */
                /* pty_handle_key(focused->pty, &event); */
            }
        }
    }
}

static void framebuffer_size_callback(GLFWwindow *glfw_window, int width, int height) {
    (void)glfw_window;

    if (renderer) {
        renderer_viewport_resize(renderer, width, height);
    }

    if (ui_window) {
        window_resize(ui_window, width, height);
    }
}

static void mouse_button_callback(GLFWwindow *glfw_window, int button, int action, int mods) {
    if (!ui_window) return;

    double x, y;
    glfwGetCursorPos(glfw_window, &x, &y);

    MouseEvent event = {
        .action = (action == GLFW_PRESS) ? MOUSE_PRESS : MOUSE_RELEASE,
        .button = button,
        .x = x,
        .y = y,
        .mods = mods
    };

    window_handle_mouse(ui_window, &event);
}

static void cursor_pos_callback(GLFWwindow *glfw_window, double x, double y) {
    if (!ui_window) return;

    MouseEvent event = {
        .action = MOUSE_MOVE,
        .x = x,
        .y = y
    };

    window_handle_mouse(ui_window, &event);
}

static void scroll_callback(GLFWwindow *glfw_window, double xoffset, double yoffset) {
    if (!ui_window) return;

    double x, y;
    glfwGetCursorPos(glfw_window, &x, &y);

    MouseEvent event = {
        .action = MOUSE_SCROLL,
        .x = x,
        .y = y,
        .scroll_x = xoffset,
        .scroll_y = yoffset
    };

    window_handle_mouse(ui_window, &event);
}

int main(void) {
    /* Initialize GLFW */
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return EXIT_FAILURE;
    }

    /* Request OpenGL 3.3 Core Profile */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    /* Create GLFW window */
    GLFWwindow *glfw_window = glfwCreateWindow(800, 600, "Ratty Terminal", NULL, NULL);
    if (!glfw_window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(glfw_window);
    glfwSwapInterval(1);  /* Enable vsync */

    /* Set up GLFW callbacks */
    glfwSetKeyCallback(glfw_window, key_callback);
    glfwSetFramebufferSizeCallback(glfw_window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(glfw_window, mouse_button_callback);
    glfwSetCursorPosCallback(glfw_window, cursor_pos_callback);
    glfwSetScrollCallback(glfw_window, scroll_callback);

    /* Load configuration */
    global_config = config_create();
    if (!global_config) {
        fprintf(stderr, "Failed to create config\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    if (!config_load_default(global_config)) {
        fprintf(stderr, "Warning: Failed to load config, using defaults\n");
    }

    /* Create renderer with font configuration */
    RenderConfig render_config = {
        .font_path = NULL,           /* Will use system default */
        .font_path_bold = NULL,
        .font_path_italic = NULL,
        .font_path_bold_italic = NULL,
        .font_size_pt = 14,
        .dpi = 0,                    /* Auto-detect */
        .atlas_size = 1024
    };

    renderer = renderer_create(&render_config);
    if (!renderer) {
        fprintf(stderr, "Failed to create renderer\n");
        config_destroy(global_config);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    /* Create UI window */
    ui_window = window_create(800, 600);
    if (!ui_window) {
        fprintf(stderr, "Failed to create UI window\n");
        renderer_destroy(renderer);
        config_destroy(global_config);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    /* Store GLFW window handle in UI window for keybindings */
    ui_window->glfw_window = glfw_window;

    /* Main loop */
    while (!glfwWindowShouldClose(glfw_window)) {
        /* Get window size */
        int width, height;
        glfwGetFramebufferSize(glfw_window, &width, &height);

        /* Begin frame */
        renderer_begin_frame(renderer, width, height);

        /* Collect render commands from UI */
        window_collect_render_commands(ui_window, renderer);

        /* End frame (executes all render commands) */
        renderer_end_frame(renderer);

        /* Swap buffers and poll events */
        glfwSwapBuffers(glfw_window);
        glfwPollEvents();
    }

    /* Cleanup */
    window_destroy(ui_window);
    renderer_destroy(renderer);
    config_destroy(global_config);
    glfwTerminate();

    return EXIT_SUCCESS;
}

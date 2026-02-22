#ifndef UI_WINDOW_H
#define UI_WINDOW_H

#include "types.h"
#include "tab.h"
#include "../render/render.h"

/*
 * Window - Top-level UI container
 *
 * Manages multiple tabs and handles global input/rendering.
 * This is the main interface between GLFW and the UI system.
 */

#define WINDOW_MAX_TABS 32

struct Window {
    Tab *tabs[WINDOW_MAX_TABS];
    int tab_count;
    int active_tab_index;

    /* Window geometry */
    int width;
    int height;
    int tab_bar_height;       /* Height reserved for tab bar */

    /* GLFW window handle (optional - can be set later) */
    void *glfw_window;
};

/* Lifecycle */
Window *window_create(int width, int height);
void window_destroy(Window *window);

/* Tab management */
Tab *window_add_tab(Window *window, const char *title);
bool window_close_tab(Window *window, int index);
void window_switch_to_tab(Window *window, int index);
void window_next_tab(Window *window);
void window_prev_tab(Window *window);
Tab *window_get_active_tab(Window *window);

/* Split operations on active tab */
void window_split_horizontal(Window *window);
void window_split_vertical(Window *window);
void window_close_active_split(Window *window);

/* Window resizing */
void window_resize(Window *window, int width, int height);

/* Rendering */
void window_render(Window *window);  /* Legacy */
void window_render_tab_bar(Window *window);  /* Legacy */
void window_collect_render_commands(Window *window, Renderer *renderer);

/* Input handling */
void window_handle_key(Window *window, KeyEvent *event);
void window_handle_mouse(Window *window, MouseEvent *event);

/* Queries */
int window_get_tab_count(Window *window);
Tab *window_get_tab(Window *window, int index);

#endif /* UI_WINDOW_H */

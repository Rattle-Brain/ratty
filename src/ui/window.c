#include "window.h"
#include "../render/render_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Tab bar colors */
#define TABBAR_BG_COLOR     render_color_pack(45, 45, 45, 255)
#define TAB_ACTIVE_COLOR    render_color_pack(60, 60, 60, 255)
#define TAB_INACTIVE_COLOR  render_color_pack(50, 50, 50, 255)
#define TAB_TEXT_COLOR      render_color_pack(220, 220, 220, 255)
#define TAB_PADDING         10
#define TAB_MIN_WIDTH       80
#define TAB_GAP             2

Window *window_create(int width, int height) {
    Window *window = malloc(sizeof(Window));
    if (!window) return NULL;

    memset(window, 0, sizeof(Window));

    window->width = width;
    window->height = height;
    window->tab_bar_height = 30;  /* Reserve 30px for tab bar */
    window->tab_count = 0;
    window->active_tab_index = -1;

    /* Create initial tab */
    Tab *initial_tab = window_add_tab(window, "Terminal");
    if (!initial_tab) {
        free(window);
        return NULL;
    }

    return window;
}

void window_destroy(Window *window) {
    if (!window) return;

    /* Destroy all tabs */
    for (int i = 0; i < window->tab_count; i++) {
        if (window->tabs[i]) {
            tab_destroy(window->tabs[i]);
        }
    }

    free(window);
}

Tab *window_add_tab(Window *window, const char *title) {
    if (!window || window->tab_count >= WINDOW_MAX_TABS) {
        return NULL;
    }

    /* Calculate content area (exclude tab bar) */
    int content_height = window->height - window->tab_bar_height;

    /* Create new tab */
    Tab *tab = tab_create(title, window->width, content_height);
    if (!tab) return NULL;

    /* Add to tab list */
    tab->index = window->tab_count;
    window->tabs[window->tab_count] = tab;
    window->tab_count++;

    /* Set as active if it's the first tab */
    if (window->tab_count == 1) {
        window->active_tab_index = 0;
        tab->active = true;
    }

    return tab;
}

bool window_close_tab(Window *window, int index) {
    if (!window || index < 0 || index >= window->tab_count) {
        return false;
    }

    /* Don't close the last tab */
    if (window->tab_count == 1) {
        return false;
    }

    /* Destroy the tab */
    tab_destroy(window->tabs[index]);

    /* Shift remaining tabs */
    for (int i = index; i < window->tab_count - 1; i++) {
        window->tabs[i] = window->tabs[i + 1];
        window->tabs[i]->index = i;
    }

    window->tab_count--;
    window->tabs[window->tab_count] = NULL;

    /* Update active tab index */
    if (window->active_tab_index == index) {
        /* Switch to previous tab, or first if closing tab 0 */
        window->active_tab_index = (index > 0) ? index - 1 : 0;
        window->tabs[window->active_tab_index]->active = true;
    } else if (window->active_tab_index > index) {
        window->active_tab_index--;
    }

    return true;
}

void window_switch_to_tab(Window *window, int index) {
    if (!window || index < 0 || index >= window->tab_count) {
        return;
    }

    if (window->active_tab_index == index) {
        return;  /* Already active */
    }

    /* Deactivate current tab */
    if (window->active_tab_index >= 0) {
        window->tabs[window->active_tab_index]->active = false;
    }

    /* Activate new tab */
    window->active_tab_index = index;
    window->tabs[index]->active = true;
}

void window_next_tab(Window *window) {
    if (!window || window->tab_count <= 1) return;

    int next = (window->active_tab_index + 1) % window->tab_count;
    window_switch_to_tab(window, next);
}

void window_prev_tab(Window *window) {
    if (!window || window->tab_count <= 1) return;

    int prev = (window->active_tab_index - 1 + window->tab_count) % window->tab_count;
    window_switch_to_tab(window, prev);
}

Tab *window_get_active_tab(Window *window) {
    if (!window || window->active_tab_index < 0) {
        return NULL;
    }

    return window->tabs[window->active_tab_index];
}

void window_split_horizontal(Window *window) {
    Tab *tab = window_get_active_tab(window);
    if (tab) {
        tab_split_horizontal(tab, 0.5f);
    }
}

void window_split_vertical(Window *window) {
    Tab *tab = window_get_active_tab(window);
    if (tab) {
        tab_split_vertical(tab, 0.5f);
    }
}

void window_close_active_split(Window *window) {
    Tab *tab = window_get_active_tab(window);
    if (!tab) return;

    Split *focused = tab_get_focused_split(tab);
    if (focused) {
        tab_close_split(tab, focused);
    }
}

void window_resize(Window *window, int width, int height) {
    if (!window) return;

    window->width = width;
    window->height = height;

    /* Resize all tabs */
    int content_height = height - window->tab_bar_height;
    for (int i = 0; i < window->tab_count; i++) {
        tab_resize(window->tabs[i], width, content_height);
    }
}

void window_render(Window *window) {
    if (!window) return;

    /* Render tab bar */
    window_render_tab_bar(window);

    /* Render active tab content */
    Tab *active = window_get_active_tab(window);
    if (active) {
        tab_render(active);
    }
}

void window_render_tab_bar(Window *window) {
    if (!window) return;

    /* TODO: Render tab bar UI */
    /* - Draw background bar */
    /* - Draw tab buttons for each tab */
    /* - Highlight active tab */
    /* - Draw close buttons */
    /* - Handle tab bar interactions */
}

void window_collect_render_commands(Window *window, Renderer *renderer) {
    if (!window || !renderer) return;

    /* Collect tab bar render commands */
    /* Tab bar background */
    RenderCommand tabbar_bg = {
        .type = RENDER_CMD_RECT,
        .data.rect = {
            .rect = {0, 0, window->width, window->tab_bar_height},
            .color = TABBAR_BG_COLOR,
            .border_width = 0
        }
    };
    renderer_submit(renderer, &tabbar_bg);

    /* Render each tab button */
    int tab_x = TAB_PADDING;
    int cell_width = renderer_get_cell_width(renderer);
    int tab_height = window->tab_bar_height - 4;
    int tab_y = 2;

    for (int i = 0; i < window->tab_count; i++) {
        Tab *tab = window->tabs[i];
        if (!tab) continue;

        /* Calculate tab width based on title length */
        int title_len = (int)strlen(tab->title);
        int tab_width = title_len * cell_width + TAB_PADDING * 2;
        if (tab_width < TAB_MIN_WIDTH) tab_width = TAB_MIN_WIDTH;

        /* Tab background */
        uint32_t tab_color = (i == window->active_tab_index)
            ? TAB_ACTIVE_COLOR : TAB_INACTIVE_COLOR;

        RenderCommand tab_bg = {
            .type = RENDER_CMD_RECT,
            .data.rect = {
                .rect = {tab_x, tab_y, tab_width, tab_height},
                .color = tab_color,
                .border_width = 0
            }
        };
        renderer_submit(renderer, &tab_bg);

        /* Tab title text */
        RenderCommand tab_text = {
            .type = RENDER_CMD_TEXT_LINE,
            .data.text_line = {
                .x = tab_x + TAB_PADDING,
                .y = tab_y + (tab_height / 2) + 4,  /* Vertically center */
                .text = tab->title,
                .text_len = title_len,
                .fg_color = TAB_TEXT_COLOR,
                .bg_color = 0,
                .flags = 0
            }
        };
        renderer_submit(renderer, &tab_text);

        tab_x += tab_width + TAB_GAP;
    }

    /* Collect active tab content */
    Tab *active = window_get_active_tab(window);
    if (active) {
        tab_collect_render_commands(active, renderer, 0, window->tab_bar_height);
    }
}

void window_handle_key(Window *window, KeyEvent *event) {
    if (!window) return;

    /* TODO: Handle global keybindings */
    /* - Ctrl+T: New tab */
    /* - Ctrl+W: Close tab */
    /* - Ctrl+Tab: Next tab */
    /* - Ctrl+Shift+Tab: Previous tab */
    /* - Ctrl+\: Split horizontal */
    /* - Ctrl+-: Split vertical */
    /* - Ctrl+Shift+W: Close split */

    /* Otherwise, forward to active tab's focused split */
    Tab *active = window_get_active_tab(window);
    if (active) {
        Split *focused = tab_get_focused_split(active);
        if (focused && focused->pty) {
            /* TODO: Send key to PTY */
            /* pty_write_key(focused->pty, event); */
        }
    }
}

void window_handle_mouse(Window *window, MouseEvent *event) {
    if (!window) return;

    /* Check if click is in tab bar */
    if (event->y < window->tab_bar_height) {
        /* TODO: Handle tab bar clicks */
        /* - Tab selection */
        /* - Close button clicks */
        /* - Tab dragging for reordering */
        return;
    }

    /* Otherwise, forward to active tab */
    Tab *active = window_get_active_tab(window);
    if (active) {
        /* Adjust coordinates for content area */
        int content_y = event->y - window->tab_bar_height;

        /* Find which split was clicked */
        if (event->action == MOUSE_PRESS) {
            tab_focus_split_at(active, event->x, content_y);
        }

        /* TODO: Handle other mouse events (scrolling, selection, etc.) */
    }
}

int window_get_tab_count(Window *window) {
    return window ? window->tab_count : 0;
}

Tab *window_get_tab(Window *window, int index) {
    if (!window || index < 0 || index >= window->tab_count) {
        return NULL;
    }

    return window->tabs[index];
}

#include "keybindings.h"
#include "../config/config.h"
#include <GLFW/glfw3.h>

Action keybinding_lookup(int key, int mods) {
    if (!global_config) return ACTION_NONE;

    return config_lookup_keybinding(global_config, key, mods);
}

void keybinding_execute(Window *window, Action action) {
    if (!window) return;

    switch (action) {
        /* Tab management */
        case ACTION_NEW_TAB:
            window_add_tab(window, "Terminal");
            break;

        case ACTION_CLOSE_TAB:
            window_close_tab(window, window->active_tab_index);
            break;

        case ACTION_NEXT_TAB:
            window_next_tab(window);
            break;

        case ACTION_PREV_TAB:
            window_prev_tab(window);
            break;

        case ACTION_GOTO_TAB_1:
        case ACTION_GOTO_TAB_2:
        case ACTION_GOTO_TAB_3:
        case ACTION_GOTO_TAB_4:
        case ACTION_GOTO_TAB_5:
        case ACTION_GOTO_TAB_6:
        case ACTION_GOTO_TAB_7:
        case ACTION_GOTO_TAB_8:
        case ACTION_GOTO_TAB_9: {
            int index = action - ACTION_GOTO_TAB_1;
            window_switch_to_tab(window, index);
            break;
        }

        /* Split management */
        case ACTION_SPLIT_HORIZONTAL:
            window_split_horizontal(window);
            break;

        case ACTION_SPLIT_VERTICAL:
            window_split_vertical(window);
            break;

        case ACTION_CLOSE_SPLIT:
            window_close_active_split(window);
            break;

        case ACTION_FOCUS_UP:
        case ACTION_FOCUS_DOWN:
        case ACTION_FOCUS_LEFT:
        case ACTION_FOCUS_RIGHT:
            /* TODO: Implement directional focus navigation */
            break;

        /* Window */
        case ACTION_QUIT:
            /* Signal GLFW to close */
            if (window->glfw_window) {
                glfwSetWindowShouldClose(window->glfw_window, GLFW_TRUE);
            }
            break;

        case ACTION_FULLSCREEN:
            /* TODO: Toggle fullscreen */
            break;

        /* Clipboard */
        case ACTION_COPY:
            /* TODO: Copy selection to clipboard */
            break;

        case ACTION_PASTE:
            /* TODO: Paste from clipboard to active terminal */
            break;

        /* Scrollback */
        case ACTION_SCROLL_UP:
        case ACTION_SCROLL_DOWN:
        case ACTION_CLEAR_SCROLLBACK:
            /* TODO: Implement scrollback operations */
            break;

        default:
            break;
    }
}

bool keybinding_handle(Window *window, KeyEvent *event) {
    if (!window || !event) return false;

    /* Only handle key press events */
    if (event->action != KEY_PRESS) {
        return false;
    }

    /* Look up action */
    Action action = keybinding_lookup(event->key, event->mods);

    if (action != ACTION_NONE) {
        /* Execute the action */
        keybinding_execute(window, action);
        return true;  /* Event consumed */
    }

    return false;  /* No binding found - pass through to terminal */
}

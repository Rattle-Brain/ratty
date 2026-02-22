#include "tab.h"
#include <stdlib.h>
#include <string.h>

Tab *tab_create(const char *title, int width, int height) {
    Tab *tab = malloc(sizeof(Tab));
    if (!tab) return NULL;

    memset(tab, 0, sizeof(Tab));

    /* Set title */
    if (title) {
        strncpy(tab->title, title, TAB_TITLE_MAX - 1);
        tab->title[TAB_TITLE_MAX - 1] = '\0';
    } else {
        strcpy(tab->title, "Terminal");
    }

    /* Create root split (single terminal) */
    tab->root = split_create_leaf(width, height);
    if (!tab->root) {
        free(tab);
        return NULL;
    }

    /* Focus the root split */
    split_focus(tab->root);

    tab->active = false;
    tab->index = 0;

    return tab;
}

void tab_destroy(Tab *tab) {
    if (!tab) return;

    if (tab->root) {
        split_destroy(tab->root);
    }

    free(tab);
}

void tab_set_title(Tab *tab, const char *title) {
    if (!tab || !title) return;

    strncpy(tab->title, title, TAB_TITLE_MAX - 1);
    tab->title[TAB_TITLE_MAX - 1] = '\0';
}

const char *tab_get_title(Tab *tab) {
    return tab ? tab->title : NULL;
}

Split *tab_split_horizontal(Tab *tab, float ratio) {
    if (!tab) return NULL;

    /* Find the currently focused split */
    Split *focused = tab_get_focused_split(tab);
    if (!focused) {
        focused = tab->root;
    }

    /* Split it horizontally */
    Split *new_container = split_horizontal(focused, ratio);

    /* Update root if it changed */
    if (focused == tab->root && new_container) {
        tab->root = new_container;
    }

    return new_container;
}

Split *tab_split_vertical(Tab *tab, float ratio) {
    if (!tab) return NULL;

    /* Find the currently focused split */
    Split *focused = tab_get_focused_split(tab);
    if (!focused) {
        focused = tab->root;
    }

    /* Split it vertically */
    Split *new_container = split_vertical(focused, ratio);

    /* Update root if it changed */
    if (focused == tab->root && new_container) {
        tab->root = new_container;
    }

    return new_container;
}

bool tab_close_split(Tab *tab, Split *split) {
    if (!tab || !split) return false;

    /* Don't close if it's the only split */
    if (split == tab->root && split_is_leaf(split)) {
        return false;
    }

    bool restructured = split_close(split);

    /* If root's parent changed, update root pointer */
    if (tab->root->parent == NULL && tab->root != split) {
        /* Root is still valid */
    } else if (restructured) {
        /* Find new root (walk up from any remaining split) */
        Split *new_root = tab->root;
        while (new_root->parent) {
            new_root = new_root->parent;
        }
        tab->root = new_root;
    }

    return restructured;
}

void tab_resize(Tab *tab, int width, int height) {
    if (!tab || !tab->root) return;

    Rect bounds = {0, 0, width, height};
    split_recalculate_geometry(tab->root, bounds);
}

void tab_recalculate_layout(Tab *tab) {
    if (!tab || !tab->root) return;

    split_recalculate_geometry(tab->root, tab->root->bounds);
}

void tab_render(Tab *tab) {
    if (!tab || !tab->root) return;

    split_render(tab->root);
}

void tab_collect_render_commands(Tab *tab, Renderer *renderer, int offset_x, int offset_y) {
    if (!tab || !tab->root || !renderer) return;

    split_collect_render_commands(tab->root, renderer, offset_x, offset_y);
}

Split *tab_get_focused_split(Tab *tab) {
    if (!tab || !tab->root) return NULL;

    return split_find_focused(tab->root);
}

void tab_focus_next_split(Tab *tab) {
    /* TODO: Implement focus traversal */
    /* Find current focused split, then find next leaf in tree order */
}

void tab_focus_prev_split(Tab *tab) {
    /* TODO: Implement focus traversal */
    /* Find current focused split, then find previous leaf in tree order */
}

void tab_focus_split_at(Tab *tab, int x, int y) {
    if (!tab || !tab->root) return;

    /* Blur current focused split */
    Split *current = tab_get_focused_split(tab);
    if (current) {
        split_blur(current);
    }

    /* Find and focus split at position */
    Split *target = split_find_at_position(tab->root, x, y);
    if (target) {
        split_focus(target);
    }
}

int tab_get_split_count(Tab *tab) {
    if (!tab || !tab->root) return 0;

    return split_count_leaves(tab->root);
}

bool tab_is_active(Tab *tab) {
    return tab && tab->active;
}

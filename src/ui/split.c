#include "split.h"
#include "../render/render_types.h"
#include <stdlib.h>
#include <string.h>

/* Private helpers */
static void split_destroy_recursive(Split *split);
static void split_render_recursive(Split *split);
static void split_render_divider(Split *split);
static void split_collect_recursive(Split *split, Renderer *renderer, int offset_x, int offset_y);

/* Default colors */
#define COLOR_BG_DEFAULT    render_color_pack(30, 30, 30, 255)
#define COLOR_FG_DEFAULT    render_color_pack(220, 220, 220, 255)
#define COLOR_DIVIDER       render_color_pack(80, 80, 80, 255)
#define COLOR_FOCUS_BORDER  render_color_pack(100, 149, 237, 255)

Split *split_create_leaf(int width, int height) {
    Split *split = malloc(sizeof(Split));
    if (!split) return NULL;

    memset(split, 0, sizeof(Split));
    split->type = SPLIT_LEAF;
    split->bounds.width = width;
    split->bounds.height = height;
    split->focused = false;
    split->parent = NULL;

    /* TODO: Initialize PTY and screen buffer */
    /* split->pty = pty_create(); */
    /* split->screen = screen_create(width, height); */

    return split;
}

Split *split_create_container(SplitType type, Split *child1, Split *child2, float ratio) {
    if (!child1 || !child2 || type == SPLIT_LEAF) {
        return NULL;
    }

    Split *split = malloc(sizeof(Split));
    if (!split) return NULL;

    memset(split, 0, sizeof(Split));
    split->type = type;
    split->child1 = child1;
    split->child2 = child2;
    split->ratio = (ratio > 0.0f && ratio < 1.0f) ? ratio : 0.5f;

    child1->parent = split;
    child2->parent = split;

    return split;
}

void split_destroy(Split *split) {
    split_destroy_recursive(split);
}

static void split_destroy_recursive(Split *split) {
    if (!split) return;

    if (split->type == SPLIT_LEAF) {
        /* TODO: Cleanup PTY and screen buffer */
        /* if (split->pty) pty_destroy(split->pty); */
        /* if (split->screen) screen_destroy(split->screen); */
    } else {
        split_destroy_recursive(split->child1);
        split_destroy_recursive(split->child2);
    }

    free(split);
}

Split *split_horizontal(Split *leaf, float ratio) {
    if (!leaf || leaf->type != SPLIT_LEAF) {
        return NULL;
    }

    /* Create new leaf for the right side */
    Split *new_leaf = split_create_leaf(leaf->bounds.width, leaf->bounds.height);
    if (!new_leaf) return NULL;

    /* Create container to hold both */
    Split *container = split_create_container(SPLIT_HORIZONTAL, leaf, new_leaf, ratio);
    if (!container) {
        split_destroy(new_leaf);
        return NULL;
    }

    /* Update parent relationship */
    if (leaf->parent) {
        Split *parent = leaf->parent;
        if (parent->child1 == leaf) {
            parent->child1 = container;
        } else {
            parent->child2 = container;
        }
        container->parent = parent;
    }

    container->bounds = leaf->bounds;
    split_recalculate_geometry(container, container->bounds);

    return container;
}

Split *split_vertical(Split *leaf, float ratio) {
    if (!leaf || leaf->type != SPLIT_LEAF) {
        return NULL;
    }

    /* Create new leaf for the bottom */
    Split *new_leaf = split_create_leaf(leaf->bounds.width, leaf->bounds.height);
    if (!new_leaf) return NULL;

    /* Create container to hold both */
    Split *container = split_create_container(SPLIT_VERTICAL, leaf, new_leaf, ratio);
    if (!container) {
        split_destroy(new_leaf);
        return NULL;
    }

    /* Update parent relationship */
    if (leaf->parent) {
        Split *parent = leaf->parent;
        if (parent->child1 == leaf) {
            parent->child1 = container;
        } else {
            parent->child2 = container;
        }
        container->parent = parent;
    }

    container->bounds = leaf->bounds;
    split_recalculate_geometry(container, container->bounds);

    return container;
}

bool split_close(Split *split) {
    if (!split || !split->parent) {
        /* Can't close root or NULL */
        return false;
    }

    Split *parent = split->parent;
    Split *sibling = (parent->child1 == split) ? parent->child2 : parent->child1;

    /* Replace parent with sibling */
    if (parent->parent) {
        Split *grandparent = parent->parent;
        if (grandparent->child1 == parent) {
            grandparent->child1 = sibling;
        } else {
            grandparent->child2 = sibling;
        }
        sibling->parent = grandparent;
    } else {
        /* Parent was root - sibling becomes new root */
        sibling->parent = NULL;
    }

    /* Destroy the closing split */
    split_destroy_recursive(split);

    /* Don't destroy parent's other child (sibling) */
    parent->child1 = NULL;
    parent->child2 = NULL;
    free(parent);

    return true;
}

void split_recalculate_geometry(Split *split, Rect bounds) {
    if (!split) return;

    split->bounds = bounds;

    if (split->type == SPLIT_LEAF) {
        /* Leaf node - just update bounds */
        /* TODO: Resize PTY to match new bounds */
        return;
    }

    /* Container node - split space between children */
    const int divider_size = 1;  /* 1 pixel divider */

    if (split->type == SPLIT_HORIZONTAL) {
        int split_x = bounds.x + (int)(bounds.width * split->ratio);

        Rect left_bounds = {
            bounds.x,
            bounds.y,
            split_x - bounds.x - divider_size,
            bounds.height
        };

        Rect right_bounds = {
            split_x + divider_size,
            bounds.y,
            bounds.x + bounds.width - split_x - divider_size,
            bounds.height
        };

        split_recalculate_geometry(split->child1, left_bounds);
        split_recalculate_geometry(split->child2, right_bounds);

    } else if (split->type == SPLIT_VERTICAL) {
        int split_y = bounds.y + (int)(bounds.height * split->ratio);

        Rect top_bounds = {
            bounds.x,
            bounds.y,
            bounds.width,
            split_y - bounds.y - divider_size
        };

        Rect bottom_bounds = {
            bounds.x,
            split_y + divider_size,
            bounds.width,
            bounds.y + bounds.height - split_y - divider_size
        };

        split_recalculate_geometry(split->child1, top_bounds);
        split_recalculate_geometry(split->child2, bottom_bounds);
    }
}

void split_set_ratio(Split *split, float ratio) {
    if (!split || split->type == SPLIT_LEAF) return;

    if (ratio < 0.1f) ratio = 0.1f;
    if (ratio > 0.9f) ratio = 0.9f;

    split->ratio = ratio;
    split_recalculate_geometry(split, split->bounds);
}

float split_get_ratio(Split *split) {
    return (split && split->type != SPLIT_LEAF) ? split->ratio : 0.5f;
}

void split_render(Split *split) {
    if (!split) return;
    split_render_recursive(split);
}

static void split_render_recursive(Split *split) {
    if (!split) return;

    if (split->type == SPLIT_LEAF) {
        /* TODO: Render terminal content */
        /* render_terminal(split->pty, split->screen, split->bounds); */

        /* TODO: Draw focus border if focused */
        /* if (split->focused) draw_focus_border(split->bounds); */
    } else {
        /* Render children recursively */
        split_render_recursive(split->child1);
        split_render_recursive(split->child2);

        /* Draw divider line */
        split_render_divider(split);
    }
}

static void split_render_divider(Split *split) {
    if (!split || split->type == SPLIT_LEAF) return;

    /* TODO: Draw divider line between child1 and child2 */
    /* Use OpenGL to draw a line at the split point */
}

Split *split_find_focused(Split *root) {
    if (!root) return NULL;

    if (root->type == SPLIT_LEAF) {
        return root->focused ? root : NULL;
    }

    Split *focused = split_find_focused(root->child1);
    if (focused) return focused;

    return split_find_focused(root->child2);
}

Split *split_find_at_position(Split *root, int x, int y) {
    if (!root) return NULL;

    /* Check if point is within this split's bounds */
    if (x < root->bounds.x || x >= root->bounds.x + root->bounds.width ||
        y < root->bounds.y || y >= root->bounds.y + root->bounds.height) {
        return NULL;
    }

    if (root->type == SPLIT_LEAF) {
        return root;
    }

    /* Check children */
    Split *found = split_find_at_position(root->child1, x, y);
    if (found) return found;

    return split_find_at_position(root->child2, x, y);
}

Split *split_get_next(Split *current, int direction) {
    /* TODO: Implement directional navigation */
    /* 0=up, 1=right, 2=down, 3=left */
    /* Walk up the tree to find appropriate sibling */
    return NULL;
}

void split_focus(Split *split) {
    if (!split || split->type != SPLIT_LEAF) return;
    split->focused = true;
}

void split_blur(Split *split) {
    if (!split || split->type != SPLIT_LEAF) return;
    split->focused = false;
}

bool split_is_leaf(Split *split) {
    return split && split->type == SPLIT_LEAF;
}

bool split_is_container(Split *split) {
    return split && split->type != SPLIT_LEAF;
}

int split_count_leaves(Split *root) {
    if (!root) return 0;

    if (root->type == SPLIT_LEAF) {
        return 1;
    }

    return split_count_leaves(root->child1) + split_count_leaves(root->child2);
}

void split_resize(Split *split, int width, int height) {
    if (!split) return;

    Rect new_bounds = split->bounds;
    new_bounds.width = width;
    new_bounds.height = height;

    split_recalculate_geometry(split, new_bounds);
}

void split_collect_render_commands(Split *split, Renderer *renderer, int offset_x, int offset_y) {
    if (!split || !renderer) return;
    split_collect_recursive(split, renderer, offset_x, offset_y);
}

static void split_collect_recursive(Split *split, Renderer *renderer, int offset_x, int offset_y) {
    if (!split || !renderer) return;

    if (split->type == SPLIT_LEAF) {
        /* Render terminal pane background */
        RenderCommand bg_cmd = {
            .type = RENDER_CMD_RECT,
            .data.rect = {
                .rect = {
                    offset_x + split->bounds.x,
                    offset_y + split->bounds.y,
                    split->bounds.width,
                    split->bounds.height
                },
                .color = COLOR_BG_DEFAULT,
                .border_width = 0
            }
        };
        renderer_submit(renderer, &bg_cmd);

        /* TODO: When terminal state is implemented, render the cell grid here */
        /* For now, just show background */

        /* Draw focus border if focused */
        if (split->focused) {
            RenderCommand border_cmd = {
                .type = RENDER_CMD_RECT,
                .data.rect = {
                    .rect = {
                        offset_x + split->bounds.x,
                        offset_y + split->bounds.y,
                        split->bounds.width,
                        split->bounds.height
                    },
                    .color = COLOR_FOCUS_BORDER,
                    .border_width = 2
                }
            };
            renderer_submit(renderer, &border_cmd);
        }
    } else {
        /* Container node - render children recursively */
        split_collect_recursive(split->child1, renderer, offset_x, offset_y);
        split_collect_recursive(split->child2, renderer, offset_x, offset_y);

        /* Draw divider line between children */
        const int divider_size = 1;

        if (split->type == SPLIT_HORIZONTAL) {
            /* Vertical divider line */
            int split_x = split->bounds.x + (int)(split->bounds.width * split->ratio);
            RenderCommand div_cmd = {
                .type = RENDER_CMD_RECT,
                .data.rect = {
                    .rect = {
                        offset_x + split_x - divider_size,
                        offset_y + split->bounds.y,
                        divider_size * 2,
                        split->bounds.height
                    },
                    .color = COLOR_DIVIDER,
                    .border_width = 0
                }
            };
            renderer_submit(renderer, &div_cmd);
        } else if (split->type == SPLIT_VERTICAL) {
            /* Horizontal divider line */
            int split_y = split->bounds.y + (int)(split->bounds.height * split->ratio);
            RenderCommand div_cmd = {
                .type = RENDER_CMD_RECT,
                .data.rect = {
                    .rect = {
                        offset_x + split->bounds.x,
                        offset_y + split_y - divider_size,
                        split->bounds.width,
                        divider_size * 2
                    },
                    .color = COLOR_DIVIDER,
                    .border_width = 0
                }
            };
            renderer_submit(renderer, &div_cmd);
        }
    }
}

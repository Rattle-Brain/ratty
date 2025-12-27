#ifndef UI_SPLIT_H
#define UI_SPLIT_H

#include "types.h"
#include "../core/pty.h"
#include "../render/render.h"

/*
 * Split - Binary tree structure for terminal panes
 *
 * A split can be:
 * 1. A LEAF node - contains an actual terminal (PTY + screen buffer)
 * 2. A CONTAINER node - contains 2 child splits (vertical or horizontal)
 *
 * Example layout:
 *     [Root: HORIZONTAL]
 *          /        \
 *    [Terminal A]  [VERTICAL]
 *                   /      \
 *            [Terminal B] [Terminal C]
 */

typedef enum {
    SPLIT_LEAF,         /* Actual terminal pane */
    SPLIT_HORIZONTAL,   /* Left/Right split */
    SPLIT_VERTICAL      /* Top/Bottom split */
} SplitType;

typedef struct Split Split;

struct Split {
    SplitType type;

    /* Geometry */
    Rect bounds;

    /* If LEAF: contains terminal data */
    PTY *pty;                    /* Pseudo-terminal handle */
    void *screen;                /* Screen buffer (terminal state) */
    bool focused;

    /* If CONTAINER: contains child splits */
    Split *child1;               /* Left or Top */
    Split *child2;               /* Right or Bottom */
    float ratio;                 /* Split ratio: 0.0 to 1.0 (default 0.5) */

    /* Hierarchy */
    Split *parent;
};

/* Lifecycle */
Split *split_create_leaf(int width, int height);
Split *split_create_container(SplitType type, Split *child1, Split *child2, float ratio);
void split_destroy(Split *split);

/* Splitting operations */
Split *split_horizontal(Split *leaf, float ratio);  /* Split left/right */
Split *split_vertical(Split *leaf, float ratio);    /* Split top/bottom */

/* Closing panes */
bool split_close(Split *split);  /* Close this pane, returns true if tree restructured */

/* Layout */
void split_recalculate_geometry(Split *split, Rect bounds);
void split_set_ratio(Split *split, float ratio);
float split_get_ratio(Split *split);

/* Rendering */
void split_render(Split *split);  /* Legacy - calls collect with NULL renderer */
void split_collect_render_commands(Split *split, Renderer *renderer, int offset_x, int offset_y);

/* Navigation */
Split *split_find_focused(Split *root);
Split *split_find_at_position(Split *root, int x, int y);
Split *split_get_next(Split *current, int direction);  /* 0=up, 1=right, 2=down, 3=left */

/* Focus */
void split_focus(Split *split);
void split_blur(Split *split);

/* Queries */
bool split_is_leaf(Split *split);
bool split_is_container(Split *split);
int split_count_leaves(Split *root);  /* Count terminal panes */

/* Resizing */
void split_resize(Split *split, int width, int height);

#endif /* UI_SPLIT_H */

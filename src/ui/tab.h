#ifndef UI_TAB_H
#define UI_TAB_H

#include "types.h"
#include "split.h"
#include "../render/render.h"

/*
 * Tab - Container for a split tree
 *
 * Each tab contains one root split (which may have many child splits).
 * Tabs are managed by the Window and displayed in a tab bar.
 */

#define TAB_TITLE_MAX 256

struct Tab {
    char title[TAB_TITLE_MAX];
    Split *root;              /* Root of the split tree */
    int index;                /* Position in tab list */
    bool active;              /* Is this the active tab? */
};

/* Lifecycle */
Tab *tab_create(const char *title, int width, int height);
void tab_destroy(Tab *tab);

/* Title management */
void tab_set_title(Tab *tab, const char *title);
const char *tab_get_title(Tab *tab);

/* Split operations - delegates to root split */
Split *tab_split_horizontal(Tab *tab, float ratio);
Split *tab_split_vertical(Tab *tab, float ratio);
bool tab_close_split(Tab *tab, Split *split);

/* Layout */
void tab_resize(Tab *tab, int width, int height);
void tab_recalculate_layout(Tab *tab);

/* Rendering */
void tab_render(Tab *tab);  /* Legacy */
void tab_collect_render_commands(Tab *tab, Renderer *renderer, int offset_x, int offset_y);

/* Focus management */
Split *tab_get_focused_split(Tab *tab);
void tab_focus_next_split(Tab *tab);
void tab_focus_prev_split(Tab *tab);
void tab_focus_split_at(Tab *tab, int x, int y);

/* Queries */
int tab_get_split_count(Tab *tab);
bool tab_is_active(Tab *tab);

#endif /* UI_TAB_H */

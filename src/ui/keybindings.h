#ifndef UI_KEYBINDINGS_H
#define UI_KEYBINDINGS_H

#include "types.h"
#include "window.h"
#include "../config/config.h"

/*
 * Keybindings - Global keyboard shortcut handler
 *
 * Maps key combinations to actions using the config system.
 * Keybindings are loaded from default_config.yaml.
 */

/* Keybinding lookup */
Action keybinding_lookup(int key, int mods);

/* Execute action */
void keybinding_execute(Window *window, Action action);

/* Handle key event (combines lookup + execute) */
bool keybinding_handle(Window *window, KeyEvent *event);

#endif /* UI_KEYBINDINGS_H */

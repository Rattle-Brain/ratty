#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Configuration system
 *
 * Parses default_config.yaml and loads settings.
 * For now, focuses on keybindings. Will expand later for colors, fonts, etc.
 */

#define CONFIG_MAX_KEYBINDINGS 128
#define CONFIG_MAX_KEY_STRING 64

typedef enum {
    ACTION_NONE = 0,

    /* Tab management */
    ACTION_NEW_TAB,
    ACTION_CLOSE_TAB,
    ACTION_NEXT_TAB,
    ACTION_PREV_TAB,
    ACTION_GOTO_TAB_1,
    ACTION_GOTO_TAB_2,
    ACTION_GOTO_TAB_3,
    ACTION_GOTO_TAB_4,
    ACTION_GOTO_TAB_5,
    ACTION_GOTO_TAB_6,
    ACTION_GOTO_TAB_7,
    ACTION_GOTO_TAB_8,
    ACTION_GOTO_TAB_9,

    /* Split management */
    ACTION_SPLIT_HORIZONTAL,
    ACTION_SPLIT_VERTICAL,
    ACTION_CLOSE_SPLIT,
    ACTION_FOCUS_UP,
    ACTION_FOCUS_DOWN,
    ACTION_FOCUS_LEFT,
    ACTION_FOCUS_RIGHT,

    /* Window */
    ACTION_QUIT,
    ACTION_FULLSCREEN,

    /* Clipboard */
    ACTION_COPY,
    ACTION_PASTE,

    /* Scrollback */
    ACTION_SCROLL_UP,
    ACTION_SCROLL_DOWN,
    ACTION_CLEAR_SCROLLBACK,

} Action;

typedef struct {
    int key;           /* GLFW key code */
    int mods;          /* Modifier mask (Ctrl, Shift, Alt) */
    Action action;
} KeyBinding;

typedef struct {
    /* Keybindings */
    KeyBinding keybindings[CONFIG_MAX_KEYBINDINGS];
    int keybinding_count;

    /* TODO: Colors, fonts, window settings, etc. */

} Config;

/* Global config instance */
extern Config *global_config;

/* Lifecycle */
Config *config_create(void);
void config_destroy(Config *config);

/* Loading */
bool config_load_from_file(Config *config, const char *path);
bool config_load_default(Config *config);

/* Keybinding queries */
Action config_lookup_keybinding(Config *config, int key, int mods);

/* Helpers */
const char *action_to_string(Action action);
Action string_to_action(const char *str);

#endif /* CONFIG_H */

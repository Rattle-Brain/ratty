#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <GLFW/glfw3.h>

/* Global config instance */
Config *global_config = NULL;

/* Helper functions */
static int parse_key_string(const char *key_str);
static int parse_mods_string(const char *mod_str);
static bool parse_keybinding_line(const char *line, KeyBinding *binding);

Config *config_create(void) {
    Config *config = malloc(sizeof(Config));
    if (!config) return NULL;

    memset(config, 0, sizeof(Config));
    return config;
}

void config_destroy(Config *config) {
    if (!config) return;
    free(config);
}

bool config_load_default(Config *config) {
    /* Try to load from default location */
    const char *default_path = "src/config/default_config.yaml";
    return config_load_from_file(config, default_path);
}

bool config_load_from_file(Config *config, const char *path) {
    if (!config || !path) return false;

    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Failed to open config file: %s\n", path);
        return false;
    }

    /*
     * Simple YAML parser for keybindings
     *
     * This is a minimal parser that only handles the keybindings section.
     * For a production system, use libyaml or similar.
     *
     * Expected format:
     *   keybindings:
     *     "ctrl+shift+t": new_tab
     *     "ctrl+w": close_split
     */

    char line[256];
    bool in_keybindings = false;
    config->keybinding_count = 0;

    while (fgets(line, sizeof(line), file)) {
        /* Remove trailing newline */
        line[strcspn(line, "\n")] = 0;

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0') continue;

        /* Check for keybindings section */
        if (strncmp(line, "keybindings:", 12) == 0) {
            in_keybindings = true;
            continue;
        }

        /* Check for other sections (exit keybindings) */
        if (line[0] != ' ' && line[0] != '\t' && strchr(line, ':')) {
            in_keybindings = false;
            continue;
        }

        /* Parse keybinding line if in keybindings section */
        if (in_keybindings && (line[0] == ' ' || line[0] == '\t')) {
            KeyBinding binding;
            if (parse_keybinding_line(line, &binding)) {
                if (config->keybinding_count < CONFIG_MAX_KEYBINDINGS) {
                    config->keybindings[config->keybinding_count++] = binding;
                }
            }
        }
    }

    fclose(file);

    printf("Loaded %d keybindings from %s\n", config->keybinding_count, path);
    return true;
}

static bool parse_keybinding_line(const char *line, KeyBinding *binding) {
    /*
     * Parse line format: "  "ctrl+shift+t": new_tab"
     */

    /* Skip leading whitespace */
    while (*line == ' ' || *line == '\t') line++;

    /* Find the key string (between quotes) */
    if (*line != '"') return false;
    line++;

    const char *key_start = line;
    const char *key_end = strchr(line, '"');
    if (!key_end) return false;

    char key_string[CONFIG_MAX_KEY_STRING];
    int key_len = key_end - key_start;
    if (key_len >= CONFIG_MAX_KEY_STRING) return false;

    strncpy(key_string, key_start, key_len);
    key_string[key_len] = '\0';

    /* Find the action string (after colon) */
    line = key_end + 1;
    const char *colon = strchr(line, ':');
    if (!colon) return false;

    line = colon + 1;
    while (*line == ' ' || *line == '\t') line++;

    char action_string[64];
    int i = 0;
    while (*line && *line != ' ' && *line != '\n' && i < 63) {
        action_string[i++] = *line++;
    }
    action_string[i] = '\0';

    /* Parse key string into key + mods */
    char *plus = strchr(key_string, '+');
    int mods = 0;
    int key = 0;

    if (plus) {
        /* Has modifiers */
        *plus = '\0';
        mods = parse_mods_string(key_string);
        key = parse_key_string(plus + 1);
    } else {
        /* No modifiers */
        key = parse_key_string(key_string);
    }

    if (key == 0) return false;

    /* Parse action string */
    Action action = string_to_action(action_string);
    if (action == ACTION_NONE) return false;

    binding->key = key;
    binding->mods = mods;
    binding->action = action;

    return true;
}

static int parse_mods_string(const char *mod_str) {
    /*
     * Parse modifier string like "ctrl+shift" or "ctrl+alt+shift"
     */
    int mods = 0;
    char buffer[CONFIG_MAX_KEY_STRING];
    strncpy(buffer, mod_str, CONFIG_MAX_KEY_STRING - 1);
    buffer[CONFIG_MAX_KEY_STRING - 1] = '\0';

    char *token = strtok(buffer, "+");
    while (token) {
        if (strcmp(token, "ctrl") == 0 || strcmp(token, "control") == 0) {
            mods |= GLFW_MOD_CONTROL;
        } else if (strcmp(token, "shift") == 0) {
            mods |= GLFW_MOD_SHIFT;
        } else if (strcmp(token, "alt") == 0) {
            mods |= GLFW_MOD_ALT;
        } else if (strcmp(token, "super") == 0 || strcmp(token, "cmd") == 0) {
            mods |= GLFW_MOD_SUPER;
        }
        token = strtok(NULL, "+");
    }

    return mods;
}

static int parse_key_string(const char *key_str) {
    /*
     * Map key name to GLFW key code
     */

    /* Single character keys */
    if (strlen(key_str) == 1) {
        char c = toupper(key_str[0]);
        if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');

        /* Special characters */
        switch (c) {
            case '\\': return GLFW_KEY_BACKSLASH;
            case '-': return GLFW_KEY_MINUS;
            case '=': return GLFW_KEY_EQUAL;
            case '[': return GLFW_KEY_LEFT_BRACKET;
            case ']': return GLFW_KEY_RIGHT_BRACKET;
            case ';': return GLFW_KEY_SEMICOLON;
            case '\'': return GLFW_KEY_APOSTROPHE;
            case ',': return GLFW_KEY_COMMA;
            case '.': return GLFW_KEY_PERIOD;
            case '/': return GLFW_KEY_SLASH;
            case '`': return GLFW_KEY_GRAVE_ACCENT;
        }
    }

    /* Named keys */
    if (strcmp(key_str, "tab") == 0) return GLFW_KEY_TAB;
    if (strcmp(key_str, "space") == 0) return GLFW_KEY_SPACE;
    if (strcmp(key_str, "enter") == 0) return GLFW_KEY_ENTER;
    if (strcmp(key_str, "backspace") == 0) return GLFW_KEY_BACKSPACE;
    if (strcmp(key_str, "delete") == 0) return GLFW_KEY_DELETE;
    if (strcmp(key_str, "escape") == 0 || strcmp(key_str, "esc") == 0) return GLFW_KEY_ESCAPE;

    /* Arrow keys */
    if (strcmp(key_str, "up") == 0) return GLFW_KEY_UP;
    if (strcmp(key_str, "down") == 0) return GLFW_KEY_DOWN;
    if (strcmp(key_str, "left") == 0) return GLFW_KEY_LEFT;
    if (strcmp(key_str, "right") == 0) return GLFW_KEY_RIGHT;

    /* Function keys */
    if (strcmp(key_str, "f1") == 0) return GLFW_KEY_F1;
    if (strcmp(key_str, "f2") == 0) return GLFW_KEY_F2;
    if (strcmp(key_str, "f3") == 0) return GLFW_KEY_F3;
    if (strcmp(key_str, "f4") == 0) return GLFW_KEY_F4;
    if (strcmp(key_str, "f5") == 0) return GLFW_KEY_F5;
    if (strcmp(key_str, "f6") == 0) return GLFW_KEY_F6;
    if (strcmp(key_str, "f7") == 0) return GLFW_KEY_F7;
    if (strcmp(key_str, "f8") == 0) return GLFW_KEY_F8;
    if (strcmp(key_str, "f9") == 0) return GLFW_KEY_F9;
    if (strcmp(key_str, "f10") == 0) return GLFW_KEY_F10;
    if (strcmp(key_str, "f11") == 0) return GLFW_KEY_F11;
    if (strcmp(key_str, "f12") == 0) return GLFW_KEY_F12;

    /* Other */
    if (strcmp(key_str, "pageup") == 0) return GLFW_KEY_PAGE_UP;
    if (strcmp(key_str, "pagedown") == 0) return GLFW_KEY_PAGE_DOWN;
    if (strcmp(key_str, "home") == 0) return GLFW_KEY_HOME;
    if (strcmp(key_str, "end") == 0) return GLFW_KEY_END;

    return 0;  /* Unknown key */
}

Action config_lookup_keybinding(Config *config, int key, int mods) {
    if (!config) return ACTION_NONE;

    for (int i = 0; i < config->keybinding_count; i++) {
        if (config->keybindings[i].key == key &&
            config->keybindings[i].mods == mods) {
            return config->keybindings[i].action;
        }
    }

    return ACTION_NONE;
}

Action string_to_action(const char *str) {
    if (!str) return ACTION_NONE;

    /* Tab management */
    if (strcmp(str, "new_tab") == 0) return ACTION_NEW_TAB;
    if (strcmp(str, "close_tab") == 0) return ACTION_CLOSE_TAB;
    if (strcmp(str, "next_tab") == 0) return ACTION_NEXT_TAB;
    if (strcmp(str, "prev_tab") == 0) return ACTION_PREV_TAB;
    if (strcmp(str, "goto_tab_1") == 0) return ACTION_GOTO_TAB_1;
    if (strcmp(str, "goto_tab_2") == 0) return ACTION_GOTO_TAB_2;
    if (strcmp(str, "goto_tab_3") == 0) return ACTION_GOTO_TAB_3;
    if (strcmp(str, "goto_tab_4") == 0) return ACTION_GOTO_TAB_4;
    if (strcmp(str, "goto_tab_5") == 0) return ACTION_GOTO_TAB_5;
    if (strcmp(str, "goto_tab_6") == 0) return ACTION_GOTO_TAB_6;
    if (strcmp(str, "goto_tab_7") == 0) return ACTION_GOTO_TAB_7;
    if (strcmp(str, "goto_tab_8") == 0) return ACTION_GOTO_TAB_8;
    if (strcmp(str, "goto_tab_9") == 0) return ACTION_GOTO_TAB_9;

    /* Split management */
    if (strcmp(str, "split_horizontal") == 0) return ACTION_SPLIT_HORIZONTAL;
    if (strcmp(str, "split_vertical") == 0) return ACTION_SPLIT_VERTICAL;
    if (strcmp(str, "close_split") == 0) return ACTION_CLOSE_SPLIT;
    if (strcmp(str, "focus_next_split") == 0) return ACTION_FOCUS_NEXT_SPLIT;
    if (strcmp(str, "focus_prev_split") == 0) return ACTION_FOCUS_PREV_SPLIT;
    if (strcmp(str, "focus_up") == 0) return ACTION_FOCUS_UP;
    if (strcmp(str, "focus_down") == 0) return ACTION_FOCUS_DOWN;
    if (strcmp(str, "focus_left") == 0) return ACTION_FOCUS_LEFT;
    if (strcmp(str, "focus_right") == 0) return ACTION_FOCUS_RIGHT;

    /* Window */
    if (strcmp(str, "quit") == 0) return ACTION_QUIT;
    if (strcmp(str, "fullscreen") == 0) return ACTION_FULLSCREEN;

    /* Clipboard */
    if (strcmp(str, "copy") == 0) return ACTION_COPY;
    if (strcmp(str, "paste") == 0) return ACTION_PASTE;

    /* Scrollback */
    if (strcmp(str, "scroll_up") == 0) return ACTION_SCROLL_UP;
    if (strcmp(str, "scroll_down") == 0) return ACTION_SCROLL_DOWN;
    if (strcmp(str, "clear_scrollback") == 0) return ACTION_CLEAR_SCROLLBACK;

    return ACTION_NONE;
}

const char *action_to_string(Action action) {
    switch (action) {
        case ACTION_NEW_TAB: return "new_tab";
        case ACTION_CLOSE_TAB: return "close_tab";
        case ACTION_NEXT_TAB: return "next_tab";
        case ACTION_PREV_TAB: return "prev_tab";
        case ACTION_GOTO_TAB_1: return "goto_tab_1";
        case ACTION_GOTO_TAB_2: return "goto_tab_2";
        case ACTION_GOTO_TAB_3: return "goto_tab_3";
        case ACTION_GOTO_TAB_4: return "goto_tab_4";
        case ACTION_GOTO_TAB_5: return "goto_tab_5";
        case ACTION_GOTO_TAB_6: return "goto_tab_6";
        case ACTION_GOTO_TAB_7: return "goto_tab_7";
        case ACTION_GOTO_TAB_8: return "goto_tab_8";
        case ACTION_GOTO_TAB_9: return "goto_tab_9";
        case ACTION_SPLIT_HORIZONTAL: return "split_horizontal";
        case ACTION_SPLIT_VERTICAL: return "split_vertical";
        case ACTION_CLOSE_SPLIT: return "close_split";
        case ACTION_FOCUS_UP: return "focus_up";
        case ACTION_FOCUS_DOWN: return "focus_down";
        case ACTION_FOCUS_LEFT: return "focus_left";
        case ACTION_FOCUS_RIGHT: return "focus_right";
        case ACTION_QUIT: return "quit";
        case ACTION_FULLSCREEN: return "fullscreen";
        case ACTION_COPY: return "copy";
        case ACTION_PASTE: return "paste";
        case ACTION_SCROLL_UP: return "scroll_up";
        case ACTION_SCROLL_DOWN: return "scroll_down";
        case ACTION_CLEAR_SCROLLBACK: return "clear_scrollback";
        default: return "none";
    }
}

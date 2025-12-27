#ifndef UI_TYPES_H
#define UI_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct Window Window;
typedef struct Tab Tab;
typedef struct Split Split;
typedef struct Widget Widget;
typedef struct WidgetVTable WidgetVTable;
typedef struct Rect Rect;
typedef struct KeyEvent KeyEvent;
typedef struct MouseEvent MouseEvent;

/* Geometry */
struct Rect {
    int x, y;
    int width, height;
};

/* Input Events */
typedef enum {
    KEY_PRESS,
    KEY_RELEASE,
    KEY_REPEAT
} KeyAction;

struct KeyEvent {
    int key;           /* GLFW key code */
    int scancode;
    KeyAction action;
    int mods;          /* Shift, Ctrl, Alt, etc. */
};

typedef enum {
    MOUSE_PRESS,
    MOUSE_RELEASE,
    MOUSE_MOVE,
    MOUSE_SCROLL
} MouseAction;

struct MouseEvent {
    MouseAction action;
    int button;        /* 0=left, 1=right, 2=middle */
    double x, y;
    double scroll_x, scroll_y;
    int mods;
};

/* Colors */
typedef struct {
    float r, g, b, a;  /* 0.0 to 1.0 */
} Color;

/* Common return codes */
typedef enum {
    UI_OK = 0,
    UI_ERROR = -1,
    UI_ERROR_NOMEM = -2,
    UI_ERROR_INVALID = -3
} UIResult;

#endif /* UI_TYPES_H */

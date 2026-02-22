#ifndef RENDER_TYPES_H
#define RENDER_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct Renderer Renderer;
typedef struct RenderCommand RenderCommand;

/* Text attribute flags */
#define ATTR_BOLD          (1 << 0)
#define ATTR_ITALIC        (1 << 1)
#define ATTR_UNDERLINE     (1 << 2)
#define ATTR_STRIKETHROUGH (1 << 3)
#define ATTR_BLINK         (1 << 4)
#define ATTR_INVERSE       (1 << 5)
#define ATTR_INVISIBLE     (1 << 6)
#define ATTR_DIM           (1 << 7)

/* Terminal cell for rendering */
typedef struct {
    uint32_t codepoint;     /* Unicode codepoint */
    uint32_t fg_color;      /* Foreground RGBA (packed) */
    uint32_t bg_color;      /* Background RGBA (packed) */
    uint8_t  flags;         /* ATTR_* flags */
} RenderCell;

/* Rectangle region */
typedef struct {
    int x, y;
    int width, height;
} RenderRect;

/* Render command types */
typedef enum {
    RENDER_CMD_CLEAR,       /* Clear region with color */
    RENDER_CMD_RECT,        /* Draw filled/bordered rectangle */
    RENDER_CMD_TEXT_GRID,   /* Render terminal cell grid */
    RENDER_CMD_TEXT_LINE,   /* Render single line of text */
    RENDER_CMD_CURSOR       /* Draw cursor */
} RenderCommandType;

/* Clear command data */
typedef struct {
    RenderRect region;
    uint32_t color;
} RenderClearCmd;

/* Rectangle command data */
typedef struct {
    RenderRect rect;
    uint32_t color;
    int border_width;       /* 0 for filled, >0 for border only */
} RenderRectCmd;

/* Terminal grid command data */
typedef struct {
    RenderRect region;      /* Pixel bounds */
    const RenderCell *cells;/* Row-major cell data */
    int cols;               /* Grid columns */
    int rows;               /* Grid rows */
    int scroll_offset;      /* Scrollback offset (rows) */
} RenderGridCmd;

/* Text line command (for UI elements like tab bar) */
typedef struct {
    int x, y;               /* Position */
    const char *text;       /* UTF-8 text */
    int text_len;           /* Byte length (-1 for null-terminated) */
    uint32_t fg_color;
    uint32_t bg_color;      /* 0 for transparent */
    uint8_t flags;          /* ATTR_* flags */
} RenderTextLineCmd;

/* Cursor styles */
typedef enum {
    CURSOR_BLOCK,
    CURSOR_UNDERLINE,
    CURSOR_BAR
} CursorStyle;

/* Cursor command data */
typedef struct {
    int x, y;               /* Cell position */
    int width, height;      /* Pixel size */
    uint32_t color;
    CursorStyle style;
    bool visible;           /* Blink state */
} RenderCursorCmd;

/* Union of all command data */
typedef union {
    RenderClearCmd clear;
    RenderRectCmd rect;
    RenderGridCmd grid;
    RenderTextLineCmd text_line;
    RenderCursorCmd cursor;
} RenderCommandData;

/* Render command */
struct RenderCommand {
    RenderCommandType type;
    RenderCommandData data;
};

/* Color utilities */
static inline uint32_t render_color_pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
}

static inline void render_color_unpack(uint32_t color, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    *r = (color >> 24) & 0xFF;
    *g = (color >> 16) & 0xFF;
    *b = (color >> 8) & 0xFF;
    *a = color & 0xFF;
}

#endif /* RENDER_TYPES_H */

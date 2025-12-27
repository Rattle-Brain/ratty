#ifndef UTILS_RETCODES_H
#define UTILS_RETCODES_H

/*
 * Return Codes for Ratty Terminal Emulator
 *
 * Error code ranges:
 *   0       - Success
 *   1-9     - General/Generic errors
 *   10-19   - Memory errors
 *   20-29   - File/IO errors
 *   30-39   - Configuration errors
 *   40-49   - Rendering errors
 *   50-59   - Font errors
 *   60-69   - OpenGL errors
 *   70-79   - PTY/Terminal core errors
 *   80-89   - UI component errors
 *   90-99   - Input handling errors
 *   100-109 - IPC/Signal errors
 *   110-119 - Platform-specific errors
 */

/* ============================================================================
 * SUCCESS
 * ============================================================================ */
#define SUCCESS                         0

/* ============================================================================
 * GENERAL ERRORS (1-9)
 * ============================================================================ */
#define ERR_GENERIC                     1   /* Unspecified error */
#define ERR_INVALID_ARGUMENT            2   /* Invalid function argument */
#define ERR_NULL_POINTER                3   /* Unexpected NULL pointer */
#define ERR_NOT_INITIALIZED             4   /* Component not initialized */
#define ERR_ALREADY_INITIALIZED         5   /* Component already initialized */
#define ERR_NOT_SUPPORTED               6   /* Operation not supported */
#define ERR_NOT_IMPLEMENTED             7   /* Feature not yet implemented */
#define ERR_TIMEOUT                     8   /* Operation timed out */
#define ERR_CANCELLED                   9   /* Operation was cancelled */

/* ============================================================================
 * MEMORY ERRORS (10-19)
 * ============================================================================ */
#define ERR_MEMORY_ALLOC                10  /* Memory allocation failed (malloc) */
#define ERR_MEMORY_REALLOC              11  /* Memory reallocation failed (realloc) */
#define ERR_MEMORY_FREE                 12  /* Memory free error (double free, etc.) */
#define ERR_MEMORY_BUFFER_OVERFLOW      13  /* Buffer overflow detected */
#define ERR_MEMORY_BUFFER_UNDERFLOW     14  /* Buffer underflow detected */
#define ERR_MEMORY_OUT_OF_BOUNDS        15  /* Array/buffer index out of bounds */
#define ERR_MEMORY_POOL_EXHAUSTED       16  /* Memory pool exhausted */
#define ERR_MEMORY_ALIGNMENT            17  /* Memory alignment error */
#define ERR_MEMORY_LEAK                 18  /* Memory leak detected */
#define ERR_MEMORY_CORRUPTED            19  /* Memory corruption detected */

/* ============================================================================
 * FILE/IO ERRORS (20-29)
 * ============================================================================ */
#define ERR_FILE_NOT_FOUND              20  /* File does not exist */
#define ERR_FILE_OPEN                   21  /* Failed to open file */
#define ERR_FILE_READ                   22  /* Failed to read from file */
#define ERR_FILE_WRITE                  23  /* Failed to write to file */
#define ERR_FILE_CLOSE                  24  /* Failed to close file */
#define ERR_FILE_PERMISSION             25  /* Permission denied */
#define ERR_FILE_EXISTS                 26  /* File already exists */
#define ERR_FILE_IS_DIRECTORY           27  /* Expected file, got directory */
#define ERR_FILE_INVALID_PATH           28  /* Invalid file path */
#define ERR_FILE_TOO_LARGE              29  /* File too large */

/* ============================================================================
 * CONFIGURATION ERRORS (30-39)
 * ============================================================================ */
#define ERR_CONFIG_LOAD                 30  /* Failed to load configuration */
#define ERR_CONFIG_PARSE                31  /* Failed to parse configuration */
#define ERR_CONFIG_INVALID_KEY          32  /* Invalid configuration key */
#define ERR_CONFIG_INVALID_VALUE        33  /* Invalid configuration value */
#define ERR_CONFIG_MISSING_KEY          34  /* Required configuration key missing */
#define ERR_CONFIG_TYPE_MISMATCH        35  /* Configuration value type mismatch */
#define ERR_CONFIG_SAVE                 36  /* Failed to save configuration */
#define ERR_CONFIG_VERSION              37  /* Configuration version mismatch */
#define ERR_CONFIG_KEYBIND_INVALID      38  /* Invalid keybinding specification */
#define ERR_CONFIG_KEYBIND_CONFLICT     39  /* Keybinding conflict detected */

/* ============================================================================
 * RENDERING ERRORS (40-49)
 * ============================================================================ */
#define ERR_RENDER_INIT                 40  /* Renderer initialization failed */
#define ERR_RENDER_CONTEXT              41  /* Render context error */
#define ERR_RENDER_COMMAND_OVERFLOW     42  /* Too many render commands */
#define ERR_RENDER_VERTEX_OVERFLOW      43  /* Vertex buffer overflow */
#define ERR_RENDER_TEXTURE_CREATE       44  /* Failed to create texture */
#define ERR_RENDER_TEXTURE_UPLOAD       45  /* Failed to upload texture data */
#define ERR_RENDER_FRAMEBUFFER          46  /* Framebuffer error */
#define ERR_RENDER_VIEWPORT             47  /* Invalid viewport dimensions */
#define ERR_RENDER_BATCH_FULL           48  /* Render batch is full */
#define ERR_RENDER_INVALID_STATE        49  /* Renderer in invalid state */

/* ============================================================================
 * FONT ERRORS (50-59)
 * ============================================================================ */
#define ERR_FONT_INIT                   50  /* Font system initialization failed */
#define ERR_FONT_LOAD                   51  /* Failed to load font file */
#define ERR_FONT_INVALID_FORMAT         52  /* Invalid font file format */
#define ERR_FONT_NO_DEFAULT             53  /* No default font found */
#define ERR_FONT_GLYPH_NOT_FOUND        54  /* Glyph not found in font */
#define ERR_FONT_RASTERIZE              55  /* Glyph rasterization failed */
#define ERR_FONT_SIZE_INVALID           56  /* Invalid font size */
#define ERR_FONT_METRICS                57  /* Failed to get font metrics */
#define ERR_FONT_SHAPING                58  /* Text shaping failed (HarfBuzz) */
#define ERR_FONT_STYLE_NOT_LOADED       59  /* Requested font style not loaded */

/* ============================================================================
 * OPENGL ERRORS (60-69)
 * ============================================================================ */
#define ERR_GL_INIT                     60  /* OpenGL initialization failed */
#define ERR_GL_VERSION                  61  /* OpenGL version not supported */
#define ERR_GL_SHADER_COMPILE           62  /* Shader compilation failed */
#define ERR_GL_SHADER_LINK              63  /* Shader program linking failed */
#define ERR_GL_UNIFORM_NOT_FOUND        64  /* Shader uniform not found */
#define ERR_GL_BUFFER_CREATE            65  /* Failed to create GL buffer */
#define ERR_GL_VAO_CREATE               66  /* Failed to create VAO */
#define ERR_GL_TEXTURE_CREATE           67  /* Failed to create GL texture */
#define ERR_GL_CONTEXT_LOST             68  /* OpenGL context lost */
#define ERR_GL_INVALID_OPERATION        69  /* Invalid OpenGL operation */

/* ============================================================================
 * PTY/TERMINAL CORE ERRORS (70-79)
 * ============================================================================ */
#define ERR_PTY_CREATE                  70  /* Failed to create PTY */
#define ERR_PTY_FORK                    71  /* Fork failed */
#define ERR_PTY_EXEC                    72  /* Exec failed (shell not found) */
#define ERR_PTY_READ                    73  /* Failed to read from PTY */
#define ERR_PTY_WRITE                   74  /* Failed to write to PTY */
#define ERR_PTY_RESIZE                  75  /* Failed to resize PTY */
#define ERR_PTY_CLOSE                   76  /* Failed to close PTY */
#define ERR_PTY_SIGNAL                  77  /* Failed to send signal to PTY */
#define ERR_PTY_POLL                    78  /* Poll error on PTY fd */
#define ERR_PTY_CHILD_EXIT              79  /* Child process exited unexpectedly */

/* ============================================================================
 * UI COMPONENT ERRORS (80-89)
 * ============================================================================ */
#define ERR_UI_WINDOW_CREATE            80  /* Failed to create window */
#define ERR_UI_WINDOW_RESIZE            81  /* Failed to resize window */
#define ERR_UI_TAB_CREATE               82  /* Failed to create tab */
#define ERR_UI_TAB_MAX_REACHED          83  /* Maximum tabs reached */
#define ERR_UI_TAB_NOT_FOUND            84  /* Tab not found */
#define ERR_UI_SPLIT_CREATE             85  /* Failed to create split */
#define ERR_UI_SPLIT_INVALID            86  /* Invalid split operation */
#define ERR_UI_SPLIT_NOT_FOUND          87  /* Split not found */
#define ERR_UI_FOCUS_ERROR              88  /* Focus operation failed */
#define ERR_UI_LAYOUT_ERROR             89  /* Layout calculation error */

/* ============================================================================
 * INPUT HANDLING ERRORS (90-99)
 * ============================================================================ */
#define ERR_INPUT_INIT                  90  /* Input system initialization failed */
#define ERR_INPUT_KEY_UNKNOWN           91  /* Unknown key code */
#define ERR_INPUT_MOD_INVALID           92  /* Invalid modifier combination */
#define ERR_INPUT_MOUSE_BOUNDS          93  /* Mouse position out of bounds */
#define ERR_INPUT_CLIPBOARD_READ        94  /* Failed to read from clipboard */
#define ERR_INPUT_CLIPBOARD_WRITE       95  /* Failed to write to clipboard */
#define ERR_INPUT_SELECTION_INVALID     96  /* Invalid text selection */
#define ERR_INPUT_ENCODING              97  /* Character encoding error */
#define ERR_INPUT_SEQUENCE_INVALID      98  /* Invalid escape sequence */
#define ERR_INPUT_BUFFER_FULL           99  /* Input buffer full */

/* ============================================================================
 * IPC/SIGNAL ERRORS (100-109)
 * ============================================================================ */
#define ERR_IPC_INIT                    100 /* IPC initialization failed */
#define ERR_IPC_CONNECT                 101 /* IPC connection failed */
#define ERR_IPC_SEND                    102 /* IPC send failed */
#define ERR_IPC_RECEIVE                 103 /* IPC receive failed */
#define ERR_IPC_TIMEOUT                 104 /* IPC operation timed out */
#define ERR_SIGNAL_HANDLER              105 /* Failed to set signal handler */
#define ERR_SIGNAL_SEND                 106 /* Failed to send signal */
#define ERR_SIGNAL_BLOCKED              107 /* Signal is blocked */
#define ERR_PIPE_CREATE                 108 /* Failed to create pipe */
#define ERR_PIPE_IO                     109 /* Pipe I/O error */

/* ============================================================================
 * PLATFORM-SPECIFIC ERRORS (110-119)
 * ============================================================================ */
#define ERR_PLATFORM_INIT               110 /* Platform initialization failed */
#define ERR_PLATFORM_GLFW               111 /* GLFW error */
#define ERR_PLATFORM_DISPLAY            112 /* Display/monitor error */
#define ERR_PLATFORM_VSYNC              113 /* VSync setup failed */
#define ERR_PLATFORM_FULLSCREEN         114 /* Fullscreen toggle failed */
#define ERR_PLATFORM_CURSOR             115 /* Cursor operation failed */
#define ERR_PLATFORM_LOCALE             116 /* Locale setup failed */
#define ERR_PLATFORM_ENVIRONMENT        117 /* Environment variable error */
#define ERR_PLATFORM_PERMISSION         118 /* Platform permission denied */
#define ERR_PLATFORM_NOT_AVAILABLE      119 /* Platform feature not available */

/* ============================================================================
 * TEXTURE ATLAS ERRORS (120-129)
 * ============================================================================ */
#define ERR_ATLAS_CREATE                120 /* Failed to create texture atlas */
#define ERR_ATLAS_FULL                  121 /* Texture atlas is full */
#define ERR_ATLAS_ALLOCATE              122 /* Failed to allocate atlas region */
#define ERR_ATLAS_UPLOAD                123 /* Failed to upload to atlas */
#define ERR_ATLAS_RESIZE                124 /* Failed to resize atlas */
#define ERR_ATLAS_MAX_SIZE              125 /* Atlas reached maximum size */
#define ERR_ATLAS_INVALID_REGION        126 /* Invalid atlas region */
#define ERR_ATLAS_PACK                  127 /* Atlas packing algorithm failed */

/* ============================================================================
 * GLYPH CACHE ERRORS (130-139)
 * ============================================================================ */
#define ERR_CACHE_CREATE                130 /* Failed to create cache */
#define ERR_CACHE_FULL                  131 /* Cache is full */
#define ERR_CACHE_MISS                  132 /* Cache miss (not an error, info) */
#define ERR_CACHE_EVICT                 133 /* Cache eviction failed */
#define ERR_CACHE_LOOKUP                134 /* Cache lookup failed */
#define ERR_CACHE_INSERT                135 /* Cache insert failed */
#define ERR_CACHE_INVALIDATE            136 /* Cache invalidation failed */

/* ============================================================================
 * VT/ESCAPE SEQUENCE ERRORS (140-149)
 * ============================================================================ */
#define ERR_VT_PARSE                    140 /* VT sequence parse error */
#define ERR_VT_SEQUENCE_UNKNOWN         141 /* Unknown escape sequence */
#define ERR_VT_SEQUENCE_INCOMPLETE      142 /* Incomplete escape sequence */
#define ERR_VT_PARAM_INVALID            143 /* Invalid sequence parameter */
#define ERR_VT_MODE_UNSUPPORTED         144 /* Unsupported terminal mode */
#define ERR_VT_CHARSET_UNKNOWN          145 /* Unknown character set */
#define ERR_VT_CURSOR_BOUNDS            146 /* Cursor out of bounds */
#define ERR_VT_SCROLLBACK_FULL          147 /* Scrollback buffer full */

/* ============================================================================
 * SCREEN BUFFER ERRORS (150-159)
 * ============================================================================ */
#define ERR_SCREEN_CREATE               150 /* Failed to create screen buffer */
#define ERR_SCREEN_RESIZE               151 /* Failed to resize screen buffer */
#define ERR_SCREEN_SCROLL               152 /* Scroll operation failed */
#define ERR_SCREEN_CLEAR                153 /* Clear operation failed */
#define ERR_SCREEN_CELL_ACCESS          154 /* Invalid cell access */
#define ERR_SCREEN_LINE_ACCESS          155 /* Invalid line access */
#define ERR_SCREEN_REGION_INVALID       156 /* Invalid screen region */

/* ============================================================================
 * HELPER MACROS
 * ============================================================================ */

/* Check if return code indicates success */
#define IS_SUCCESS(code)    ((code) == SUCCESS)
#define IS_ERROR(code)      ((code) != SUCCESS)

/* Get error category (tens digit) */
#define ERROR_CATEGORY(code) (((code) / 10) * 10)

/* Error category constants */
#define ERR_CAT_GENERAL     0
#define ERR_CAT_MEMORY      10
#define ERR_CAT_FILE        20
#define ERR_CAT_CONFIG      30
#define ERR_CAT_RENDER      40
#define ERR_CAT_FONT        50
#define ERR_CAT_GL          60
#define ERR_CAT_PTY         70
#define ERR_CAT_UI          80
#define ERR_CAT_INPUT       90
#define ERR_CAT_IPC         100
#define ERR_CAT_PLATFORM    110
#define ERR_CAT_ATLAS       120
#define ERR_CAT_CACHE       130
#define ERR_CAT_VT          140
#define ERR_CAT_SCREEN      150

/* Convert error code to string (declaration - implement in retcodes.c if needed) */
const char *retcode_to_string(int code);

#endif /* UTILS_RETCODES_H */

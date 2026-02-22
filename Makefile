# Ratty Terminal Emulator Makefile

# Project name
PROJECT = ratty
EXAMPLE = ratty_ui_example

# Directories
SRC_DIR = src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Isrc

# Platform detection
UNAME_S := $(shell uname -s)

# FreeType and HarfBuzz (for font rendering)
ifeq ($(UNAME_S),Darwin)
    # macOS with Homebrew
    HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo "/opt/homebrew")
    FREETYPE_PREFIX := $(shell brew --prefix freetype 2>/dev/null || echo "$(HOMEBREW_PREFIX)/opt/freetype")
    HARFBUZZ_PREFIX := $(shell brew --prefix harfbuzz 2>/dev/null || echo "$(HOMEBREW_PREFIX)/opt/harfbuzz")
    FREETYPE_CFLAGS = -I$(FREETYPE_PREFIX)/include/freetype2
    FREETYPE_LDFLAGS = -L$(FREETYPE_PREFIX)/lib -lfreetype
    HARFBUZZ_CFLAGS = -I$(HARFBUZZ_PREFIX)/include/harfbuzz
    HARFBUZZ_LDFLAGS = -L$(HARFBUZZ_PREFIX)/lib -lharfbuzz
    GL_LDFLAGS = -framework OpenGL
else
    # Linux - try pkg-config first, fallback to system paths
    FREETYPE_CFLAGS = $(shell pkg-config --cflags freetype2 2>/dev/null || echo "-I/usr/include/freetype2")
    FREETYPE_LDFLAGS = $(shell pkg-config --libs freetype2 2>/dev/null || echo "-lfreetype")
    HARFBUZZ_CFLAGS = $(shell pkg-config --cflags harfbuzz 2>/dev/null || echo "-I/usr/include/harfbuzz")
    HARFBUZZ_LDFLAGS = $(shell pkg-config --libs harfbuzz 2>/dev/null || echo "-lharfbuzz")
    GL_LDFLAGS = -lGL
endif

CFLAGS += $(FREETYPE_CFLAGS) $(HARFBUZZ_CFLAGS)
LDFLAGS = -lglfw -lutil -lm $(FREETYPE_LDFLAGS) $(HARFBUZZ_LDFLAGS) $(GL_LDFLAGS)

# Debug/Release flags
DEBUG_FLAGS = -g -O0 -DDEBUG
RELEASE_FLAGS = -O3 -DNDEBUG

# Default to debug build
MODE ?= debug
ifeq ($(MODE),release)
    CFLAGS += $(RELEASE_FLAGS)
else
    CFLAGS += $(DEBUG_FLAGS)
endif

# Source files
CORE_SOURCES = $(SRC_DIR)/core/pty.c
UI_SOURCES = $(SRC_DIR)/ui/split.c \
             $(SRC_DIR)/ui/tab.c \
             $(SRC_DIR)/ui/window.c \
             $(SRC_DIR)/ui/keybindings.c
CONFIG_SOURCES = $(SRC_DIR)/config/config.c
RENDER_SOURCES = $(SRC_DIR)/render/render.c \
                 $(SRC_DIR)/render/gl_backend.c \
                 $(SRC_DIR)/render/font.c \
                 $(SRC_DIR)/render/text_shaper.c \
                 $(SRC_DIR)/render/glyph_cache.c \
                 $(SRC_DIR)/render/texture_atlas.c

MAIN_SOURCE = $(SRC_DIR)/main.c
EXAMPLE_SOURCE = $(SRC_DIR)/ui/example_integration.c

# All library sources (everything except main files)
LIB_SOURCES = $(CORE_SOURCES) $(UI_SOURCES) $(CONFIG_SOURCES) $(RENDER_SOURCES)

# Object files
LIB_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(LIB_SOURCES))
MAIN_OBJECT = $(OBJ_DIR)/main.o
EXAMPLE_OBJECT = $(OBJ_DIR)/ui/example_integration.o

# Executables
MAIN_EXECUTABLE = $(BIN_DIR)/$(PROJECT)
EXAMPLE_EXECUTABLE = $(BIN_DIR)/$(EXAMPLE)

# Targets
.PHONY: all clean run example debug release help dirs

# Default target
all: dirs $(MAIN_EXECUTABLE)

# Create necessary directories
dirs:
	@mkdir -p $(OBJ_DIR)/core
	@mkdir -p $(OBJ_DIR)/ui
	@mkdir -p $(OBJ_DIR)/config
	@mkdir -p $(OBJ_DIR)/render
	@mkdir -p $(BIN_DIR)

# Build main executable
$(MAIN_EXECUTABLE): $(LIB_OBJECTS) $(MAIN_OBJECT)
	@echo "Linking $(PROJECT)..."
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Build UI example
example: dirs $(EXAMPLE_EXECUTABLE)

$(EXAMPLE_EXECUTABLE): $(LIB_OBJECTS) $(EXAMPLE_OBJECT)
	@echo "Linking $(EXAMPLE)..."
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Compile source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Convenience targets
debug:
	@$(MAKE) MODE=debug all

release:
	@$(MAKE) MODE=release all

run: all
	@echo "Running $(PROJECT)..."
	@$(MAIN_EXECUTABLE)

run-example: example
	@echo "Running $(EXAMPLE)..."
	@$(EXAMPLE_EXECUTABLE)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete"

# Deep clean (including old test binaries)
distclean: clean
	@echo "Deep cleaning..."
	@rm -f $(SRC_DIR)/pty_test
	@echo "Deep clean complete"

# Rebuild everything
rebuild: clean all

# Help target
help:
	@echo "Ratty Terminal Emulator - Makefile targets:"
	@echo ""
	@echo "  make              - Build main executable (debug mode)"
	@echo "  make all          - Same as 'make'"
	@echo "  make example      - Build UI example"
	@echo "  make debug        - Build in debug mode (default)"
	@echo "  make release      - Build in release mode with optimizations"
	@echo "  make run          - Build and run main executable"
	@echo "  make run-example  - Build and run UI example"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make distclean    - Remove all generated files"
	@echo "  make rebuild      - Clean and rebuild"
	@echo "  make help         - Show this help message"
	@echo ""
	@echo "Build modes:"
	@echo "  MODE=debug        - Debug build with symbols (default)"
	@echo "  MODE=release      - Release build with optimizations"
	@echo ""
	@echo "Examples:"
	@echo "  make MODE=release              - Build release version"
	@echo "  make clean && make MODE=release - Clean rebuild in release mode"

# Dependency tracking (automatically generate dependencies)
-include $(LIB_OBJECTS:.o=.d)
-include $(MAIN_OBJECT:.o=.d)
-include $(EXAMPLE_OBJECT:.o=.d)

# Generate dependency files
$(OBJ_DIR)/%.d: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MM -MT $(OBJ_DIR)/$*.o $< > $@